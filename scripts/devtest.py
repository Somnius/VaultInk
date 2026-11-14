#!/usr/bin/env python3
"""Plays a JSON scenario on a reader over the developer link (docs/devlink.md, "Device tests").

    devtest.py scenario.json               run it; steps.jsonl and the pictures go to .work/devtest/<run>/
    devtest.py scenario.json --dry-run     print the commands it would send; no port is opened

A scenario:

    {"name": "open-settings",
     "requires": {"build": "dev", "battery_min": 40},
     "forbid_activity": ["WifiSelection"],
     "timeout_s": 120,
     "steps": [
       {"wait": {"activity": "Home", "timeout_ms": 8000}},
       {"key": "DOWN"},
       {"key": {"name": "CONFIRM", "hold_ms": 200}},
       {"tap": [240, 400]},
       {"swipe": [400, 300, 80, 300]},
       {"wait": {}},
       {"expect": {"status": {"activity": "Settings", "heap_free": {">": 60000}}}},
       {"shot": "settings.png"},
       {"sleep_ms": 500},
       {"cmd": "STATUS LITE"}
     ]}

Exit code 0: every step passed. 1: a step failed, a time ran out, or the run was stopped (a
forbidden screen, the battery). 2: it did not run (a bad scenario, no port, not the developer's
own build, the battery below the floor).

It never presses the power key and never sends REBOOT, BOOTLOADER, FLASH, PUT, BENCH or LOG: a
scenario that asks for one is refused before the port is opened. Only the x4pro-dev build (STATUS
build "dev") is driven. Standard library only; one open port for the whole run.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import devlink  # noqa: E402  pylint: disable=wrong-import-position

EXIT_PASS = 0
EXIT_FAIL = 1
EXIT_NOT_RUN = 2

REQUIRED_BUILD = "dev"
BATTERY_FLOOR = 30  # percent; a scenario may ask for more, never for less
BATTERY_EVERY_STEPS = 10
STEP_TIMEOUT_MS = 10_000
MAX_STEP_TIMEOUT_MS = 120_000
RUN_TIMEOUT_S = 300
MAX_RUN_TIMEOUT_S = 3600
MAX_STEPS = 500
MAX_SLEEP_MS = 10_000
# Screens a test must never sit in. A scenario adds to the list and cannot take from it. On the
# first two the port goes away, the others write firmware.
FORBIDDEN_ACTIVITIES = ("UsbDrive", "Sleep", "OtaUpdate", "SdFirmwareUpdate")
# What a {"cmd": ...} step may send: commands that only read. Everything else is refused; the
# ones below are named so the message says why.
READ_ONLY_COMMANDS = ("PING", "HELP", "STATUS", "LS", "SHOTCRC")
REFUSED_COMMANDS = {
    "POWER": "the link never presses the power key",
    "REBOOT": "a test does not restart the reader",
    "BOOTLOADER": "a test does not put the reader in download mode",
    "FLASH": "a test does not write firmware",
    "PUT": "a test does not write to the SD card",
    "BENCH": "a test does not change the bench log setting",
    "LOG": "a test does not change the log level",
    "KEY": 'use a {"key": ...} step',
    "TAP": 'use a {"tap": ...} step',
    "SWIPE": 'use a {"swipe": ...} step',
    "WAIT": 'use a {"wait": ...} step',
    "SCREENSHOT": 'use a {"shot": ...} step',
    "GET": "not part of a scenario: copy files with devlink.py get",
}
COMPARISONS = {
    "==": lambda a, b: a == b,
    "!=": lambda a, b: a != b,
    ">": lambda a, b: a > b,
    ">=": lambda a, b: a >= b,
    "<": lambda a, b: a < b,
    "<=": lambda a, b: a <= b,
}
STEP_KINDS = ("key", "tap", "swipe", "wait", "expect", "shot", "sleep_ms", "cmd")
STEP_OPTIONS = ("timeout_ms", "note")


class ScenarioError(ValueError):
    """The scenario file is wrong or asks for something a test may not do."""


class StepFailed(Exception):
    """A step did not pass. `diagnose` is False when the reader must be left alone."""

    def __init__(self, reason: str, diagnose: bool = True, code: int = EXIT_FAIL):
        super().__init__(reason)
        self.reason = reason
        self.diagnose = diagnose
        self.code = code


# ---- the scenario (no I/O) ----


def _is_int(value) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _one_line(text, what: str) -> str:
    """Text that goes into a command line: a control character would end the line and start
    another command."""
    if not isinstance(text, str) or not text or any(ord(c) < 0x20 or ord(c) == 0x7F for c in text):
        raise ScenarioError(f"{what} must be text on one line")
    return text


def _fields(value, what: str, required, optional=()):
    if not isinstance(value, dict):
        raise ScenarioError(f"{what} must be an object")
    unknown = [key for key in value if key not in required and key not in optional]
    missing = [key for key in required if key not in value]
    if unknown or missing:
        raise ScenarioError(f"{what}: unknown {unknown}, missing {missing}")
    return value


def _numbers(value, what: str, names, optional):
    """[a, b, ...] with an optional last number, or an object with those names."""
    if isinstance(value, list):
        if not len(names) <= len(value) <= len(names) + 1 or not all(_is_int(n) for n in value):
            raise ScenarioError(f"{what} must be {len(names)} whole numbers, and the time if wanted")
        return value[: len(names)], (value[len(names)] if len(value) > len(names) else None)
    value = _fields(value, what, names, (optional,))
    if not all(_is_int(n) for n in value.values()):
        raise ScenarioError(f"{what} takes whole numbers")
    return [value[name] for name in names], value.get(optional)


def cmd_text(text) -> str:
    """The text of a {"cmd": ...} step: a command that only reads."""
    text = _one_line(text, "cmd").strip()
    if text.upper().startswith("CMD:"):
        text = text[4:].strip()
    name = text.split(" ", 1)[0] if text else ""
    upper = name.upper()
    if upper in REFUSED_COMMANDS:
        raise ScenarioError(f"cmd {upper} is not allowed in a scenario: {REFUSED_COMMANDS[upper]}")
    if name not in READ_ONLY_COMMANDS:
        raise ScenarioError(f"cmd {name!r} is not allowed in a scenario; only " + " ".join(READ_ONLY_COMMANDS))
    if name == "STATUS" and text not in ("STATUS", "STATUS LITE"):
        raise ScenarioError("cmd STATUS takes nothing, or LITE")
    if name == "LS":
        try:
            devlink.sd_path(text[2:].strip())
        except ValueError as error:
            raise ScenarioError(str(error)) from error
    elif name not in ("STATUS",) and text != name:
        raise ScenarioError(f"cmd {name} takes no arguments")
    if len(f"CMD:{text}".encode()) > devlink.MAX_COMMAND_BYTES:
        raise ScenarioError("cmd is longer than a command line")
    return text


def _expectation(field, rule):
    """[(op, value), ...] for one STATUS field: a plain value means ==."""
    if not isinstance(field, str) or not field:
        raise ScenarioError("expect: a STATUS field name must be text")
    if not isinstance(rule, dict):
        if rule is None or isinstance(rule, list):
            raise ScenarioError(f"expect {field}: a value, or an object such as {{\">\": 5}}")
        return [("==", rule)]
    if not rule:
        raise ScenarioError(f"expect {field}: no comparison given")
    checks = []
    for op, value in rule.items():
        if op not in COMPARISONS:
            raise ScenarioError(f"expect {field}: {op!r} is not one of " + " ".join(COMPARISONS))
        if op in ("==", "!="):
            if isinstance(value, (dict, list)) or value is None:
                raise ScenarioError(f"expect {field}: {op} takes a number, text, true or false")
        elif not _is_number(value):
            raise ScenarioError(f"expect {field}: {op} takes a number")
        checks.append((op, value))
    return checks


def parse_step(raw, number: int) -> dict:
    """One step as the runner takes it: {"kind", "commands" (what is sent), ...}."""
    what = f"step {number}"
    if not isinstance(raw, dict):
        raise ScenarioError(f"{what} must be an object")
    kinds = [key for key in raw if key in STEP_KINDS]
    unknown = [key for key in raw if key not in STEP_KINDS and key not in STEP_OPTIONS]
    if unknown:
        raise ScenarioError(f"{what}: unknown {unknown}; a step is one of " + " ".join(STEP_KINDS))
    if len(kinds) != 1:
        raise ScenarioError(f"{what} must have exactly one of " + " ".join(STEP_KINDS))
    kind = kinds[0]
    value = raw[kind]
    timeout_ms = raw.get("timeout_ms", STEP_TIMEOUT_MS)
    if not _is_int(timeout_ms) or not 100 <= timeout_ms <= MAX_STEP_TIMEOUT_MS:
        raise ScenarioError(f"{what}: timeout_ms must be 100 to {MAX_STEP_TIMEOUT_MS}")
    step = {"number": number, "kind": kind, "timeout_ms": timeout_ms, "commands": []}
    if "note" in raw:
        step["note"] = str(raw["note"])
    try:
        if kind == "key":
            if isinstance(value, dict):
                value = _fields(value, what, ("name",), ("hold_ms",))
                name, hold = value["name"], value.get("hold_ms")
            else:
                name, hold = value, None
            if not isinstance(name, str) or (hold is not None and not _is_int(hold)):
                raise ScenarioError(f"{what}: a key is a name, or {{\"name\": ..., \"hold_ms\": ...}}")
            step["commands"] = [devlink.key_command(name, hold)]
        elif kind == "tap":
            (x, y), hold = _numbers(value, what, ("x", "y"), "hold_ms")
            step["commands"] = [devlink.tap_command(x, y, hold)]
        elif kind == "swipe":
            (x1, y1, x2, y2), ms = _numbers(value, what, ("x1", "y1", "x2", "y2"), "ms")
            step["commands"] = [devlink.swipe_command(x1, y1, x2, y2, ms)]
        elif kind == "wait":
            value = _fields(value, what, (), ("activity", "timeout_ms"))
            activity = value.get("activity", "")
            if activity != "":
                activity = _one_line(activity, f"{what}: activity")
            wait_ms = value.get("timeout_ms", devlink.DEFAULT_WAIT_MS)
            step["commands"] = [devlink.wait_command(activity, wait_ms)]
            step["activity"] = activity.strip()
            step["wait_ms"] = wait_ms
            # The host gives the reader its time and a little more.
            step["timeout_ms"] = max(timeout_ms, wait_ms + 3000) if "timeout_ms" not in raw else timeout_ms
            if step["timeout_ms"] < wait_ms:
                raise ScenarioError(f"{what}: the step's timeout_ms is shorter than the wait's")
        elif kind == "expect":
            value = _fields(value, what, ("status",))
            status = value["status"]
            if not isinstance(status, dict) or not status:
                raise ScenarioError(f"{what}: expect.status must name at least one field")
            step["checks"] = {field: _expectation(field, rule) for field, rule in status.items()}
            step["commands"] = ["STATUS"]
        elif kind == "shot":
            name = _one_line(value, f"{what}: shot")
            if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,80}\.(png|bmp)", name):
                raise ScenarioError(f"{what}: shot is a file name ending in .png or .bmp, no folders")
            step["file"] = name
            step["commands"] = ["SCREENSHOT", "SHOTCRC"]
        elif kind == "sleep_ms":
            if not _is_int(value) or not 0 <= value <= MAX_SLEEP_MS:
                raise ScenarioError(f"{what}: sleep_ms must be 0 to {MAX_SLEEP_MS}")
            step["sleep_ms"] = value
        elif kind == "cmd":
            step["commands"] = [cmd_text(value)]
    except ScenarioError as error:
        text = str(error)
        raise ScenarioError(text if text.startswith("step ") else f"{what}: {text}") from error
    except ValueError as error:  # devlink's own checks: POWER, an unknown key, a time out of range
        raise ScenarioError(f"{what}: {error}") from error
    return step


def parse_scenario(data) -> dict:
    """Checks a whole scenario before anything is sent. Raises ScenarioError."""
    data = _fields(data, "the scenario", ("name", "steps"), ("requires", "forbid_activity", "timeout_s", "note"))
    name = data["name"]
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", name):
        raise ScenarioError("name must be 1 to 64 letters, digits, dots, dashes or underscores")
    requires = _fields(data.get("requires", {}), "requires", (), ("build", "battery_min"))
    if requires.get("build", REQUIRED_BUILD) != REQUIRED_BUILD:
        raise ScenarioError(f'requires.build must be "{REQUIRED_BUILD}": no other build is driven')
    battery_min = requires.get("battery_min", BATTERY_FLOOR)
    if not _is_int(battery_min) or not BATTERY_FLOOR <= battery_min <= 100:
        raise ScenarioError(f"requires.battery_min must be {BATTERY_FLOOR} to 100 (the floor cannot be lowered)")
    extra = data.get("forbid_activity", [])
    if not isinstance(extra, list):
        raise ScenarioError("forbid_activity must be a list of screen names")
    forbidden = list(FORBIDDEN_ACTIVITIES)
    for activity in extra:
        activity = _one_line(activity, "forbid_activity: a screen name")
        if activity not in forbidden:
            forbidden.append(activity)
    timeout_s = data.get("timeout_s", RUN_TIMEOUT_S)
    if not _is_number(timeout_s) or not 1 <= timeout_s <= MAX_RUN_TIMEOUT_S:
        raise ScenarioError(f"timeout_s must be 1 to {MAX_RUN_TIMEOUT_S}")
    steps = data["steps"]
    if not isinstance(steps, list) or not 1 <= len(steps) <= MAX_STEPS:
        raise ScenarioError(f"steps must be a list of 1 to {MAX_STEPS} steps")
    parsed = [parse_step(step, number) for number, step in enumerate(steps, 1)]
    for step in parsed:
        if step.get("activity") in forbidden:
            raise ScenarioError(f"step {step['number']}: waits for {step['activity']}, a forbidden screen")
    files = [step["file"].lower() for step in parsed if step["kind"] == "shot"]
    if len(files) != len(set(files)):
        raise ScenarioError("two shot steps write the same file")
    return {
        "name": name,
        "battery_min": battery_min,
        "forbidden": forbidden,
        "timeout_s": timeout_s,
        "steps": parsed,
    }


def load_scenario(path: str) -> dict:
    try:
        with open(path, encoding="utf-8") as handle:
            data = json.load(handle)
    except OSError as error:
        raise ScenarioError(f"{path}: {error.strerror}") from error
    except ValueError as error:
        raise ScenarioError(f"{path}: not JSON ({error})") from error
    return parse_scenario(data)


def check_status(checks: dict, status: dict) -> list:
    """The expectations a STATUS reply does not meet, as text. Empty when it meets them all."""
    failed = []
    for field, rules in checks.items():
        if field not in status:
            failed.append(f"{field}: not in STATUS")
            continue
        actual = status[field]
        for op, wanted in rules:
            try:
                good = COMPARISONS[op](actual, wanted)
                # 1 == True in Python; a number is not a flag here.
                if op in ("==", "!=") and isinstance(actual, bool) != isinstance(wanted, bool):
                    good = op == "!="
            except TypeError:
                good = False
            if not good:
                failed.append(f"{field}: {actual!r} is not {op} {wanted!r}")
    return failed


def dry_run_lines(scenario: dict) -> list:
    """What a run would send, a line a command, without a port."""
    lines = [
        f"# {scenario['name']}: {len(scenario['steps'])} steps, {scenario['timeout_s']} s at most",
        f"# before: CMD:STATUS (build must be {REQUIRED_BUILD!r}, battery {scenario['battery_min']}% or more)",
        f"# after every step: CMD:STATUS LITE (stop on {', '.join(scenario['forbidden'])})",
    ]
    for step in scenario["steps"]:
        number = step["number"]
        if step["kind"] == "sleep_ms":
            lines.append(f"{number}: (pause {step['sleep_ms']} ms, nothing sent)")
        elif step["kind"] == "shot":
            lines.append(f"{number}: CMD:SCREENSHOT + CMD:SHOTCRC -> {step['file']}")
        else:
            lines.extend(f"{number}: CMD:{text}" for text in step["commands"])
    return lines


# ---- the run ----


class Runner:
    """Plays a parsed scenario on an open port. `port` is anything devlink.command() can use."""

    def __init__(self, scenario, port, out_dir, clock=time.monotonic, sleep=time.sleep, battery_every=None):
        self.scenario = scenario
        self.port = port
        self.out_dir = out_dir
        self.clock = clock
        self.sleep = sleep
        self.battery_every = battery_every or BATTERY_EVERY_STEPS
        self.started = clock()
        self.log_handle = None

    def log(self, **record):
        record = {"t": round(self.clock() - self.started, 3), **record}
        self.log_handle.write(json.dumps(record, ensure_ascii=False) + "\n")
        self.log_handle.flush()

    def remaining(self, step_deadline=None) -> float:
        """Seconds a command may take now: what is left of the run, and of the step."""
        left = self.started + self.scenario["timeout_s"] - self.clock()
        if step_deadline is not None:
            left = min(left, step_deadline - self.clock())
        if left <= 0:
            raise StepFailed("out of time")
        return left

    def send(self, text, step_deadline=None, timeout=devlink.REPLY_TIMEOUT_S) -> dict:
        return devlink.command(self.port, text, min(timeout, self.remaining(step_deadline)))

    def check_battery(self, status, code):
        percent = status.get("battery_percent")
        if not _is_number(percent) or percent < self.scenario["battery_min"]:
            raise StepFailed(
                f"battery at {percent}%, below {self.scenario['battery_min']}%", diagnose=False, code=code
            )

    def check_activity(self, activity):
        if activity in self.scenario["forbidden"]:
            raise StepFailed(f"reached the forbidden screen {activity}", diagnose=False)

    def play(self, step) -> dict:
        """One step. Returns what goes into its log line; raises StepFailed or LinkError."""
        kind = step["kind"]
        deadline = self.clock() + step["timeout_ms"] / 1000
        if kind == "sleep_ms":
            self.sleep(min(step["sleep_ms"] / 1000, self.remaining(deadline)))
            return {}
        if kind == "shot":
            data, tries = devlink.screenshot_checked(self.port, timeout=self.remaining(deadline))
            path = os.path.join(self.out_dir, step["file"])
            with open(path, "wb") as handle:
                handle.write(devlink.encode_picture(path, data))
            return {"file": step["file"], "bytes": len(data), "tries": tries}
        wait_s = step.get("wait_ms", 0) / 1000
        reply = self.send(step["commands"][0], deadline, wait_s + devlink.REPLY_TIMEOUT_S)
        if kind == "wait" and reply.get("activity") is not None:
            self.check_activity(reply["activity"])
        if not reply.get("ok"):
            raise StepFailed(f"the reader answered: {reply.get('error', reply)}")
        if kind == "expect":
            failed = check_status(step["checks"], reply)
            if failed:
                raise StepFailed("; ".join(failed))
        return {"reply": reply}

    def diagnose(self):
        """After a failure: a picture and a STATUS, as far as the reader still answers."""
        try:
            self.log(event="status", reply=devlink.command(self.port, "STATUS"))
        except devlink.LinkError as error:
            # No answer at all (asleep, USB Drive, unplugged): a picture would not come either.
            self.log(event="status", error=str(error))
            return
        try:
            data, _ = devlink.screenshot_checked(self.port)
            path = os.path.join(self.out_dir, "failure.png")
            with open(path, "wb") as handle:
                handle.write(devlink.encode_picture(path, data))
            self.log(event="shot", file="failure.png")
        except (devlink.LinkError, ValueError, OSError) as error:
            self.log(event="shot", error=str(error))

    def run(self) -> int:
        os.makedirs(self.out_dir, exist_ok=True)
        with open(os.path.join(self.out_dir, "steps.jsonl"), "a", encoding="utf-8") as handle:
            self.log_handle = handle
            code, reason = self._run()
            self.log(event="end", result=("pass", "fail", "not run")[code], exit=code, reason=reason)
        return code

    def _run(self):
        scenario = self.scenario
        try:
            status = devlink.command(self.port, "STATUS")
        except devlink.LinkError as error:
            return EXIT_NOT_RUN, f"no STATUS: {error}"
        self.log(event="start", name=scenario["name"], status=status)
        if not status.get("ok") or status.get("build") != REQUIRED_BUILD:
            return EXIT_NOT_RUN, f"STATUS build is {status.get('build')!r}, not {REQUIRED_BUILD!r}: not driven"
        try:
            self.check_battery(status, EXIT_NOT_RUN)
            self.check_activity(status.get("activity"))
        except StepFailed as failure:
            return EXIT_NOT_RUN, failure.reason
        for step in scenario["steps"]:
            began = self.clock()
            record = {"step": step["number"], "kind": step["kind"], "commands": step["commands"]}
            if "note" in step:
                record["note"] = step["note"]
            try:
                self.remaining()
                record.update(self.play(step))
                # Where the step left the reader. No battery read, no RenderLock: it does not wait
                # for a frame that is being drawn.
                lite = self.send("STATUS LITE")
                record["activity"] = lite.get("activity")
                self.check_activity(lite.get("activity"))
                if step["number"] % self.battery_every == 0:
                    self.check_battery(self.send("STATUS"), EXIT_FAIL)
            except (StepFailed, devlink.LinkError, OSError, ValueError) as error:
                failure = error if isinstance(error, StepFailed) else StepFailed(str(error))
                if self.clock() - self.started >= scenario["timeout_s"]:
                    failure = StepFailed(f"the run took over {scenario['timeout_s']} s ({failure.reason})")
                self.log(**record, ok=False, ms=round((self.clock() - began) * 1000), error=failure.reason)
                if failure.diagnose:
                    self.diagnose()
                return failure.code, f"step {step['number']}: {failure.reason}"
            self.log(**record, ok=True, ms=round((self.clock() - began) * 1000))
        return EXIT_PASS, ""


def run_directory(base: str, name: str, now=None) -> str:
    return os.path.join(base, f"{name}-{time.strftime('%Y%m%d-%H%M%S', time.localtime(now))}")


def default_base() -> str:
    return os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), ".work", "devtest")


def main(argv=None, open_port=devlink.open_port) -> int:
    parser = argparse.ArgumentParser(description="Play a JSON scenario over the VaultInk developer link")
    parser.add_argument("scenario", help="the JSON file")
    parser.add_argument("--port", help="serial port (default: $DEVLINK_PORT, else the first /dev/ttyACM*)")
    parser.add_argument("--dry-run", action="store_true", help="print the commands; open no port, write nothing")
    parser.add_argument("--out", help="where the run folders go (default: .work/devtest)")
    parser.add_argument("--battery-every", type=int, default=BATTERY_EVERY_STEPS, help="steps between battery checks (10)")
    args = parser.parse_args(argv)
    if args.battery_every < 1:
        parser.error("--battery-every must be 1 or more")
    try:
        scenario = load_scenario(args.scenario)
    except ScenarioError as error:
        print(f"devtest: {error}", file=sys.stderr)
        return EXIT_NOT_RUN
    if args.dry_run:
        print("\n".join(dry_run_lines(scenario)))
        return EXIT_PASS
    out_dir = run_directory(args.out or default_base(), scenario["name"])
    try:
        port = open_port(args.port)
    except devlink.LinkError as error:
        print(f"devtest: {error}", file=sys.stderr)
        return EXIT_NOT_RUN
    try:
        code = Runner(scenario, port, out_dir, battery_every=args.battery_every).run()
    except OSError as error:
        print(f"devtest: {error}", file=sys.stderr)
        return EXIT_NOT_RUN
    finally:
        port.close()
    print(f"devtest: {scenario['name']}: {('pass', 'FAIL', 'NOT RUN')[code]}, log in {out_dir}")
    return code


if __name__ == "__main__":
    sys.exit(main())
