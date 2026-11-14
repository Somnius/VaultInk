#!/usr/bin/env python3
"""Tests for scripts/devtest.py: the scenario parser, what it refuses, and whole runs against a
made-up reader (a port object in memory; no serial port, no device)."""

import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))

import devlink  # noqa: E402
import devtest  # noqa: E402

FRAME = bytes(range(256)) * 187 + bytes(128)  # 48000 bytes, an 800x480 panel


def scenario(steps, **extra):
    return {"name": "t", "steps": steps, **extra}


class FakePort:
    """What devlink.command() and devlink.screenshot() need of a port, answered like the firmware
    of the developer's own build. Nothing waits: a command that gets no reply fails at once."""

    def __init__(self, build="dev", battery=80, activities=None, lose_shots=0, wrong_crcs=0, silent=()):
        self.build = build
        self.battery = battery
        self.activities = list(activities or ["Home"])  # the screen after each key, tap or swipe
        self.lose_shots = lose_shots  # this many screenshots come without their last bytes
        self.wrong_crcs = wrong_crcs  # this many SHOTCRC replies do not match
        self.silent = silent  # commands that get no reply
        self.seen = []
        self.pending = b""
        self.closed = False
        self.visit = 1

    @property
    def activity(self):
        return self.activities[0]

    def close(self):
        self.closed = True

    def drain(self):
        self.pending = b""

    def write(self, data):
        for line in data.decode().split("\n"):
            if line.startswith("CMD:"):
                self.seen.append(line[4:])
                self.pending += b"[12] [INF] [MEM] Free: 150000 bytes\n"
                self.answer(line[4:])

    def reply(self, **fields):
        self.pending += json.dumps(fields).encode() + b"\n"

    def answer(self, text):
        name = text.split(" ")[0]
        if name in self.silent:
            return
        if text == "SCREENSHOT":
            data = FRAME
            if self.lose_shots:
                self.lose_shots -= 1
                data = FRAME[:-100]
                self.pending += b"SCREENSHOT_START:%d\n" % len(FRAME) + data
                return
            self.pending += b"SCREENSHOT_START:%d\n" % len(FRAME) + data + b"SCREENSHOT_END\n"
        elif text == "SHOTCRC":
            crc = zlib.crc32(FRAME)
            if self.wrong_crcs:
                self.wrong_crcs -= 1
                crc ^= 1
            self.reply(ok=True, crc32=crc, bytes=len(FRAME), visit=self.visit, renders=7)
        elif text in ("STATUS", "STATUS LITE"):
            fields = {"ok": True, "build": self.build, "activity": self.activity, "heap_free": 150000, "usb": True}
            if text == "STATUS":
                fields["battery_percent"] = self.battery
            self.reply(**fields, visit=self.visit, queued=0)
        elif name in ("KEY", "TAP", "SWIPE"):
            if len(self.activities) > 1:
                self.activities.pop(0)
                self.visit += 1
            self.reply(ok=True, queued=2)
        elif name == "WAIT":
            words = text.split(" ")[1:-1]
            wanted = " ".join(words)
            if wanted and wanted != self.activity:
                self.reply(ok=False, error="timeout", wanted=wanted, activity=self.activity, waited_ms=int(text.split()[-1]))
            else:
                self.reply(ok=True, activity=self.activity, visit=self.visit, renders=7, waited_ms=160)
        elif name == "PING":
            self.reply(ok=True, pong=True)
        else:
            self.reply(ok=False, error="unknown")

    def read_line(self, deadline):
        if b"\n" not in self.pending:
            raise devlink.LinkError("no answer")
        line, _, self.pending = self.pending.partition(b"\n")
        return line

    def read_exact(self, count, deadline):
        if len(self.pending) < count:
            raise devlink.LinkError(f"only {len(self.pending)} of {count} bytes came")
        data, self.pending = self.pending[:count], self.pending[count:]
        return data


class Clock:
    """Time that moves only when told to, or by `step` at every look."""

    def __init__(self, step=0.0):
        self.now = 1000.0
        self.step = step

    def __call__(self):
        self.now += self.step
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class Parser(unittest.TestCase):
    def parse(self, steps, **extra):
        return devtest.parse_scenario(scenario(steps, **extra))

    def commands(self, step):
        return self.parse([step])["steps"][0]["commands"]

    def test_every_kind_of_step(self):
        parsed = self.parse(
            [
                {"wait": {"activity": "Home", "timeout_ms": 8000}},
                {"key": "down"},
                {"key": {"name": "CONFIRM", "hold_ms": 200}},
                {"tap": [240, 400]},
                {"tap": {"x": 1, "y": 2, "hold_ms": 600}},
                {"swipe": [400, 300, 80, 300, 300]},
                {"swipe": {"x1": 1, "y1": 2, "x2": 3, "y2": 4}},
                {"wait": {}},
                {"expect": {"status": {"activity": "Settings", "heap_free": {">": 60000, "<=": 400000}}}},
                {"shot": "settings.png", "note": "the list", "timeout_ms": 30000},
                {"sleep_ms": 500},
                {"cmd": "STATUS LITE"},
                {"cmd": "CMD:LS /debug"},
            ],
            requires={"build": "dev", "battery_min": 40},
            forbid_activity=["WifiSelection", "Sleep"],
            timeout_s=120,
        )
        self.assertEqual(
            [step["commands"] for step in parsed["steps"]],
            [
                ["WAIT Home 8000"],
                ["KEY DOWN"],
                ["KEY CONFIRM 200"],
                ["TAP 240 400"],
                ["TAP 1 2 600"],
                ["SWIPE 400 300 80 300 300"],
                ["SWIPE 1 2 3 4"],
                ["WAIT 5000"],
                ["STATUS"],
                ["SCREENSHOT", "SHOTCRC"],
                [],
                ["STATUS LITE"],
                ["LS /debug"],
            ],
        )
        self.assertEqual(parsed["battery_min"], 40)
        self.assertEqual(parsed["forbidden"], ["UsbDrive", "Sleep", "OtaUpdate", "SdFirmwareUpdate", "WifiSelection"])
        self.assertEqual(parsed["timeout_s"], 120)
        self.assertEqual(parsed["steps"][0]["timeout_ms"], 11000)  # the wait's time and three seconds
        self.assertEqual(parsed["steps"][9]["timeout_ms"], 30000)
        self.assertEqual(parsed["steps"][9]["note"], "the list")

    def test_defaults(self):
        parsed = self.parse([{"key": "BACK"}])
        self.assertEqual(parsed["battery_min"], 30)
        self.assertEqual(parsed["forbidden"], list(devtest.FORBIDDEN_ACTIVITIES))
        self.assertEqual(parsed["timeout_s"], devtest.RUN_TIMEOUT_S)
        self.assertEqual(parsed["steps"][0]["timeout_ms"], devtest.STEP_TIMEOUT_MS)

    def test_what_is_never_sent(self):
        refused = [
            {"key": "POWER"},
            {"key": "power"},
            {"key": {"name": "POWER", "hold_ms": 3000}},
            {"cmd": "REBOOT"},
            {"cmd": "reboot"},
            {"cmd": "CMD:REBOOT"},
            {"cmd": " CMD: BOOTLOADER"},
            {"cmd": "BOOTLOADER"},
            {"cmd": "FLASH /firmware.bin"},
            {"cmd": "PUT /debug/a.txt 0 YQ=="},
            {"cmd": "POWER"},
            {"cmd": "KEY POWER"},
            {"cmd": "KEY CONFIRM"},
            {"cmd": "BENCH 5"},
            {"cmd": "LOG 2"},
            {"cmd": "GET /debug/a 0 10"},
            {"cmd": "NOSUCH"},
            {"cmd": ""},
            {"cmd": "PING extra"},
            {"cmd": "STATUS FULL"},
            {"cmd": "LS debug"},
            {"cmd": "LS /debug/../books"},
            # A second command hidden behind a line end.
            {"cmd": "STATUS\nCMD:REBOOT"},
            {"cmd": "STATUS\rCMD:REBOOT"},
            {"wait": {"activity": "Home\nCMD:REBOOT"}},
            {"wait": {"activity": "Home 100\nCMD:FLASH /x.bin", "timeout_ms": 500}},
            {"key": "CONFIRM\nCMD:REBOOT"},
            {"key": "CONFIRM 100\nCMD:REBOOT"},
        ]
        for step in refused:
            with self.assertRaises(devtest.ScenarioError, msg=step):
                self.parse([step])
        for name in ("POWER", "REBOOT", "BOOTLOADER", "FLASH", "PUT"):
            self.assertIn(name, devtest.REFUSED_COMMANDS)
            self.assertNotIn(name, devtest.READ_ONLY_COMMANDS)
        with self.assertRaisesRegex(devtest.ScenarioError, "step 2: .*POWER"):
            self.parse([{"key": "BACK"}, {"key": "POWER"}])

    def test_steps_that_are_wrong(self):
        wrong = [
            {},
            {"key": "BACK", "tap": [1, 2]},
            {"press": "BACK"},
            {"key": "BACK", "extra": 1},
            {"key": "ENTER"},
            {"key": 5},
            {"key": {"name": "BACK", "hold_ms": 6000}},
            {"key": {"name": "BACK", "ms": 100}},
            {"tap": [1]},
            {"tap": [1, 2, 3, 4]},
            {"tap": [1.5, 2]},
            {"tap": [-1, 2]},
            {"tap": {"x": 1}},
            {"swipe": [1, 2, 3]},
            {"swipe": [1, 2, 3, 4, 10]},
            {"wait": "Home"},
            {"wait": {"activity": 5}},
            {"wait": {"timeout_ms": 50}},
            {"wait": {"timeout_ms": 60001}},
            {"wait": {"activity": "a" * 32}},
            {"wait": {"timeout_ms": 9000}, "timeout_ms": 5000},
            {"wait": {"activity": "Sleep"}},
            {"expect": {}},
            {"expect": {"status": {}}},
            {"expect": {"status": {"heap_free": {"~": 5}}}},
            {"expect": {"status": {"heap_free": {">": "5"}}}},
            {"expect": {"status": {"heap_free": {">": True}}}},
            {"expect": {"status": {"heap_free": {}}}},
            {"expect": {"status": {"heap_free": None}}},
            {"expect": {"activity": "Home"}},
            {"shot": "a.jpg"},
            {"shot": "../a.png"},
            {"shot": "/tmp/a.png"},
            {"shot": "dir/a.png"},
            {"shot": ".png"},
            {"shot": 5},
            {"sleep_ms": -1},
            {"sleep_ms": 10001},
            {"sleep_ms": "5"},
            {"key": "BACK", "timeout_ms": 50},
            {"key": "BACK", "timeout_ms": 120001},
            "KEY BACK",
        ]
        for step in wrong:
            with self.assertRaises(devtest.ScenarioError, msg=step):
                self.parse([step])

    def test_scenarios_that_are_wrong(self):
        good = [{"key": "BACK"}]
        wrong = [
            {"steps": good},
            {"name": "t"},
            {"name": "", "steps": good},
            {"name": "../x", "steps": good},
            {"name": "a b", "steps": good},
            {"name": "t", "steps": []},
            {"name": "t", "steps": {}},
            {"name": "t", "steps": good * 501},
            {"name": "t", "steps": good, "extra": 1},
            {"name": "t", "steps": good, "requires": {"build": "dev-user"}},
            {"name": "t", "steps": good, "requires": {"build": "release"}},
            {"name": "t", "steps": good, "requires": {"battery_min": 29}},
            {"name": "t", "steps": good, "requires": {"battery_min": 0}},
            {"name": "t", "steps": good, "requires": {"battery_min": 101}},
            {"name": "t", "steps": good, "requires": {"battery_min": "40"}},
            {"name": "t", "steps": good, "requires": {"usb": True}},
            {"name": "t", "steps": good, "forbid_activity": "Sleep"},
            {"name": "t", "steps": good, "forbid_activity": ["a\nb"]},
            {"name": "t", "steps": good, "timeout_s": 0},
            {"name": "t", "steps": good, "timeout_s": 3601},
            {"name": "t", "steps": [{"shot": "a.png"}, {"shot": "A.png"}]},
            {"name": "t", "steps": [{"wait": {"activity": "Dice"}}], "forbid_activity": ["Dice"]},
            [],
        ]
        for data in wrong:
            with self.assertRaises(devtest.ScenarioError, msg=data):
                devtest.parse_scenario(data)

    def test_the_forbidden_list_only_grows(self):
        parsed = self.parse([{"key": "BACK"}], forbid_activity=[])
        self.assertEqual(parsed["forbidden"], list(devtest.FORBIDDEN_ACTIVITIES))
        for name in ("UsbDrive", "Sleep", "OtaUpdate", "SdFirmwareUpdate"):
            self.assertIn(name, parsed["forbidden"])

    def test_check_status(self):
        status = {"activity": "Home", "heap_free": 150000, "usb": True, "queued": 0}

        def failed(rules):
            checks = self.parse([{"expect": {"status": rules}}])["steps"][0]["checks"]
            return devtest.check_status(checks, status)

        self.assertEqual(failed({"activity": "Home", "heap_free": {">": 100000, "<": 200000}, "usb": True}), [])
        self.assertEqual(failed({"queued": 0, "heap_free": {">=": 150000, "<=": 150000, "!=": 1}}), [])
        self.assertEqual(len(failed({"activity": "Settings"})), 1)
        self.assertEqual(len(failed({"heap_free": {">": 150000}})), 1)
        self.assertEqual(len(failed({"heap_free": {">": 1, "<": 2}})), 1)
        self.assertEqual(failed({"missing": 1}), ["missing: not in STATUS"])
        # Text is not above a number, and true is not 1.
        self.assertEqual(len(failed({"activity": {">": 5}})), 1)
        self.assertEqual(len(failed({"usb": 1})), 1)
        self.assertEqual(len(failed({"queued": False})), 1)
        self.assertEqual(failed({"usb": {"!=": 1}}), [])

    def test_load_scenario(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "s.json")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write("{not json")
            with self.assertRaises(devtest.ScenarioError):
                devtest.load_scenario(path)
            with self.assertRaises(devtest.ScenarioError):
                devtest.load_scenario(os.path.join(folder, "none.json"))
            with open(path, "w", encoding="utf-8") as handle:
                json.dump(scenario([{"key": "BACK"}]), handle)
            self.assertEqual(devtest.load_scenario(path)["steps"][0]["commands"], ["KEY BACK"])


class Run(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.out = os.path.join(self.folder.name, "run")

    def run_steps(self, port, steps, clock=None, **extra):
        clock = clock or Clock()
        options = {key: extra.pop(key) for key in ("battery_every",) if key in extra}
        parsed = devtest.parse_scenario(scenario(steps, **extra))
        code = devtest.Runner(parsed, port, self.out, clock=clock, sleep=clock.sleep, **options).run()
        with open(os.path.join(self.out, "steps.jsonl"), encoding="utf-8") as handle:
            return code, [json.loads(line) for line in handle]

    def test_a_run_that_passes(self):
        port = FakePort(activities=["Home", "Settings"])
        code, log = self.run_steps(
            port,
            [
                {"wait": {"activity": "Home"}},
                {"key": "CONFIRM"},
                {"wait": {"activity": "Settings", "timeout_ms": 3000}},
                {"expect": {"status": {"activity": "Settings", "heap_free": {">": 60000}}}},
                {"shot": "settings.png"},
                {"sleep_ms": 200},
                {"cmd": "PING"},
            ],
        )
        self.assertEqual(code, 0)
        self.assertEqual(
            port.seen,
            [
                "STATUS",
                "WAIT Home 5000",
                "STATUS LITE",
                "KEY CONFIRM",
                "STATUS LITE",
                "WAIT Settings 3000",
                "STATUS LITE",
                "STATUS",
                "STATUS LITE",
                "SCREENSHOT",
                "SHOTCRC",
                "STATUS LITE",
                "STATUS LITE",
                "PING",
                "STATUS LITE",
            ],
        )
        self.assertEqual(log[0]["event"], "start")
        self.assertEqual(log[0]["status"]["build"], "dev")
        steps = [line for line in log if "step" in line]
        self.assertEqual([line["step"] for line in steps], [1, 2, 3, 4, 5, 6, 7])
        self.assertTrue(all(line["ok"] for line in steps))
        self.assertEqual(steps[1]["commands"], ["KEY CONFIRM"])
        self.assertEqual(steps[1]["activity"], "Settings")
        self.assertEqual(steps[2]["reply"]["waited_ms"], 160)
        self.assertEqual(steps[4]["tries"], 1)
        self.assertEqual(log[-1], {"t": log[-1]["t"], "event": "end", "result": "pass", "exit": 0, "reason": ""})
        with open(os.path.join(self.out, "settings.png"), "rb") as handle:
            self.assertEqual(handle.read(), devlink.encode_picture("x.png", FRAME))

    def test_only_the_developers_own_build_is_driven(self):
        for build in ("dev-user", None):
            port = FakePort(build=build)
            code, log = self.run_steps(port, [{"key": "CONFIRM"}])
            self.assertEqual(code, 2)
            self.assertEqual(port.seen, ["STATUS"])
            self.assertEqual(log[-1]["result"], "not run")
        # A build that answers nothing (a release build).
        port = FakePort(silent=("STATUS",))
        self.assertEqual(self.run_steps(port, [{"key": "CONFIRM"}])[0], 2)
        self.assertEqual(port.seen, ["STATUS"])

    def test_the_battery_floor_before_the_run(self):
        port = FakePort(battery=29)
        code, log = self.run_steps(port, [{"key": "CONFIRM"}])
        self.assertEqual(code, 2)
        self.assertEqual(port.seen, ["STATUS"])
        self.assertIn("battery", log[-1]["reason"])
        self.assertEqual(self.run_steps(FakePort(battery=30), [{"key": "CONFIRM"}])[0], 0)
        port = FakePort(battery=45)
        self.assertEqual(self.run_steps(port, [{"key": "CONFIRM"}], requires={"battery_min": 50})[0], 2)
        self.assertEqual(port.seen, ["STATUS"])

    def test_the_battery_is_checked_every_few_steps(self):
        port = FakePort(battery=32)
        original = port.answer

        def answer(text):
            if text == "KEY DOWN":
                port.battery -= 1
            original(text)

        port.answer = answer
        code, log = self.run_steps(port, [{"key": "DOWN"}] * 6, battery_every=2)
        self.assertEqual(code, 1)
        # Checked after step 2 (30%: still good) and after step 4 (28%): steps 5 and 6 are not played.
        self.assertEqual(port.seen.count("KEY DOWN"), 4)
        self.assertEqual(port.seen.count("STATUS"), 3)
        self.assertNotIn("SCREENSHOT", port.seen)  # the reader is left alone
        self.assertIn("battery", log[-1]["reason"])

    def test_a_forbidden_screen_stops_the_run(self):
        for screen in ("UsbDrive", "Sleep", "OtaUpdate", "SdFirmwareUpdate"):
            port = FakePort(activities=["Home", screen])
            code, log = self.run_steps(port, [{"key": "CONFIRM"}, {"key": "CONFIRM"}, {"shot": "a.png"}])
            self.assertEqual(code, 1, screen)
            self.assertEqual(port.seen, ["STATUS", "KEY CONFIRM", "STATUS LITE"])
            self.assertIn(screen, log[-1]["reason"])
            self.out += "x"
        # One the scenario named, seen in a WAIT reply.
        port = FakePort(activities=["Home", "Dice"])
        code, _ = self.run_steps(port, [{"key": "CONFIRM"}, {"wait": {}}], forbid_activity=["Dice"])
        self.assertEqual(code, 1)
        self.out += "x"
        # Already there before the first step: nothing is pressed.
        port = FakePort(activities=["OtaUpdate"])
        self.assertEqual(self.run_steps(port, [{"key": "BACK"}])[0], 2)
        self.assertEqual(port.seen, ["STATUS"])

    def test_a_wait_that_times_out_takes_a_picture_and_a_status(self):
        port = FakePort()
        code, log = self.run_steps(port, [{"wait": {"activity": "Settings", "timeout_ms": 500}}, {"key": "BACK"}])
        self.assertEqual(code, 1)
        self.assertEqual(port.seen, ["STATUS", "WAIT Settings 500", "STATUS", "SCREENSHOT", "SHOTCRC"])
        self.assertEqual(log[1]["ok"], False)
        self.assertIn("timeout", log[1]["error"])
        self.assertEqual(log[2]["event"], "status")
        self.assertEqual(log[3], {"t": log[3]["t"], "event": "shot", "file": "failure.png"})
        self.assertTrue(os.path.exists(os.path.join(self.out, "failure.png")))
        self.assertEqual(log[-1]["reason"][:7], "step 1:")

    def test_a_step_with_no_answer(self):
        port = FakePort(silent=("WAIT",))
        code, log = self.run_steps(port, [{"wait": {}}, {"key": "BACK"}])
        self.assertEqual(code, 1)
        self.assertNotIn("KEY BACK", port.seen)
        self.assertIn("SCREENSHOT", port.seen)
        self.assertEqual(log[-1]["result"], "fail")

    def test_a_reader_that_went_away_is_not_asked_for_a_picture(self):
        port = FakePort()
        original = port.answer

        def answer(text):
            if "KEY CONFIRM" in port.seen[:-1]:
                return  # asleep after the key: nothing answers any more
            original(text)

        port.answer = answer
        code, log = self.run_steps(port, [{"key": "CONFIRM"}, {"key": "BACK"}])
        self.assertEqual(code, 1)
        self.assertEqual(port.seen, ["STATUS", "KEY CONFIRM", "STATUS LITE", "STATUS"])
        self.assertIn("error", log[-2])

    def test_an_expectation_that_fails(self):
        port = FakePort()
        code, log = self.run_steps(port, [{"expect": {"status": {"heap_free": {">": 200000}}}}, {"key": "BACK"}])
        self.assertEqual(code, 1)
        self.assertIn("heap_free", log[1]["error"])
        self.assertNotIn("KEY BACK", port.seen)

    def test_the_whole_run_has_a_time_limit(self):
        clock = Clock()
        port = FakePort()
        code, log = self.run_steps(port, [{"sleep_ms": 9000}] * 5, clock=clock, timeout_s=20)
        self.assertEqual(code, 1)
        steps = [line for line in log if "step" in line]
        self.assertEqual([line["ok"] for line in steps], [True, True, False])
        self.assertIn("over 20 s", log[-1]["reason"])
        self.assertIn("SCREENSHOT", port.seen)

    def test_a_step_has_a_time_limit(self):
        clock = Clock(step=0.3)  # every look at the clock costs 0.3 s
        port = FakePort()
        code, log = self.run_steps(port, [{"key": "BACK", "timeout_ms": 100}], clock=clock)
        self.assertEqual(code, 1)
        self.assertNotIn("KEY BACK", port.seen)
        self.assertIn("out of time", log[1]["error"])

    def test_a_screenshot_is_taken_again(self):
        port = FakePort(lose_shots=1, wrong_crcs=1)
        code, log = self.run_steps(port, [{"shot": "a.bmp"}])
        self.assertEqual(code, 0)
        self.assertEqual(port.seen.count("SCREENSHOT"), 3)
        self.assertEqual(log[1]["tries"], 3)
        with open(os.path.join(self.out, "a.bmp"), "rb") as handle:
            self.assertEqual(handle.read(), devlink.encode_picture("a.bmp", FRAME))

    def test_a_screenshot_that_never_matches_fails_the_step(self):
        port = FakePort(wrong_crcs=99)
        code, log = self.run_steps(port, [{"shot": "a.png"}])
        self.assertEqual(code, 1)
        self.assertFalse(os.path.exists(os.path.join(self.out, "a.png")))
        self.assertFalse(os.path.exists(os.path.join(self.out, "failure.png")))
        self.assertIn("3 tries", log[1]["error"])

    def test_an_error_reply_fails_the_step(self):
        port = FakePort()
        original = port.answer
        port.answer = lambda text: port.reply(ok=False, error="queue full") if text.startswith("TAP") else original(text)
        code, log = self.run_steps(port, [{"tap": [10, 10]}, {"key": "BACK"}])
        self.assertEqual(code, 1)
        self.assertIn("queue full", log[1]["error"])


class Main(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)

    def write(self, data):
        path = os.path.join(self.folder.name, "s.json")
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(data, handle)
        return path

    def main(self, *argv, port=None):
        opened = []

        def open_port(path):
            opened.append(path)
            if port is None:
                raise devlink.LinkError("no serial port")
            return port

        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = devtest.main(list(argv), open_port=open_port)
        return code, out.getvalue(), err.getvalue(), opened

    def test_dry_run_opens_no_port_and_writes_nothing(self):
        path = self.write(scenario([{"key": "CONFIRM"}, {"wait": {"activity": "Home"}}, {"shot": "a.png"}, {"sleep_ms": 5}]))
        base = os.path.join(self.folder.name, "out")
        code, out, _, opened = self.main(path, "--dry-run", "--out", base)
        self.assertEqual(code, 0)
        self.assertEqual(opened, [])
        self.assertFalse(os.path.exists(base))
        self.assertIn("1: CMD:KEY CONFIRM\n", out)
        self.assertIn("2: CMD:WAIT Home 5000\n", out)
        self.assertIn("3: CMD:SCREENSHOT + CMD:SHOTCRC -> a.png\n", out)
        self.assertIn("4: (pause 5 ms", out)
        self.assertIn("UsbDrive", out)

    def test_a_refused_scenario_opens_no_port(self):
        for step in ({"key": "POWER"}, {"cmd": "REBOOT"}, {"cmd": "FLASH /a.bin"}):
            for flags in ((), ("--dry-run",)):
                code, _, err, opened = self.main(self.write(scenario([step])), *flags, port=FakePort())
                self.assertEqual(code, 2)
                self.assertEqual(opened, [])
                self.assertIn("not allowed", err)

    def test_exit_codes_and_the_run_folder(self):
        base = os.path.join(self.folder.name, "out")
        path = self.write(scenario([{"key": "CONFIRM"}]))
        self.assertEqual(self.main(path, "--out", base)[0], 2)  # no port
        self.assertFalse(os.path.exists(base))
        port = FakePort()
        code, out, _, opened = self.main(path, "--out", base, "--port", "/dev/none", port=port)
        self.assertEqual((code, opened), (0, ["/dev/none"]))
        self.assertTrue(port.closed)
        self.assertIn("pass", out)
        (run,) = os.listdir(base)
        self.assertRegex(run, r"^t-\d{8}-\d{6}$")
        self.assertTrue(os.path.exists(os.path.join(base, run, "steps.jsonl")))
        failing = self.write(scenario([{"expect": {"status": {"activity": "Nowhere"}}}]))
        port = FakePort()
        self.assertEqual(self.main(failing, "--out", os.path.join(base, "f"), port=port)[0], 1)
        self.assertTrue(port.closed)
        self.assertEqual(self.main(path, "--out", os.path.join(base, "u"), port=FakePort(build="dev-user"))[0], 2)

    def test_the_default_folder_is_in_dot_work(self):
        self.assertTrue(devtest.default_base().endswith(os.path.join(".work", "devtest")))
        self.assertEqual(os.path.basename(devtest.run_directory("/x", "name", 0))[:5], "name-")


class LinkAdditions(unittest.TestCase):
    """What this slice added to scripts/devlink.py."""

    def test_wait_command(self):
        self.assertEqual(devlink.wait_command(), "WAIT 5000")
        self.assertEqual(devlink.wait_command("Home", 100), "WAIT Home 100")
        self.assertEqual(devlink.wait_command(" Two Words ", 60000), "WAIT Two Words 60000")
        for activity, ms in (("", 99), ("", 60001), ("a" * 32, 500), ("a\nb", 500), ("", 1.5), ("", True), (5, 500)):
            with self.assertRaises(ValueError):
                devlink.wait_command(activity, ms)

    def test_wait_and_checked_screenshot(self):
        port = FakePort()
        self.assertTrue(devlink.wait(port, "Home")["ok"])
        self.assertEqual(devlink.wait(port, "Dice", 300)["error"], "timeout")
        self.assertEqual(devlink.screenshot_checked(port), (FRAME, 1))
        self.assertEqual(devlink.screenshot_checked(FakePort(wrong_crcs=2)), (FRAME, 3))
        with self.assertRaises(devlink.LinkError):
            devlink.screenshot_checked(FakePort(lose_shots=3))
        # A build without SHOTCRC is told so at once, not asked three times.
        port = FakePort()
        port.answer = lambda text, original=port.answer: (
            port.reply(ok=False, error="unknown") if text == "SHOTCRC" else original(text)
        )
        with self.assertRaisesRegex(devlink.LinkError, "SHOTCRC refused"):
            devlink.screenshot_checked(port)
        self.assertEqual(port.seen.count("SCREENSHOT"), 1)


if __name__ == "__main__":
    unittest.main()
