#!/usr/bin/env python3
"""Tests for scripts/devlink.py against a fake device on a pseudo-terminal.

    python3 test/test_devlink_script.py
"""

import base64
import contextlib
import csv
import io
import json
import os
import pty
import random
import struct
import sys
import tempfile
import threading
import unittest
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

    def __init__(self, framebuffer=b"", dev_build=True, lose_bytes=0, end_marker=True, files=None, cut_gets=0):
        self.master, slave = pty.openpty()
        self.path = os.ttyname(slave)
        self.slave = slave  # kept open so the master never sees a hang-up between two opens
        self.framebuffer = framebuffer
        self.dev_build = dev_build
        self.lose_bytes = lose_bytes
        self.end_marker = end_marker
        self.files = files  # a made-up SD card: path -> bytes; None for a device without the SD commands
        self.cut_gets = cut_gets  # this many GET replies are cut short by a log line
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
            reply = STATUS
        elif line == "CMD:PING":
            reply = {"ok": True, "pong": True}
        elif line.startswith("CMD:LOG "):
            reply = {"ok": True, "level": int(line.split()[1])}
        elif line.startswith(("CMD:KEY ", "CMD:TAP ", "CMD:SWIPE ")):
            self.queued += 2
            reply = {"ok": True, "queued": self.queued}
        elif self.files is not None and line.startswith(("CMD:LS ", "CMD:GET ", "CMD:PUT ", "CMD:FLASH ")):
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
        if name == "FLASH":
            if rest not in self.files:
                return {"ok": False, "error": "cannot open the file"}
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
