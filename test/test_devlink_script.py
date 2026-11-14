#!/usr/bin/env python3
"""Tests for scripts/devlink.py against a fake device on a pseudo-terminal.

    python3 test/test_devlink_script.py
"""

import base64
import contextlib
import csv
import hashlib
import io
import json
import os
import pty
import random
import socket
import struct
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import zlib

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))

import devlink  # noqa: E402

STATUS = {
    "ok": True,
    "version": "0.74.0-x4pro",
    "uptime_ms": 123456,
    "heap_free": 150000,
    "battery_mv": 4012,
    "battery_percent": 83,
    "usb": True,
    "charging": False,
    "cpu_mhz": 80,
    "light_on": True,
    "light_brightness": 40,
    "activity": 'Reader "x"',
    "idle_ms": 4000,
}


class FakeDevice:
    """Answers like the firmware on the master side of a pty; `path` is what the script opens."""

    def __init__(
        self,
        framebuffer=b"",
        dev_build=True,
        lose_bytes=0,
        end_marker=True,
        files=None,
        cut_gets=0,
        status=None,
        web=None,
        new_version=None,
    ):
        self.master, slave = pty.openpty()
        self.path = os.ttyname(slave)
        self.slave = slave  # kept open so the master never sees a hang-up between two opens
        self.framebuffer = framebuffer
        self.dev_build = dev_build
        self.lose_bytes = lose_bytes
        self.end_marker = end_marker
        self.files = files  # a made-up SD card: path -> bytes; None for a device without the SD commands
        self.cut_gets = cut_gets  # this many GET replies are cut short by a log line
        self.status = dict(STATUS, **status) if status else STATUS
        self.web = web if web is not None else {}  # what FETCH can reach: url -> bytes
        self.new_version = new_version  # what STATUS says after a FLASH and a REBOOT
        self.flashed = False
        self.queued = 0
        self.seen = []
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def close(self):
        os.close(self.slave)
        os.close(self.master)
        self.thread.join(timeout=2)

    def send(self, data):
        while data:
            data = data[os.write(self.master, data) :]

    def run(self):
        pending = b""
        try:
            while True:
                data = os.read(self.master, 4096)
                if not data:
                    return
                pending += data
                while b"\n" in pending:
                    line, _, pending = pending.partition(b"\n")
                    self.answer(line.decode().strip())
        except OSError:
            return

    def answer(self, line):
        if not line.startswith("CMD:"):
            return  # the real reader ignores a line that is not a command, such as the bare newline the host sends first
        self.seen.append(line)
        # What a real port is full of: log lines, one of them with braces.
        self.send(b"[1200] [INF] [MEM] Free: 150000 bytes\n[1201] [DBG] [X] {not json}\n")
        if line == "CMD:SCREENSHOT":
            data = self.framebuffer[: len(self.framebuffer) - self.lose_bytes]
            self.send(b"SCREENSHOT_START:%d\n" % len(self.framebuffer))
            self.send(data)
            if self.end_marker:
                self.send(b"SCREENSHOT_END\n")
            return
        if not self.dev_build:
            return
        if line == "CMD:STATUS":
            reply = self.status
        elif line == "CMD:REBOOT":
            reply = {"ok": True, "action": "reboot"}
            if self.flashed and self.new_version:
                self.status = dict(self.status, version=self.new_version, uptime_ms=900)
        elif line == "CMD:PING":
            reply = {"ok": True, "pong": True}
        elif line.startswith("CMD:LOG "):
            reply = {"ok": True, "level": int(line.split()[1])}
        elif line.startswith(("CMD:KEY ", "CMD:TAP ", "CMD:SWIPE ")):
            self.queued += 2
            reply = {"ok": True, "queued": self.queued}
        elif self.files is not None and line.startswith(
            ("CMD:LS ", "CMD:GET ", "CMD:PUT ", "CMD:FLASH ", "CMD:FETCH ")
        ):
            reply = self.answer_sd(line)
            if reply is None:
                return
        else:
            reply = {"ok": False, "error": "unknown"}
        self.send(json.dumps(reply).encode() + b"\n")


    def answer_sd(self, line):
        """The framing of the SD commands. Which paths are allowed is the firmware's rule, tested in
        test/dev_link; here everything outside /debug, /screenshots and /.vaultink is refused."""
        refused = {"ok": False, "error": "path refused (docs/devlink.md, Security)"}
        name, _, rest = line[4:].partition(" ")
        if name == "LS":
            folder = rest.rstrip("/")
            names = sorted(path for path in self.files if path.startswith(folder + "/"))
            if not names:
                return refused
            for path in names:
                entry = {"name": path[len(folder) + 1 :], "dir": False, "size": len(self.files[path])}
                self.send(json.dumps(entry).encode() + b"\n")
            return {"ok": True, "count": len(names), "truncated": False}
        if name == "FETCH":
            url, rest = rest.split(" ", 1)
            path, wanted = rest.rsplit(" ", 1)
            if url not in self.web:
                return {"ok": False, "error": "cannot connect to the server"}
            data = self.web[url]
            digest = hashlib.sha256(data).hexdigest()
            self.send(json.dumps({"fetch": "started", "total": len(data)}).encode() + b"\n")
            self.send(b"[1300] [INF] [DEVLINK] CMD:FETCH\n")
            step = {"fetch": "writing", "written": len(data), "total": len(data), "percent": 100}
            self.send(json.dumps(step).encode() + b"\n")
            if digest != wanted:
                return {
                    "ok": False,
                    "error": "sha256 mismatch: the file was deleted",
                    "bytes": len(data),
                    "sha256": digest,
                    "deleted": True,
                }
            self.files[path] = data
            return {"ok": True, "bytes": len(data), "sha256": digest, "path": path, "ms": 4200}
        if name == "FLASH":
            if rest not in self.files:
                return {"ok": False, "error": "cannot open the file"}
            self.flashed = True
            size = len(self.files[rest])
            self.send(json.dumps({"flash": "checked", "bytes": size, "slot": "app1"}).encode() + b"\n")
            self.send(b"[1300] [INF] [FW] erasing\n")
            step = {"flash": "writing", "written": size, "total": size, "percent": 100}
            self.send(json.dumps(step).encode() + b"\n")
            return {"ok": True, "action": "flash", "slot": "app1", "bytes": size, "rebooted": False}
        path, offset, last = rest.rsplit(" ", 2)
        offset = int(offset)
        if not path.startswith(("/debug/", "/screenshots/", "/.vaultink/")):
            return refused
        if name == "GET":
            if path not in self.files:
                return {"ok": False, "error": "no such file"}
            data = self.files[path]
            part = data[offset : offset + min(int(last), 2880)]
            if self.cut_gets > 0:
                self.cut_gets -= 1
                self.send(b'{"offset":%d,"size":%d,"data":"QUJD[1300] [INF] [MEM] Free\n' % (offset, len(data)))
                return None
            return {
                "offset": offset,
                "size": len(data),
                "data": base64.b64encode(part).decode(),
                "len": len(part),
                "eof": offset + len(part) >= len(data),
                "ok": True,
            }
        current = b"" if offset == 0 else self.files.get(path, b"")
        if offset != len(current):
            return {"ok": False, "error": "the offset must be 0 or the size of the file", "size": len(current)}
        part = base64.b64decode(last, validate=True)
        if not path.startswith("/debug/") or len(part) > 180:
            return refused
        self.files[path] = current + part
        return {"ok": True, "written": len(part), "size": len(self.files[path])}


def reference_bmp_rows(framebuffer, width, height):
    """ScreenshotUtil::saveFramebufferAsBmp's loop, line for line: BMP rows, bottom row first."""
    phy_width, phy_height = height, width
    padded = (phy_width + 31) // 32 * 4
    rows = []
    for out_y in range(phy_height):
        row = bytearray(padded)
        for out_x in range(phy_width):
            src_x = width - 1 - out_y
            src_y = phy_width - 1 - out_x
            pixel = (framebuffer[src_y * (width // 8) + (src_x // 8)] >> (7 - (src_x % 8))) & 1
            row[out_x // 8] |= pixel << (7 - (out_x % 8))
        rows.append(bytes(row))
    return rows


class Parsing(unittest.TestCase):
    def test_reply_lines(self):
        self.assertEqual(devlink.parse_reply(b'{"ok":true,"pong":true}\r\n'), {"ok": True, "pong": True})
        self.assertEqual(devlink.parse_reply('  {"ok":false,"error":"unknown"}'), {"ok": False, "error": "unknown"})

    def test_lines_that_are_not_replies(self):
        for line in (
            b"",
            b"[1200] [INF] [MEM] Free: 1 bytes",
            b"{not json}",
            b'{"ok":true',
            b'{"other":1}',
            b'{"ok":"yes"}',
            b"[1,2]",
            b'"ok"',
            b"\xff\xfe{",
        ):
            self.assertIsNone(devlink.parse_reply(line), line)

    def test_screenshot_start(self):
        self.assertEqual(devlink.parse_screenshot_start(b"SCREENSHOT_START:48000\r"), 48000)
        self.assertIsNone(devlink.parse_screenshot_start(b"SCREENSHOT_START:"))
        self.assertIsNone(devlink.parse_screenshot_start(b"SCREENSHOT_START:-1"))
        self.assertIsNone(devlink.parse_screenshot_start(b"SCREENSHOT_END"))
        self.assertIsNone(devlink.parse_screenshot_start(b"x SCREENSHOT_START:5"))

    def test_csv_row(self):
        row = devlink.csv_row(STATUS, 1763049600)
        self.assertEqual(row["link"], "ok")
        self.assertEqual(row["battery_mv"], 4012)
        self.assertEqual(row["light_on"], 1)
        self.assertEqual(row["charging"], 0)
        self.assertEqual(row["activity"], 'Reader "x"')
        self.assertEqual(row["uptime_ms"], 123456)
        self.assertEqual(row["epoch"], 1763049600)
        self.assertEqual(set(row), set(devlink.CSV_COLUMNS))

    def test_csv_row_without_an_answer(self):
        for status in (None, {"ok": False, "error": "unknown"}):
            row = devlink.csv_row(status, 1763049600)
            self.assertEqual(row["link"], "none")
            self.assertEqual(row["battery_mv"], "")


class Picture(unittest.TestCase):
    def test_corners(self):
        # The framebuffer's pixel (0, 479), its bottom-left, is the picture's top-left.
        data = bytearray(48000)
        data[479 * 100] = 0x80
        width, rows = devlink.rotate_framebuffer(bytes(data))
        self.assertEqual((width, len(rows)), (480, 800))
        self.assertEqual(rows[0][0], 0x80)
        self.assertEqual(sum(bin(byte).count("1") for row in rows for byte in row), 1)
        # Its pixel (799, 0), the top-right, is the picture's bottom-right.
        data = bytearray(48000)
        data[99] = 0x01
        _, rows = devlink.rotate_framebuffer(bytes(data))
        self.assertEqual(rows[799][59], 0x01)

    def test_bmp_matches_the_firmware_loop(self):
        data = random.Random(7).randbytes(48000)
        bmp = devlink.encode_picture("x.BMP", data)
        self.assertEqual(bmp[:2], b"BM")
        size, _, _, offset = struct.unpack_from("<IHHI", bmp, 2)
        self.assertEqual(size, len(bmp))
        self.assertEqual(offset, 62)
        info = struct.unpack_from("<IiiHH", bmp, 14)
        self.assertEqual(info, (40, 480, 800, 1, 1))
        self.assertEqual(bmp[54:62], bytes([0, 0, 0, 0, 255, 255, 255, 0]))
        self.assertEqual(bmp[62:], b"".join(reference_bmp_rows(data, 800, 480)))

    def test_png_holds_the_same_pixels(self):
        data = random.Random(8).randbytes(48000)
        png = devlink.encode_picture("x.png", data)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        at, chunks = 8, {}
        while at < len(png):
            (length,) = struct.unpack_from(">I", png, at)
            kind = png[at + 4 : at + 8]
            body = png[at + 8 : at + 8 + length]
            self.assertEqual(struct.unpack_from(">I", png, at + 8 + length)[0], zlib.crc32(kind + body))
            chunks[kind] = body
            at += 12 + length
        self.assertEqual(struct.unpack(">IIBBBBB", chunks[b"IHDR"]), (480, 800, 1, 0, 0, 0, 0))
        raw = zlib.decompress(chunks[b"IDAT"])
        rows = [raw[i * 61 + 1 : (i + 1) * 61] for i in range(800)]
        self.assertTrue(all(raw[i * 61] == 0 for i in range(800)))
        self.assertEqual(rows, [row[:60] for row in reversed(reference_bmp_rows(data, 800, 480))])

    def test_wrong_sizes(self):
        with self.assertRaises(ValueError):
            devlink.rotate_framebuffer(b"\0" * 47999)
        with self.assertRaises(ValueError):
            devlink.encode_picture("x.jpg", b"\0" * 48000)


class Link(unittest.TestCase):
    def setUp(self):
        devlink.REPLY_TIMEOUT_S = 0.6
        self.devices = []

    def tearDown(self):
        for device in self.devices:
            device.close()

    def device(self, **options):
        device = FakeDevice(**options)
        self.devices.append(device)
        return device

    def test_command_skips_the_log_lines(self):
        device = self.device()
        port = devlink.open_port(device.path)
        try:
            self.assertEqual(devlink.command(port, "STATUS"), STATUS)
            self.assertEqual(devlink.command(port, "LOG 2"), {"ok": True, "level": 2})
            self.assertEqual(devlink.command(port, "FLASH x"), {"ok": False, "error": "unknown"})
        finally:
            port.close()
        self.assertEqual(device.seen, ["CMD:STATUS", "CMD:LOG 2", "CMD:FLASH x"])

    def test_a_release_build_does_not_answer(self):
        device = self.device(dev_build=False)
        port = devlink.open_port(device.path)
        try:
            with self.assertRaises(devlink.LinkError):
                devlink.command(port, "STATUS")
        finally:
            port.close()

    def test_screenshot(self):
        data = random.Random(9).randbytes(48000)  # holds newlines and every other byte
        for dev_build in (True, False):
            device = self.device(framebuffer=data, dev_build=dev_build)
            port = devlink.open_port(device.path)
            try:
                self.assertEqual(devlink.screenshot(port), data)
            finally:
                port.close()

    def test_screenshot_with_lost_bytes(self):
        data = random.Random(10).randbytes(48000)
        for options in ({"lose_bytes": 300}, {"end_marker": False}):
            device = self.device(framebuffer=data, **options)
            port = devlink.open_port(device.path)
            try:
                with self.assertRaises(devlink.LinkError):
                    devlink.screenshot(port, timeout=1.5)
            finally:
                port.close()

    def test_no_port(self):
        with self.assertRaises(devlink.LinkError):
            devlink.open_port("/nonexistent/ttyACM9")
        self.assertIsNone(devlink.status_once("/nonexistent/ttyACM9"))

    def test_main_status_and_exit_codes(self):
        device = self.device()
        self.assertEqual(devlink.main(["--port", device.path, "status"]), 0)
        self.assertEqual(devlink.main(["--port", device.path, "cmd", "PING"]), 0)
        self.assertEqual(devlink.main(["--port", device.path, "cmd", "NOPE"]), 1)
        self.assertEqual(devlink.main(["--port", "/nonexistent/ttyACM9", "status"]), 2)

    def test_main_screenshot_writes_the_file(self):
        data = random.Random(11).randbytes(48000)
        device = self.device(framebuffer=data)
        with tempfile.TemporaryDirectory() as folder:
            out = os.path.join(folder, "shot.bmp")
            self.assertEqual(devlink.main(["--port", device.path, "screenshot", out]), 0)
            with open(out, "rb") as handle:
                self.assertEqual(handle.read(), devlink.encode_picture(out, data))

    def test_bench_survives_the_device_going_away_and_coming_back(self):
        device = self.device()
        link = os.path.join(tempfile.mkdtemp(), "ttyACM0")
        os.symlink(device.path, link)
        ticks = {"now": 1000.0, "polls": 0}

        def clock():
            return ticks["now"]

        def sleep(seconds):
            # Poll 2 and 3 find no port (asleep), poll 4 finds a port that does not answer
            # (USB Drive mode or a release build), poll 5 finds the device again.
            ticks["now"] += seconds
            ticks["polls"] += 1
            if ticks["polls"] == 1:
                os.unlink(link)
            elif ticks["polls"] == 3:
                device.dev_build = False
                os.symlink(device.path, link)
            elif ticks["polls"] == 4:
                device.dev_build = True

        with tempfile.TemporaryDirectory() as folder:
            out = os.path.join(folder, "bench.csv")
            rows = devlink.bench(link, every=60, minutes=4, out=out, sleep=sleep, clock=clock)
            self.assertEqual(rows, 5)
            # A second run appends without a second header.
            devlink.bench(link, every=60, minutes=0.5, out=out, sleep=sleep, clock=clock)
            with open(out, newline="", encoding="utf-8") as handle:
                table = list(csv.DictReader(handle))
        self.assertEqual([row["link"] for row in table], ["ok", "none", "none", "none", "ok", "ok"])
        self.assertEqual([row["epoch"] for row in table[:5]], ["1000", "1060", "1120", "1180", "1240"])
        self.assertEqual(table[0]["battery_mv"], "4012")
        self.assertEqual(table[0]["activity"], 'Reader "x"')
        self.assertEqual(table[1]["battery_mv"], "")
        self.assertEqual(table[4]["cpu_mhz"], "80")


BENCH_FILE = """epoch,uptime_ms,battery_mv,battery_percent,usb,light_on,light_brightness,light_warmth,cpu_mhz,heap_free,heap_min,psram_free,activity,wifi,ble,wake_cause
1794571200,1000,4000,80,0,0,0,128,80,150000,90000,6000000,Reader,0,0,0
1794571500,301000,3998,80,0,0,0,128,80,150000,90000,6000000,Reader,0,0,0
1794571800,601000,3994,79,0,0,0,128,80,150000,90000,6000000,Reader,0,0,0
1794572100,901000,3990,79,0,1,40,128,80,150000,90000,6000000,Reader,0,0,0
1794572400,1201000,3980,78,0,1,40,128,80,150000,90000,6000000,Reader,0,0,0
1794572700,1501000,3970,77,0,1,40,128,160,150000,90000,6000000,Home,0,0,0
0,2000,3969,77,0,0,0,128,160,150000,90000,6000000,Home,0,0,0
0,602000,3967,77,1,0,0,128,80,150000,90000,6000000,Home,0,0,0
0,1202000,3999,78,1,0,0,128,80,150000,90000,6000000,Home,0,0,0
"""


class CommandTexts(unittest.TestCase):
    def test_a_line_without_ok_is_not_a_reply(self):
        entry = b'{"name":"a.txt","dir":false,"size":3}'
        self.assertIsNone(devlink.parse_reply(entry))
        self.assertEqual(devlink.parse_json_line(entry), {"name": "a.txt", "dir": False, "size": 3})
        self.assertIsNone(devlink.parse_json_line(b"[1200] [INF] {x}"))
        self.assertIsNone(devlink.parse_json_line(b"[1, 2]"))
        self.assertIsNone(devlink.parse_json_line(b'{"data":"QUJD[1300] [INF] cut'))

    def test_key(self):
        self.assertEqual(devlink.key_command("CONFIRM"), "KEY CONFIRM")
        self.assertEqual(devlink.key_command("back", 1500), "KEY BACK 1500")
        for name in devlink.KEY_NAMES:
            self.assertEqual(devlink.key_command(name), "KEY " + name)
        for name in ("POWER", "power", "HOME", "", "BACK CONFIRM"):
            with self.assertRaises(ValueError):
                devlink.key_command(name)
        for hold in (0, 19, 5001, -1):
            with self.assertRaises(ValueError):
                devlink.key_command("UP", hold)

    def test_tap_and_swipe(self):
        self.assertEqual(devlink.tap_command(240, 400), "TAP 240 400")
        self.assertEqual(devlink.tap_command(0, 0, 700), "TAP 0 0 700")
        self.assertEqual(devlink.swipe_command(400, 300, 80, 300), "SWIPE 400 300 80 300")
        self.assertEqual(devlink.swipe_command(1, 2, 3, 4, 500), "SWIPE 1 2 3 4 500")
        with self.assertRaises(ValueError):
            devlink.tap_command(-1, 5)
        with self.assertRaises(ValueError):
            devlink.tap_command(1, 5, 5)
        with self.assertRaises(ValueError):
            devlink.swipe_command(1, 2, 3, 70000)
        with self.assertRaises(ValueError):
            devlink.swipe_command(1, 2, 3, 4, 6000)

    def test_sd_path(self):
        self.assertEqual(devlink.sd_path("/debug/a b.txt"), "/debug/a b.txt")
        for path in ("debug/a.txt", "", "/debug/../.crosspoint/wifi.json", "/debug/a\nCMD:REBOOT", "./x", "~/x.bin"):
            with self.assertRaises(ValueError):
                devlink.sd_path(path)

    def test_put_commands_fit_the_line_and_join_back(self):
        data = random.Random(3).randbytes(1000)
        greek = "/debug/\u039c\u03b5 \u03ba\u03b5\u03bd\u03cc.txt"
        for path in ("/debug/a.bin", "/debug/" + "d" * 150 + ".bin", greek):
            commands = devlink.put_commands(path, data)
            joined = b""
            for text in commands:
                self.assertLessEqual(len(f"CMD:{text}".encode()), devlink.MAX_COMMAND_BYTES)
                name, rest = text.split(" ", 1)
                got_path, offset, encoded = rest.rsplit(" ", 2)
                self.assertEqual((name, got_path), ("PUT", path))
                self.assertEqual(int(offset), len(joined))
                part = base64.b64decode(encoded, validate=True)
                self.assertTrue(0 < len(part) <= devlink.PUT_CHUNK_BYTES)
                joined += part
            self.assertEqual(joined, data)
        # 174 bytes fit a 256-byte line with this path.
        self.assertEqual(len(devlink.put_commands("/debug/a.bin", data)), 6)
        self.assertEqual(devlink.put_commands("/.vaultink/bench.cfg", b"5\n"), ["PUT /.vaultink/bench.cfg 0 NQo="])

    def test_put_commands_that_cannot_be_sent(self):
        with self.assertRaises(ValueError):
            devlink.put_commands("/debug/empty.txt", b"")
        with self.assertRaises(ValueError):
            devlink.put_commands("/debug/" + "d" * 240, b"abc")
        with self.assertRaises(ValueError):
            devlink.put_commands("debug/a.txt", b"abc")

    def test_get_data_is_checked(self):
        reply = {"ok": True, "offset": 10, "size": 13, "data": "QUJD", "len": 3, "eof": True}
        self.assertEqual(devlink.get_data(reply, 10), b"ABC")
        for change in ({"offset": 0}, {"len": 4}, {"data": "QUJ"}, {"data": "QU JD"}, {"data": "QUJDRA=="}):
            with self.assertRaises(devlink.LinkError):
                devlink.get_data({**reply, **change}, 10)


class NextSlice(unittest.TestCase):
    """KEY, TAP, SWIPE, LS, GET, PUT and FLASH against the fake device."""

    def setUp(self):
        devlink.REPLY_TIMEOUT_S = 0.6
        self.devices = []

    def tearDown(self):
        for device in self.devices:
            device.close()

    def device(self, **options):
        device = FakeDevice(**options)
        self.devices.append(device)
        return device

    def run_main(self, device, *words):
        out = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = devlink.main(["--port", device.path, *words])
        return code, out.getvalue(), err.getvalue()

    def test_key_tap_and_swipe(self):
        device = self.device()
        code, out, _ = self.run_main(device, "key", "CONFIRM")
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(out), {"ok": True, "queued": 2})
        self.assertEqual(self.run_main(device, "key", "back", "--hold", "800")[0], 0)
        self.assertEqual(self.run_main(device, "tap", "240", "400")[0], 0)
        self.assertEqual(self.run_main(device, "tap", "10", "20", "--hold", "600")[0], 0)
        self.assertEqual(self.run_main(device, "swipe", "400", "300", "80", "300", "--ms", "200")[0], 0)
        self.assertEqual(
            device.seen,
            [
                "CMD:KEY CONFIRM",
                "CMD:KEY BACK 800",
                "CMD:TAP 240 400",
                "CMD:TAP 10 20 600",
                "CMD:SWIPE 400 300 80 300 200",
            ],
        )

    def test_the_power_key_is_never_sent(self):
        device = self.device()
        code, _, err = self.run_main(device, "key", "POWER")
        self.assertEqual(code, 2)
        self.assertIn("POWER is not allowed", err)
        self.assertEqual(self.run_main(device, "key", "SLEEP")[0], 2)
        self.assertEqual(self.run_main(device, "tap", "5", "5", "--hold", "1")[0], 2)
        self.assertEqual(device.seen, [])

    def test_ls_prints_the_entries(self):
        device = self.device(files={"/debug/a.txt": b"abc", "/debug/b.log": b"x" * 1200, "/books/c.epub": b"no"})
        code, out, _ = self.run_main(device, "ls", "/debug")
        self.assertEqual(code, 0)
        lines = out.splitlines()
        self.assertEqual([line.split() for line in lines[:2]], [["3", "a.txt"], ["1200", "b.log"]])
        self.assertEqual(json.loads(lines[2]), {"ok": True, "count": 2, "truncated": False})
        code, out, _ = self.run_main(device, "ls", "/Vaults")
        self.assertEqual(code, 1)
        self.assertFalse(json.loads(out)["ok"])

    def test_get_copies_a_file_in_parts(self):
        data = random.Random(5).randbytes(7000)  # three parts of 2880 at most
        device = self.device(files={"/.vaultink/bench.csv": data, "/debug/empty.txt": b""})
        with tempfile.TemporaryDirectory() as folder:
            out = os.path.join(folder, "bench.csv")
            self.assertEqual(self.run_main(device, "get", "/.vaultink/bench.csv", out)[0], 0)
            with open(out, "rb") as handle:
                self.assertEqual(handle.read(), data)
            empty = os.path.join(folder, "empty.txt")
            self.assertEqual(self.run_main(device, "get", "/debug/empty.txt", empty)[0], 0)
            self.assertEqual(os.path.getsize(empty), 0)
        self.assertEqual(
            device.seen[:3],
            [
                "CMD:GET /.vaultink/bench.csv 0 2880",
                "CMD:GET /.vaultink/bench.csv 2880 2880",
                "CMD:GET /.vaultink/bench.csv 5760 2880",
            ],
        )

    def test_get_asks_again_for_a_part_cut_by_a_log_line(self):
        data = random.Random(6).randbytes(3000)
        device = self.device(files={"/debug/log.txt": data}, cut_gets=2)
        port = devlink.open_port(device.path)
        try:
            reply, got = devlink.get_file(port, "/debug/log.txt")
        finally:
            port.close()
        self.assertTrue(reply["ok"])
        self.assertEqual(got, data)
        self.assertEqual(device.seen.count("CMD:GET /debug/log.txt 0 2880"), 3)

    def test_get_gives_up_when_every_try_is_cut(self):
        device = self.device(files={"/debug/log.txt": b"abc"}, cut_gets=9)
        with tempfile.TemporaryDirectory() as folder:
            out = os.path.join(folder, "log.txt")
            self.assertEqual(self.run_main(device, "get", "/debug/log.txt", out)[0], 2)
            self.assertFalse(os.path.exists(out))
        self.assertEqual(len(device.seen), devlink.CHUNK_TRIES)

    def test_get_of_a_refused_path_writes_nothing(self):
        device = self.device(files={"/.crosspoint/wifi.json": b"secret"})
        with tempfile.TemporaryDirectory() as folder:
            out = os.path.join(folder, "wifi.json")
            code, text, _ = self.run_main(device, "get", "/.crosspoint/wifi.json", out)
            self.assertEqual(code, 1)
            self.assertIn("path refused", text)
            self.assertNotIn("secret", text)
            self.assertFalse(os.path.exists(out))
            # A path with ".." is not even sent.
            self.assertEqual(self.run_main(device, "get", "/debug/../.crosspoint/wifi.json", out)[0], 2)
        self.assertEqual(device.seen, ["CMD:GET /.crosspoint/wifi.json 0 2880"])

    def test_put_copies_a_file_in_parts(self):
        data = random.Random(7).randbytes(1000)
        device = self.device(files={})
        with tempfile.TemporaryDirectory() as folder:
            source = os.path.join(folder, "note.bin")
            with open(source, "wb") as handle:
                handle.write(data)
            code, out, _ = self.run_main(device, "put", source, "/debug/note.bin")
            self.assertEqual(code, 0)
            # 171 bytes fit a 256-byte line with this path.
            self.assertEqual(json.loads(out), {"ok": True, "written": 1000 - 5 * 171, "size": 1000})
            self.assertEqual(device.files["/debug/note.bin"], data)
            self.assertEqual(len(device.seen), 6)
            # Refused by the reader: stops at the first part.
            code, out, _ = self.run_main(device, "put", source, "/Vaults/note.bin")
            self.assertEqual(code, 1)
            self.assertEqual(len(device.seen), 7)
            self.assertNotIn("/Vaults/note.bin", device.files)
            # No such local file, and an empty one: nothing is sent.
            self.assertEqual(self.run_main(device, "put", os.path.join(folder, "missing"), "/debug/x")[0], 2)
            empty = os.path.join(folder, "empty")
            open(empty, "wb").close()
            self.assertEqual(self.run_main(device, "put", empty, "/debug/x")[0], 2)
            self.assertEqual(len(device.seen), 7)

    def test_flash_prints_the_progress_and_does_not_reboot(self):
        device = self.device(files={"/firmware.bin": b"\xe9" * 4096})
        code, out, _ = self.run_main(device, "flash", "/firmware.bin")
        self.assertEqual(code, 0)
        lines = out.splitlines()
        self.assertEqual(json.loads(lines[0])["flash"], "checked")
        self.assertEqual(json.loads(lines[1])["percent"], 100)
        self.assertEqual(json.loads(lines[2])["slot"], "app1")
        self.assertIn("cmd REBOOT", lines[3])
        self.assertEqual(device.seen, ["CMD:FLASH /firmware.bin"])  # no REBOOT was sent
        code, out, _ = self.run_main(device, "flash", "/missing.bin")
        self.assertEqual(code, 1)
        self.assertNotIn("REBOOT", out)

    def test_flash_takes_an_sd_path_and_copies_nothing(self):
        device = self.device(files={})
        with tempfile.TemporaryDirectory() as folder:
            local = os.path.join(folder, "firmware.bin")
            with open(local, "wb") as handle:
                handle.write(b"\xe9" * 64)
            code, _, err = self.run_main(device, "flash", os.path.relpath(local))
            self.assertEqual(code, 2)
            self.assertIn("Nothing is copied over the link", err)
        self.assertEqual(self.run_main(device, "flash", "/fw/../firmware.bin")[0], 2)
        self.assertEqual(device.seen, [])


SHA = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"  # sha256(b"test")


class FetchTexts(unittest.TestCase):
    """FETCH and push-update: the parts without a port or a network."""

    def test_private_addresses_and_their_edges(self):
        for address in ("10.0.0.0", "10.255.255.255", "172.16.0.0", "172.31.255.255", "192.168.0.0", "192.168.255.255"):
            self.assertTrue(devlink.private_ipv4(address), address)
        for address in (
            "9.255.255.255",
            "11.0.0.0",
            "172.15.255.255",
            "172.32.0.0",
            "192.167.1.1",
            "192.169.1.1",
            "127.0.0.1",
            "169.254.1.1",
            "0.0.0.0",
            "100.64.0.1",
            "8.8.8.8",
            "192.168.1",
            "192.168.1.2.3",
            "192.168.01.2",
            "192.168.1.256",
            "0xc0.0xa8.1.2",
            "192.168.1.-2",
            "192.168.1. 2",
            "192.168.１.2",
            "",
            "reader.local",
            "::1",
        ):
            self.assertFalse(devlink.private_ipv4(address), address)

    def test_urls_the_reader_would_take(self):
        for url in (
            "http://192.168.1.20:8000/firmware.bin",
            "http://10.0.0.5/fw/x4pro-dev_1.2.3~a.bin",
            "http://172.20.1.1:65535/a",
            "http://192.168.1.2:1/a",
        ):
            self.assertEqual(devlink.fetch_url(url), url)

    def test_urls_that_are_refused_before_anything_is_sent(self):
        for url in (
            "",
            "https://192.168.1.2/a.bin",
            "HTTP://192.168.1.2/a.bin",
            "ftp://192.168.1.2/a.bin",
            "192.168.1.2/a.bin",
            "http://reader.local/a.bin",
            "http://localhost:8000/a.bin",
            "http://user@192.168.1.2/a.bin",
            "http://user:pw@192.168.1.2/a.bin",
            "http://192.168.1.2@8.8.8.8/a.bin",
            "http://192.168.1.2:80@8.8.8.8/a.bin",
            "http://[::1]/a.bin",
            "http://[fe80::1]:8000/a.bin",
            "http://127.0.0.1:8000/a.bin",
            "http://169.254.1.1/a.bin",
            "http://172.15.0.1/a.bin",
            "http://172.32.0.1/a.bin",
            "http://8.8.8.8/a.bin",
            "http://192.168.1.2:0/a.bin",
            "http://192.168.1.2:65536/a.bin",
            "http://192.168.1.2:080/a.bin",
            "http://192.168.1.2:/a.bin",
            "http://192.168.1.2:80a/a.bin",
            "http://192.168.1.2",
            "http://192.168.1.2/",
            "http://192.168.1.2/fw/",
            "http://192.168.1.2/../a.bin",
            "http://192.168.1.2/fw/../a.bin",
            "http://192.168.1.2/%2e%2e/a.bin",
            "http://192.168.1.2//a.bin",
            "http://192.168.1.2/a.bin?x=1",
            "http://192.168.1.2/a.bin#x",
            "http://192.168.1.2/a.bin junk",
            "http://192.168.1.2/a.bin\r\nHost: x",
            "http://192.168.1.2/a b.bin",
            "http://192.168.1.2/" + "a" * 200,
        ):
            with self.assertRaises(ValueError, msg=url):
                devlink.fetch_url(url)

    def test_the_command_text(self):
        self.assertEqual(
            devlink.fetch_command("http://192.168.1.20:8000/firmware.bin", "/firmware-dev.bin", SHA.upper() + "\n"),
            f"FETCH http://192.168.1.20:8000/firmware.bin /firmware-dev.bin {SHA}",
        )
        url = "http://192.168.1.20:8000/firmware.bin"
        for path, digest in (
            ("/firmware.txt", SHA),  # not a .bin
            ("firmware.bin", SHA),  # not absolute
            ("/fw/../firmware.bin", SHA),
            ("/firmware.bin", SHA[:-1]),
            ("/firmware.bin", SHA[:-1] + "g"),
            ("/firmware.bin", ""),
            ("/" + "a" * 150 + ".bin", SHA),  # over the 256-byte line
        ):
            with self.assertRaises(ValueError, msg=(path, digest)):
                devlink.fetch_command(url, path, digest)

    def test_the_file_to_push(self):
        with tempfile.TemporaryDirectory() as folder:
            good = os.path.join(folder, "firmware.bin")
            with open(good, "wb") as handle:
                handle.write(b"test")
            self.assertEqual(devlink.sha256_file(good), (SHA, 4))
            self.assertEqual(devlink.check_push_file(good), (SHA, 4))
            empty = os.path.join(folder, "empty.bin")
            open(empty, "wb").close()
            text = os.path.join(folder, "notes.txt")
            with open(text, "wb") as handle:
                handle.write(b"test")
            for path in (empty, text):
                with self.assertRaises(ValueError):
                    devlink.check_push_file(path)
            with self.assertRaises(OSError):
                devlink.check_push_file(os.path.join(folder, "missing.bin"))
            limit = devlink.FETCH_MAX_BYTES
            devlink.FETCH_MAX_BYTES = 3
            try:
                with self.assertRaisesRegex(ValueError, "app slot"):
                    devlink.check_push_file(good)
            finally:
                devlink.FETCH_MAX_BYTES = limit
        self.assertEqual(devlink.FETCH_MAX_BYTES, 0x7F0000)

    def test_what_status_must_say_before_a_push(self):
        devlink.check_push_status({"ok": True, "build": "dev", "wifi": True})
        with self.assertRaisesRegex(devlink.UpdateError, "x4pro-dev"):
            devlink.check_push_status({"ok": True, "build": "dev-user", "wifi": True})
        with self.assertRaisesRegex(devlink.UpdateError, "x4pro-dev"):
            devlink.check_push_status({"ok": True, "wifi": True})
        with self.assertRaisesRegex(devlink.UpdateError, "Wi-Fi"):
            devlink.check_push_status({"ok": True, "build": "dev", "wifi": False})
        with self.assertRaisesRegex(devlink.UpdateError, "Wi-Fi"):
            devlink.check_push_status({"ok": True, "build": "dev"})
        with self.assertRaisesRegex(devlink.UpdateError, "STATUS failed"):
            devlink.check_push_status({"ok": False, "error": "unknown"})

    def test_the_fetch_reply_must_be_the_file(self):
        devlink.check_fetch_reply({"ok": True, "bytes": 4, "sha256": SHA}, SHA, 4)
        with self.assertRaisesRegex(devlink.UpdateError, "not on Wi-Fi"):
            devlink.check_fetch_reply({"ok": False, "error": "not on Wi-Fi: join a network"}, SHA, 4)
        with self.assertRaisesRegex(devlink.UpdateError, r"url refused \(the port"):
            devlink.check_fetch_reply({"ok": False, "error": "url refused", "reason": "the port must be"}, SHA, 4)
        with self.assertRaises(devlink.UpdateError):
            devlink.check_fetch_reply({"ok": True, "bytes": 5, "sha256": SHA}, SHA, 4)
        with self.assertRaises(devlink.UpdateError):
            devlink.check_fetch_reply({"ok": True, "bytes": 4, "sha256": "0" * 64}, SHA, 4)
        with self.assertRaises(devlink.UpdateError):
            devlink.check_fetch_reply({"ok": True}, SHA, 4)

    def test_a_new_start_is_told_from_the_old_firmware(self):
        new = {"ok": True, "version": "1.0-dev", "uptime_ms": 3000}
        self.assertTrue(devlink.booted_as(new, "1.0-dev", 6.0))
        self.assertTrue(devlink.booted_as(new, "1.0-dev", 1.5))  # two seconds of slack
        self.assertFalse(devlink.booted_as(new, "1.1-dev", 6.0))
        # The same version text, still the run from before the REBOOT.
        self.assertFalse(devlink.booted_as(dict(new, uptime_ms=600000), "1.0-dev", 6.0))
        self.assertFalse(devlink.booted_as(dict(new, ok=False), "1.0-dev", 6.0))
        self.assertFalse(devlink.booted_as({"ok": True, "version": "1.0-dev"}, "1.0-dev", 6.0))
        self.assertFalse(devlink.booted_as(None, "1.0-dev", 6.0))

    def poller(self, answers):
        """A STATUS poll and a clock that moves two seconds a poll."""
        state = {"now": 100.0, "polls": 0}

        def poll(_path):
            state["polls"] += 1
            return answers[min(state["polls"], len(answers)) - 1]

        def sleep(seconds):
            state["now"] += seconds

        return state, poll, sleep, (lambda: state["now"])

    def test_waiting_for_the_version(self):
        new = {"ok": True, "version": "1.0-dev", "uptime_ms": 1500}
        old = {"ok": True, "version": "0.9-dev", "uptime_ms": 900000}
        said = []
        state, poll, sleep, clock = self.poller([old, None, None, new])
        status = devlink.wait_for_version("p", "1.0-dev", 30, 100.0, poll, sleep, clock, said.append)
        self.assertEqual(status, new)
        self.assertEqual(state["polls"], 4)
        self.assertEqual(len(said), 3)

    def test_waiting_gives_up_with_what_it_saw(self):
        quiet = lambda _text: None  # noqa: E731
        state, poll, sleep, clock = self.poller([None])
        with self.assertRaisesRegex(devlink.UpdateError, "did not answer"):
            devlink.wait_for_version("p", "1.0-dev", 10, 100.0, poll, sleep, clock, quiet)
        self.assertEqual(state["polls"], 6)  # at 0, 2, 4, 6, 8 and 10 s
        old = {"ok": True, "version": "0.9-dev", "uptime_ms": 2000}
        _, poll, sleep, clock = self.poller([None, old])
        with self.assertRaisesRegex(devlink.UpdateError, "'0.9-dev', not '1.0-dev'"):
            devlink.wait_for_version("p", "1.0-dev", 10, 100.0, poll, sleep, clock, quiet)
        same = {"ok": True, "version": "1.0-dev", "uptime_ms": 900000}
        _, poll, sleep, clock = self.poller([same])
        with self.assertRaisesRegex(devlink.UpdateError, "as before the REBOOT"):
            devlink.wait_for_version("p", "1.0-dev", 10, 100.0, poll, sleep, clock, quiet)

    def test_this_computers_address_must_be_private(self):
        class Probe:
            address = "192.168.1.77"
            fail = False

            def __init__(self, *_):
                pass

            def connect(self, _where):
                if Probe.fail:
                    raise OSError("Network is unreachable")

            def getsockname(self):
                return (Probe.address, 40000)

            def close(self):
                pass

        real = devlink.socket.socket
        devlink.socket.socket = Probe
        try:
            self.assertEqual(devlink.lan_ip(), "192.168.1.77")
            Probe.address = "203.0.113.9"
            with self.assertRaisesRegex(devlink.LinkError, "--host-ip"):
                devlink.lan_ip()
            Probe.fail = True
            with self.assertRaisesRegex(devlink.LinkError, "--host-ip"):
                devlink.lan_ip()
        finally:
            devlink.socket.socket = real


class FileServing(unittest.TestCase):
    """The file server of push-update, on the loopback address (no network needed)."""

    def test_it_serves_that_one_file_and_stops(self):
        with tempfile.TemporaryDirectory() as folder:
            local = os.path.join(folder, "my build.bin")
            data = bytes(range(256)) * 300
            with open(local, "wb") as handle:
                handle.write(data)
            with open(os.path.join(folder, "secret.txt"), "wb") as handle:
                handle.write(b"not served")
            with devlink.FileServer(local, "127.0.0.1") as server:
                self.assertEqual(server.url, f"http://127.0.0.1:{server.port}/firmware.bin")
                with urllib.request.urlopen(server.url, timeout=5) as answer:
                    self.assertEqual(answer.status, 200)
                    self.assertEqual(int(answer.headers["Content-Length"]), len(data))
                    self.assertEqual(answer.read(), data)
                self.assertEqual(os.listdir(server.folder), ["firmware.bin"])
                for other in ("secret.txt", "my%20build.bin", "../secret.txt"):
                    with self.assertRaises(urllib.error.HTTPError):
                        urllib.request.urlopen(f"http://127.0.0.1:{server.port}/{other}", timeout=5)
                served_from, port = server.folder, server.port
            self.assertFalse(os.path.exists(served_from))
            with self.assertRaises(OSError):
                socket.create_connection(("127.0.0.1", port), timeout=1).close()
            self.assertTrue(os.path.exists(local))  # the file itself is left alone

    def test_a_port_that_is_taken(self):
        with tempfile.TemporaryDirectory() as folder:
            local = os.path.join(folder, "firmware.bin")
            with open(local, "wb") as handle:
                handle.write(b"test")
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as taken:
                taken.bind(("127.0.0.1", 0))
                taken.listen(1)
                before = set(os.listdir(tempfile.gettempdir()))
                with self.assertRaisesRegex(devlink.LinkError, "cannot serve"):
                    devlink.FileServer(local, "127.0.0.1", taken.getsockname()[1])
                left = [name for name in set(os.listdir(tempfile.gettempdir())) - before if "devlink-push" in name]
                self.assertEqual(left, [])

    def test_a_file_that_is_not_there(self):
        with self.assertRaises(OSError):
            devlink.FileServer("/nonexistent/firmware.bin", "127.0.0.1")


class PushUpdate(unittest.TestCase):
    """`fetch` and `push-update` against the fake device; the file server is a stand-in that hands
    the bytes to the fake device's FETCH."""

    URL = "http://192.168.1.50:8123/firmware.bin"

    def setUp(self):
        devlink.REPLY_TIMEOUT_S = 0.6
        devlink.PUSH_POLL_S = 0.01
        self.devices = []
        self.folder = tempfile.TemporaryDirectory()
        self.local = os.path.join(self.folder.name, "firmware.bin")
        self.data = b"\xe9" + bytes(4095)
        with open(self.local, "wb") as handle:
            handle.write(self.data)
        self.digest = hashlib.sha256(self.data).hexdigest()
        self.served = []

    def tearDown(self):
        for device in self.devices:
            device.close()
        self.folder.cleanup()

    def device(self, **options):
        options.setdefault("files", {})
        options.setdefault("status", {"build": "dev", "wifi": True})
        device = FakeDevice(**options)
        self.devices.append(device)
        return device

    def server_for(self, device, corrupt=False):
        test = self

        class Server:
            def __init__(self, file, address, port):
                test.served.append(("start", address, port))
                with open(file, "rb") as handle:
                    data = handle.read()
                device.web[test.URL] = data + b"x" if corrupt else data
                self.url = test.URL

            def __enter__(self):
                return self

            def __exit__(self, *_):
                del device.web[test.URL]
                test.served.append(("stop",))

        return Server

    def push(self, device, expected="0.76.0-x4pro-dev", **options):
        said = []
        options.setdefault("serve", self.server_for(device))
        options.setdefault("host_ip", "192.168.1.50")
        status = devlink.push_update(
            device.path, self.local, expected, say=said.append, sleep=lambda _s: None, boot_timeout=3, **options
        )
        return status, said

    def run_main(self, device, *words):
        out = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = devlink.main(["--port", device.path, *words])
        return code, out.getvalue(), err.getvalue()

    def test_the_whole_update(self):
        device = self.device(new_version="0.76.0-x4pro-dev")
        status, said = self.push(device)
        self.assertEqual(status["version"], "0.76.0-x4pro-dev")
        self.assertEqual(
            device.seen[:4],
            [
                "CMD:STATUS",
                f"CMD:FETCH {self.URL} /firmware-dev.bin {self.digest}",
                "CMD:FLASH /firmware-dev.bin",
                "CMD:REBOOT",
            ],
        )
        self.assertEqual(set(device.seen[4:]), {"CMD:STATUS"})
        self.assertEqual(device.files["/firmware-dev.bin"], self.data)
        # The server ran for the FETCH only.
        self.assertEqual(self.served, [("start", "192.168.1.50", 0), ("stop",)])
        text = "\n".join(said)
        for part in ("1/5", self.digest, "2/5 FETCH", "fetch: 100%", "3/5 FLASH", "flash: 100%", "4/5 REBOOT", "Done"):
            self.assertIn(part, text)

    def test_the_address_is_looked_up_when_not_named(self):
        device = self.device(new_version="0.76.0-x4pro-dev")
        self.push(device, host_ip=None, find_ip=lambda: "10.1.2.3", http_port=8123, sd="/fw/new.bin")
        self.assertEqual(self.served[0], ("start", "10.1.2.3", 8123))
        self.assertIn("/fw/new.bin", device.files)

    def test_a_wrong_hash_stops_before_flash(self):
        device = self.device(new_version="0.76.0-x4pro-dev")
        with self.assertRaisesRegex(devlink.UpdateError, "sha256 mismatch"):
            self.push(device, serve=self.server_for(device, corrupt=True))
        self.assertEqual(len(device.seen), 2)  # STATUS and FETCH: no FLASH, no REBOOT
        self.assertEqual(device.files, {})
        self.assertEqual(self.served[-1], ("stop",))

    def test_a_reader_that_cannot_reach_the_server(self):
        device = self.device()

        class Unreachable(self.server_for(device)):
            def __init__(self, file, address, port):
                super().__init__(file, address, port)
                device.web.clear()
                device.web["http://elsewhere"] = b""

            def __exit__(self, *_):
                pass

        with self.assertRaisesRegex(devlink.UpdateError, "FETCH failed: cannot connect"):
            self.push(device, serve=Unreachable)
        self.assertEqual(len(device.seen), 2)

    def test_the_wrong_build_or_no_wifi_sends_nothing_more(self):
        for status, message in (
            ({"build": "dev-user", "wifi": True}, "x4pro-dev"),
            ({"build": "dev", "wifi": False}, "Wi-Fi"),
        ):
            device = self.device(status=status)
            with self.assertRaisesRegex(devlink.UpdateError, message):
                self.push(device)
            self.assertEqual(device.seen, ["CMD:STATUS"])
        self.assertEqual(self.served, [])

    def test_a_version_that_never_comes(self):
        device = self.device(new_version="0.75.9-x4pro-dev")
        with self.assertRaisesRegex(devlink.UpdateError, "not '0.76.0-x4pro-dev'"):
            self.push(device)
        self.assertIn("CMD:REBOOT", device.seen)

    def test_a_flash_that_is_refused(self):
        device = self.device()
        real = device.answer_sd

        def answer_sd(line):
            if line.startswith("CMD:FLASH "):
                return {"ok": False, "error": "image refused", "reason": "WRONG_BOARD", "slot": "app1"}
            return real(line)

        device.answer_sd = answer_sd
        with self.assertRaisesRegex(devlink.UpdateError, r"FLASH failed: image refused \(WRONG_BOARD\)"):
            self.push(device)
        self.assertNotIn("CMD:REBOOT", device.seen)

    def test_what_is_wrong_before_the_port_is_opened(self):
        device = self.device()
        for options in ({"host_ip": "8.8.8.8"}, {"host_ip": "127.0.0.1"}, {"sd": "firmware.bin"}, {"expected": ""}):
            with self.assertRaises(ValueError, msg=options):
                self.push(device, **options)
        self.assertEqual(device.seen, [])

    def test_push_update_from_the_command_line(self):
        device = self.device(status={"build": "dev-user", "wifi": True})
        code, _, err = self.run_main(device, "push-update", self.local, "--expect", "0.76.0-x4pro-dev")
        self.assertEqual(code, 1)
        self.assertIn("x4pro-dev", err)
        missing = os.path.join(self.folder.name, "missing.bin")
        self.assertEqual(self.run_main(device, "push-update", missing, "--expect", "x")[0], 2)
        code, _, err = self.run_main(device, "push-update", self.local, "--expect", "x", "--host-ip", "example.com")
        self.assertEqual(code, 2)
        self.assertIn("--host-ip", err)
        self.assertEqual(device.seen, ["CMD:STATUS"])

    def test_fetch_from_the_command_line(self):
        device = self.device(web={self.URL: self.data})
        code, out, _ = self.run_main(device, "fetch", self.URL, "/firmware-dev.bin", self.digest)
        self.assertEqual(code, 0)
        lines = out.splitlines()
        self.assertEqual(json.loads(lines[0]), {"fetch": "started", "total": 4096})
        self.assertEqual(json.loads(lines[1])["percent"], 100)
        self.assertEqual(json.loads(lines[2])["sha256"], self.digest)
        self.assertIn("devlink.py flash /firmware-dev.bin", lines[3])
        self.assertEqual(device.seen, [f"CMD:FETCH {self.URL} /firmware-dev.bin {self.digest}"])  # no FLASH, no REBOOT
        # A wrong hash: the reader's answer, exit code 1, no hint.
        code, out, _ = self.run_main(device, "fetch", self.URL, "/firmware-dev.bin", "0" * 64)
        self.assertEqual(code, 1)
        self.assertNotIn("devlink.py flash", out)

    def test_fetch_refuses_before_anything_is_sent(self):
        device = self.device()
        for url, path, digest in (
            ("https://192.168.1.50/firmware.bin", "/firmware.bin", self.digest),
            ("http://example.com/firmware.bin", "/firmware.bin", self.digest),
            ("http://8.8.8.8/firmware.bin", "/firmware.bin", self.digest),
            (self.URL, "/firmware.txt", self.digest),
            (self.URL, "/firmware.bin", "abc"),
        ):
            code, _, err = self.run_main(device, "fetch", url, path, digest)
            self.assertEqual(code, 2, (url, path))
            self.assertTrue(err.startswith("devlink: "))
        self.assertEqual(device.seen, [])


class BenchFile(unittest.TestCase):
    def test_rows_and_what_is_skipped(self):
        text = BENCH_FILE + "1794572700,15\n" + BENCH_FILE.splitlines()[0] + "\n1,2,x,4,0,0,0,0,0,0,0,0,A,0,0,0\n"
        rows, skipped = devlink.parse_bench_file(text.splitlines())
        self.assertEqual(len(rows), 9)
        self.assertEqual(skipped, 2)  # the cut line and the one with a letter; the second header is not a row
        self.assertEqual(rows[0]["battery_mv"], 4000)
        self.assertEqual(rows[5]["activity"], "Home")
        self.assertEqual(devlink.BENCH_FILE_COLUMNS, BENCH_FILE.splitlines()[0].split(","))

    def test_the_columns_are_the_ones_the_firmware_writes(self):
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        with open(os.path.join(root, "lib", "DevLink", "DevLinkBench.cpp"), encoding="utf-8") as handle:
            # The header is one string written as adjacent literals: join them.
            text = "".join(handle.read().split()).replace('""', "")
        self.assertIn('"' + ",".join(devlink.BENCH_FILE_COLUMNS) + '\\n"', text)

    def test_stretches(self):
        rows, _ = devlink.parse_bench_file(BENCH_FILE.splitlines())
        stretches, breaks = devlink.bench_stretches(rows)
        self.assertEqual(breaks, 1)  # the uptime started again: a sleep or a restart
        self.assertEqual(
            [stretch["group"] for stretch in stretches],
            ["light off", "light off", "light changed", "light on", "light on", "cable in", "cable in"],
        )
        first = stretches[0]
        self.assertAlmostEqual(first["minutes"], 5.0)
        self.assertAlmostEqual(first["mv_per_hour"], 24.0)  # 2 mV in 5 minutes
        self.assertAlmostEqual(first["percent_per_hour"], 0.0)
        self.assertAlmostEqual(stretches[4]["percent_per_hour"], 12.0)  # 1 percent in 5 minutes
        self.assertEqual(stretches[4]["activity"], "Home")
        self.assertAlmostEqual(stretches[5]["minutes"], 10.0)  # from the uptime: the clock was not set
        self.assertLess(stretches[6]["mv_per_hour"], 0)  # charging

    def test_groups_weigh_by_time(self):
        rows, _ = devlink.parse_bench_file(BENCH_FILE.splitlines())
        groups = {group["group"]: group for group in devlink.bench_groups(devlink.bench_stretches(rows)[0])}
        self.assertEqual(list(groups), ["light off", "light on", "light changed", "cable in"])
        self.assertEqual(groups["light off"]["stretches"], 2)
        self.assertAlmostEqual(groups["light off"]["minutes"], 10.0)
        self.assertAlmostEqual(groups["light off"]["mv_per_hour"], 36.0)  # 6 mV in 10 minutes
        self.assertAlmostEqual(groups["light off"]["percent_per_hour"], 6.0)
        self.assertAlmostEqual(groups["light on"]["mv_per_hour"], 120.0)  # 20 mV in 10 minutes
        self.assertAlmostEqual(groups["light on"]["percent_per_hour"], 12.0)
        # 2 mV down in 10 minutes, then 32 mV up in 10: 30 mV up in 20 minutes.
        self.assertAlmostEqual(groups["cable in"]["mv_per_hour"], -90.0)

    def test_report(self):
        report = devlink.bench_file_report(BENCH_FILE.splitlines(), "bench.csv")
        lines = report.splitlines()
        self.assertEqual(lines[0], "bench.csv: 9 rows, 7 awake stretches, 1 breaks (sleep or restart), 0 lines skipped")
        self.assertIn("state          stretches  minutes  percent/h   mV/h", lines)
        self.assertIn("light off              2     10.0       6.00   36.0", lines)
        self.assertIn("light on               2     10.0      12.00  120.0", lines)
        self.assertIn("cable in               2     20.0      -3.00  -90.0", lines)
        self.assertTrue(any(line.startswith("up 0.0 min") and line.endswith("Home") for line in lines), report)
        self.assertTrue(any("80>79" in line and "3998>3994" in line for line in lines), report)

    def test_a_file_without_a_stretch(self):
        one = "\n".join(BENCH_FILE.splitlines()[:2])
        report = devlink.bench_file_report(one.splitlines(), "one.csv")
        self.assertIn("1 rows, 0 awake stretches", report)
        self.assertIn("nothing to work out", report)
        self.assertIn("0 rows", devlink.bench_file_report([], "empty.csv"))

    def test_main_bench_file(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "bench.csv")
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(BENCH_FILE)
            self.assertEqual(devlink.main(["bench-file", path]), 0)
            self.assertEqual(devlink.main(["bench-file", os.path.join(folder, "missing.csv")]), 2)


if __name__ == "__main__":
    unittest.main()
