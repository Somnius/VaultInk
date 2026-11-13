#!/usr/bin/env python3
"""Host side of the VaultInk developer link (docs/devlink.md).

    devlink.py status                      one STATUS reply, a field a line
    devlink.py cmd STATUS                  any command, the raw JSON reply
    devlink.py cmd LOG 2
    devlink.py screenshot out.png          .png or .bmp, the same picture as Power + Down
    devlink.py bench --every 60 --minutes 30 --out idle.csv

Only the x4pro-dev build answers the JSON commands; every build answers the screenshot.
Standard library only. On Linux and macOS the port is opened with termios; pyserial is used
only where termios does not exist (Windows), and that path has not been run.
"""

from __future__ import annotations

import argparse
import csv
import glob
import json
import os
import select
import struct
import sys
import time
import zlib

try:
    import termios
except ImportError:  # Windows
    termios = None

PANEL_WIDTH = 800
PANEL_HEIGHT = 480
REPLY_TIMEOUT_S = 3.0
SCREENSHOT_TIMEOUT_S = 20.0
CSV_COLUMNS = [
    "time",
    "epoch",
    "link",
    "battery_mv",
    "battery_percent",
    "heap_free",
    "cpu_mhz",
    "light_on",
    "light_brightness",
    "activity",
    "usb",
    "charging",
    "idle_ms",
    "uptime_ms",
]


class LinkError(Exception):
    """No port, no answer, or the port went away (sleep, USB Drive, unplugged)."""


# ---- parsing (no I/O; test/dev_link/test_devlink_script.py covers it) ----


def parse_reply(line: bytes | str):
    """The reply object of one line, or None for a log line or anything else."""
    if isinstance(line, bytes):
        line = line.decode("utf-8", errors="replace")
    line = line.strip()
    if not (line.startswith("{") and line.endswith("}")):
        return None
    try:
        reply = json.loads(line)
    except ValueError:
        return None
    if not isinstance(reply, dict) or not isinstance(reply.get("ok"), bool):
        return None
    return reply


def parse_screenshot_start(line: bytes):
    """The byte count of a SCREENSHOT_START:<n> line, or None."""
    text = line.strip()
    prefix = b"SCREENSHOT_START:"
    if not text.startswith(prefix):
        return None
    digits = text[len(prefix) :]
    if not digits.isdigit():
        return None
    return int(digits)


def rotate_framebuffer(data: bytes, width: int = PANEL_WIDTH, height: int = PANEL_HEIGHT):
    """Turn the panel's framebuffer into the upright picture, as rows of packed bits, top row first.

    The same turn as ScreenshotUtil::saveFramebufferAsBmp (and as debugging_monitor.py's
    ROTATE_270): the picture is `height` wide and `width` tall, and its pixel (x, y) is the
    framebuffer's pixel (y, height - 1 - x). Bit 7 of a byte is the leftmost pixel; 1 is white.
    """
    width_bytes = width // 8
    if width % 8 or len(data) != width_bytes * height:
        raise ValueError(f"{len(data)} bytes is not a {width}x{height} 1-bit framebuffer")
    out_width = height
    out_row_bytes = (out_width + 7) // 8
    rows = []
    for y in range(width):
        row = bytearray(out_row_bytes)
        source_byte = y // 8
        source_shift = 7 - (y % 8)
        for x in range(out_width):
            source_row = height - 1 - x
            if (data[source_row * width_bytes + source_byte] >> source_shift) & 1:
                row[x // 8] |= 0x80 >> (x % 8)
        rows.append(bytes(row))
    return out_width, rows


def encode_bmp(width: int, rows) -> bytes:
    """A 1-bit BMP, bottom row first, palette 0 = black and 1 = white, like the device's own."""
    padded = (width + 31) // 32 * 4
    pixels = b"".join(row.ljust(padded, b"\0") for row in reversed(rows))
    offset = 14 + 40 + 8
    header = struct.pack("<2sIHHI", b"BM", offset + len(pixels), 0, 0, offset)
    info = struct.pack("<IiiHHIIiiII", 40, width, len(rows), 1, 1, 0, len(pixels), 2835, 2835, 2, 2)
    palette = bytes([0, 0, 0, 0, 255, 255, 255, 0])
    return header + info + palette + pixels


def encode_png(width: int, rows) -> bytes:
    """A 1-bit greyscale PNG (1 = white)."""

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    header = struct.pack(">IIBBBBB", width, len(rows), 1, 0, 0, 0, 0)
    raw = b"".join(b"\0" + row for row in rows)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def encode_picture(path: str, data: bytes, width: int = PANEL_WIDTH, height: int = PANEL_HEIGHT) -> bytes:
    out_width, rows = rotate_framebuffer(data, width, height)
    if path.lower().endswith(".png"):
        return encode_png(out_width, rows)
    if path.lower().endswith(".bmp"):
        return encode_bmp(out_width, rows)
    raise ValueError("the output file must end in .png or .bmp")


def csv_row(status, now: float) -> dict:
    """One bench row; `status` is a STATUS reply, or None when the device did not answer."""
    row = {column: "" for column in CSV_COLUMNS}
    row["time"] = time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(now))
    row["epoch"] = int(now)
    if not status or not status.get("ok"):
        row["link"] = "none"
        return row
    row["link"] = "ok"
    for column in CSV_COLUMNS[3:]:
        value = status.get(column, "")
        if isinstance(value, bool):
            value = int(value)
        row[column] = value
    return row


# ---- the port ----


def find_port() -> str:
    """The first port that looks like the reader: Espressif's USB Serial/JTAG, else any ttyACM."""
    env = os.environ.get("DEVLINK_PORT")
    if env:
        return env
    for pattern in (
        "/dev/serial/by-id/*Espressif*JTAG*",
        "/dev/ttyACM*",
        "/dev/cu.usbmodem*",
    ):
        ports = sorted(glob.glob(pattern))
        if ports:
            return ports[0]
    raise LinkError("no serial port (asleep, in USB Drive mode, or not plugged in)")


class TermiosPort:
    def __init__(self, path: str):
        try:
            self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as error:
            raise LinkError(f"cannot open {path}: {error.strerror}") from error
        try:
            attrs = termios.tcgetattr(self.fd)
            attrs[0] = 0  # iflag: no translation, no flow control
            attrs[1] = 0  # oflag: no translation
            # 8 data bits, receiver on, ignore modem lines, and keep DTR/RTS as they are on close.
            attrs[2] = (attrs[2] | termios.CS8 | termios.CREAD | termios.CLOCAL) & ~(
                termios.PARENB | termios.CSTOPB | termios.HUPCL
            )
            attrs[3] = 0  # lflag: raw, no echo
            attrs[6][termios.VMIN] = 0
            attrs[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        except termios.error:
            pass  # a pipe or a file in a test: read and write still work
        self.pending = b""

    def close(self) -> None:
        try:
            os.close(self.fd)
        except OSError:
            pass

    def write(self, data: bytes) -> None:
        deadline = time.monotonic() + REPLY_TIMEOUT_S
        try:
            while data:
                if time.monotonic() > deadline:
                    raise LinkError("the port does not take data")
                _, writable, _ = select.select([], [self.fd], [], 0.2)
                if writable:
                    data = data[os.write(self.fd, data) :]
        except OSError as error:
            raise LinkError(f"the port went away: {error.strerror}") from error

    def _fill(self, deadline: float) -> bool:
        """Read what is there, waiting until the deadline at most. False when nothing came."""
        wait = max(0.0, deadline - time.monotonic())
        try:
            readable, _, _ = select.select([self.fd], [], [], wait)
            if not readable:
                return False
            data = os.read(self.fd, 65536)
        except BlockingIOError:
            return False
        except OSError as error:
            raise LinkError(f"the port went away: {error.strerror}") from error
        if not data:
            raise LinkError("the port closed")
        self.pending += data
        return True

    def drain(self) -> None:
        """Throw away what the device sent before we asked (old log lines)."""
        self.pending = b""
        stop = time.monotonic() + 0.5  # a device that never stops logging must not hold us here
        while time.monotonic() < stop and self._fill(time.monotonic() + 0.05):
            self.pending = b""

    def read_line(self, deadline: float) -> bytes:
        while b"\n" not in self.pending:
            if not self._fill(deadline) and time.monotonic() >= deadline:
                raise LinkError("no answer (is this the x4pro-dev build, awake and out of USB Drive mode?)")
        line, _, self.pending = self.pending.partition(b"\n")
        return line

    def read_exact(self, count: int, deadline: float) -> bytes:
        while len(self.pending) < count:
            if not self._fill(deadline) and time.monotonic() >= deadline:
                raise LinkError(f"only {len(self.pending)} of {count} bytes came")
        data, self.pending = self.pending[:count], self.pending[count:]
        return data


class PySerialPort(TermiosPort):
    """The same calls over pyserial, for systems without termios. Never run: unverified."""

    def __init__(self, path: str):  # pylint: disable=super-init-not-called
        try:
            import serial  # type: ignore
        except ImportError as error:
            raise LinkError("this system has no termios; install pyserial (pip install pyserial)") from error
        try:
            self.serial = serial.Serial(path, 115200, timeout=0.05)
        except serial.SerialException as error:
            raise LinkError(f"cannot open {path}: {error}") from error
        self.pending = b""

    def close(self) -> None:
        self.serial.close()

    def write(self, data: bytes) -> None:
        try:
            self.serial.write(data)
        except Exception as error:
            raise LinkError(f"the port went away: {error}") from error

    def _fill(self, deadline: float) -> bool:
        try:
            data = self.serial.read(65536)
        except Exception as error:
            raise LinkError(f"the port went away: {error}") from error
        self.pending += data
        return bool(data)


def open_port(path: str | None):
    path = path or find_port()
    return TermiosPort(path) if termios else PySerialPort(path)


def command(port, text: str, timeout: float = REPLY_TIMEOUT_S) -> dict:
    """Send CMD:<text> and return its JSON reply, skipping the log lines around it."""
    port.drain()
    port.write(f"\nCMD:{text}\n".encode())  # a bare newline first ends any stray fragment the reader holds
    deadline = time.monotonic() + timeout
    while True:
        if time.monotonic() >= deadline:  # a device that only logs must not keep this loop going
            raise LinkError("no reply in time (is this the x4pro-dev build, awake and out of USB Drive mode?)")
        reply = parse_reply(port.read_line(deadline))
        if reply is not None:
            return reply


def screenshot(port, timeout: float = SCREENSHOT_TIMEOUT_S) -> bytes:
    """The raw framebuffer of CMD:SCREENSHOT (SCREENSHOT_START:<n>, n bytes, SCREENSHOT_END)."""
    port.drain()
    port.write(b"\nCMD:SCREENSHOT\n")
    deadline = time.monotonic() + timeout
    while True:
        size = parse_screenshot_start(port.read_line(deadline))
        if size is not None:
            break
    if size <= 0 or size > 4 * 1024 * 1024:
        raise LinkError(f"the device announced {size} bytes")
    data = port.read_exact(size, deadline)
    # The marker follows the data at once; a log line may come first.
    end_deadline = min(deadline, time.monotonic() + REPLY_TIMEOUT_S)
    while True:
        try:
            line = port.read_line(end_deadline)
        except LinkError as error:
            raise LinkError("SCREENSHOT_END did not follow the data: bytes were lost on the way") from error
        if line.strip() == b"SCREENSHOT_END":
            return data


def status_once(path: str | None):
    """One STATUS on a fresh connection, or None when the device is not there or does not answer."""
    try:
        port = open_port(path)
    except LinkError:
        return None
    try:
        return command(port, "STATUS")
    except LinkError:
        return None
    finally:
        port.close()


def bench(path: str | None, every: float, minutes: float, out: str, sleep=time.sleep, clock=time.time) -> int:
    """Append a row every `every` seconds for `minutes`. A row with link=none marks a missed poll;
    the port is looked up and opened again for every poll, so the device may sleep, reboot or sit
    in USB Drive mode in between."""
    new_file = not os.path.exists(out) or os.path.getsize(out) == 0
    end = clock() + minutes * 60
    rows = 0
    with open(out, "a", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_COLUMNS)
        if new_file:
            writer.writeheader()
        while True:
            now = clock()
            row = csv_row(status_once(path), now)
            writer.writerow(row)
            handle.flush()
            rows += 1
            print(",".join(str(row[column]) for column in CSV_COLUMNS), flush=True)
            if now + every > end:
                return rows
            sleep(max(0.0, now + every - clock()))


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="VaultInk developer link (docs/devlink.md)")
    parser.add_argument("--port", help="serial port (default: $DEVLINK_PORT, else the first /dev/ttyACM*)")
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("status", help="print STATUS, a field a line")
    cmd = sub.add_parser("cmd", help="send one command and print the JSON reply")
    cmd.add_argument("words", nargs="+", help="e.g. STATUS, or LOG 2")
    shot = sub.add_parser("screenshot", help="save the screen as .png or .bmp")
    shot.add_argument("file")
    shot.add_argument("--width", type=int, default=PANEL_WIDTH, help="panel width in pixels (800)")
    shot.add_argument("--height", type=int, default=PANEL_HEIGHT, help="panel height in pixels (480)")
    run = sub.add_parser("bench", help="poll STATUS into a CSV file")
    run.add_argument("--every", type=float, default=60, help="seconds between polls (60)")
    run.add_argument("--minutes", type=float, default=30, help="how long to run (30)")
    run.add_argument("--out", required=True, help="CSV file, appended to")
    args = parser.parse_args(argv)

    try:
        if args.action == "bench":
            if args.every <= 0 or args.minutes <= 0:
                parser.error("--every and --minutes must be above 0")
            bench(args.port, args.every, args.minutes, args.out)
            return 0
        if args.action == "screenshot" and not args.file.lower().endswith((".png", ".bmp")):
            parser.error("the output file must end in .png or .bmp")
        port = open_port(args.port)
        try:
            if args.action == "screenshot":
                data = screenshot(port)
                picture = encode_picture(args.file, data, args.width, args.height)
                with open(args.file, "wb") as handle:
                    handle.write(picture)
                print(f"{args.file}: {len(data)} bytes from the device")
                return 0
            reply = command(port, "STATUS" if args.action == "status" else " ".join(args.words))
        finally:
            port.close()
    except (LinkError, ValueError) as error:
        print(f"devlink: {error}", file=sys.stderr)
        return 2
    if args.action == "status" and reply.get("ok"):
        for key, value in reply.items():
            print(f"{key}: {value}")
    else:
        print(json.dumps(reply, ensure_ascii=False))
    return 0 if reply.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
