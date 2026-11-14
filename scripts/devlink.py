#!/usr/bin/env python3
"""Host side of the VaultInk developer link (docs/devlink.md).

    devlink.py status                      one STATUS reply, a field a line
    devlink.py cmd STATUS                  any command, the raw JSON reply
    devlink.py cmd LOG 2
    devlink.py screenshot out.png          .png or .bmp, the same picture as Power + Down
    devlink.py bench --every 60 --minutes 30 --out idle.csv
    devlink.py bench-file bench.csv        the drain per hour from a copied /.vaultink/bench.csv
    devlink.py key CONFIRM                 press a key (BACK CONFIRM LEFT RIGHT UP DOWN; never POWER)
    devlink.py tap 240 400                 touch a screen pixel, as a screenshot shows it
    devlink.py swipe 400 300 80 300        a swipe from one screen pixel to another
    devlink.py ls /debug                   list a folder of the SD card
    devlink.py get /.vaultink/bench.csv bench.csv
    devlink.py put note.txt /debug/note.txt
    devlink.py flash /firmware.bin         flash a .bin that is already on the SD card; then cmd REBOOT
    devlink.py fetch http://192.168.1.20:8000/firmware.bin /firmware-dev.bin <sha256>
                                           the reader downloads a .bin to its card over Wi-Fi (LAN only)
    devlink.py push-update firmware.bin --expect 0.75.0-x4pro-dev
                                           serve the file, FETCH, FLASH, REBOOT, wait for that version
    devlink.py wait --activity Home        wait until the reader is at rest (and that screen is in front)
    devlink.py screenshot out.png --verify check the bytes against CMD:SHOTCRC, ask again when they differ

scripts/devtest.py plays a JSON scenario of these over one open port.

Only the x4pro-dev build answers the JSON commands; every build answers the screenshot. The SD
commands reach /debug, /screenshots and a few files of /.vaultink, nothing else (docs/devlink.md).
Standard library only. On Linux and macOS the port is opened with termios; pyserial is used
only where termios does not exist (Windows), and that path has not been run.
"""

from __future__ import annotations

import argparse
import base64
import binascii
import csv
import glob
import hashlib
import json
import os
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

try:
    import termios
except ImportError:  # Windows
    termios = None

PANEL_WIDTH = 800
PANEL_HEIGHT = 480
REPLY_TIMEOUT_S = 3.0
FLASH_TIMEOUT_S = 600.0
FETCH_TIMEOUT_S = 200.0  # the reader gives up after 180 s (src/util/DevLinkDevice.cpp)
FETCH_MAX_BYTES = 0x7F0000  # one app slot, as lib/DevLink/DevLinkFetch.h
FETCH_MAX_URL = 160
FETCH_URL_PATH_CHARS = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-~/")
PUSH_SD_PATH = "/firmware-dev.bin"
PUSH_SERVED_NAME = "firmware.bin"
PUSH_BOOT_TIMEOUT_S = 90.0
PUSH_POLL_S = 2.0
KEY_NAMES = ("BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN")
MIN_HOLD_MS = 20
MAX_HOLD_MS = 5000
MAX_COMMAND_BYTES = 256  # the reader drops a longer line whole
GET_CHUNK_BYTES = 2880  # the most one CMD:GET returns
PUT_CHUNK_BYTES = 180  # the most one CMD:PUT takes
CHUNK_TRIES = 3
SCREENSHOT_TIMEOUT_S = 20.0
SCREENSHOT_TRIES = 3
MIN_WAIT_MS = 100  # CMD:WAIT, as lib/DevLink/DevLinkArgs.h
MAX_WAIT_MS = 60000
DEFAULT_WAIT_MS = 5000
MAX_ACTIVITY_NAME = 31
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


class UpdateError(Exception):
    """A step of push-update that the reader answered and refused, or that did not end as expected."""


# ---- parsing (no I/O; test/dev_link/test_devlink_script.py covers it) ----


def parse_json_line(line: bytes | str):
    """The JSON object of one line, or None for a log line or anything else."""
    if isinstance(line, bytes):
        line = line.decode("utf-8", errors="replace")
    line = line.strip()
    if not (line.startswith("{") and line.endswith("}")):
        return None
    try:
        reply = json.loads(line)
    except ValueError:
        return None
    return reply if isinstance(reply, dict) else None


def parse_reply(line: bytes | str):
    """The reply object of one line (it has "ok"), or None for a log line or anything else. A JSON
    line without "ok" is not a reply: LS sends one for every entry and FLASH for its progress."""
    reply = parse_json_line(line)
    if reply is None or not isinstance(reply.get("ok"), bool):
        return None
    return reply


# ---- keys, touches and SD files: the command texts (no I/O) ----


def _hold(value, name: str) -> str:
    if value is None:
        return ""
    if not MIN_HOLD_MS <= value <= MAX_HOLD_MS:
        raise ValueError(f"{name} must be {MIN_HOLD_MS} to {MAX_HOLD_MS} ms")
    return f" {value}"


def _pixel(value: int) -> int:
    if not 0 <= value <= 65535:
        raise ValueError("a coordinate is a screen pixel, 0 or above")
    return value


def key_command(name: str, hold=None) -> str:
    """KEY <name> [hold_ms]. The power key is refused here as it is on the reader."""
    name = name.upper()
    if name == "POWER":
        raise ValueError("POWER is not allowed: the link never presses the power key")
    if name not in KEY_NAMES:
        raise ValueError("the key must be one of " + " ".join(KEY_NAMES))
    return f"KEY {name}{_hold(hold, 'the hold time')}"


def tap_command(x: int, y: int, hold=None) -> str:
    return f"TAP {_pixel(x)} {_pixel(y)}{_hold(hold, 'the hold time')}"


def swipe_command(x1: int, y1: int, x2: int, y2: int, ms=None) -> str:
    return f"SWIPE {_pixel(x1)} {_pixel(y1)} {_pixel(x2)} {_pixel(y2)}{_hold(ms, 'the swipe time')}"


def wait_command(activity: str = "", timeout_ms: int = DEFAULT_WAIT_MS) -> str:
    """WAIT [activity] <timeout_ms>. The time is always written, so a screen name that ends in a
    number cannot be taken for it."""
    if not isinstance(timeout_ms, int) or isinstance(timeout_ms, bool) or not MIN_WAIT_MS <= timeout_ms <= MAX_WAIT_MS:
        raise ValueError(f"the wait time must be {MIN_WAIT_MS} to {MAX_WAIT_MS} ms")
    if not isinstance(activity, str):
        raise ValueError("the screen name must be text")
    activity = activity.strip()
    if len(activity.encode()) > MAX_ACTIVITY_NAME or any(ord(c) < 0x20 or ord(c) == 0x7F for c in activity):
        raise ValueError(f"a screen name is {MAX_ACTIVITY_NAME} bytes at most, on one line")
    return f"WAIT {activity} {timeout_ms}" if activity else f"WAIT {timeout_ms}"


def sd_path(path: str) -> str:
    """A path on the reader's card as the commands take it. The reader decides what is allowed;
    this only catches what can never be right before anything is sent."""
    if not path.startswith("/") or ".." in path or "\n" in path or "\r" in path:
        raise ValueError(f"{path!r} is not a path on the reader's SD card (it starts with / and has no '..')")
    return path


def put_commands(path: str, data: bytes):
    """The CMD:PUT lines that write `data` to `path`: each as large as the 256-byte line allows,
    180 bytes of data at most, each at the offset where the one before ended."""
    path = sd_path(path)
    if not data:
        raise ValueError("an empty file cannot be sent")
    commands = []
    offset = 0
    while offset < len(data):
        room = MAX_COMMAND_BYTES - len(f"CMD:PUT {path} {offset} ".encode())
        size = min(PUT_CHUNK_BYTES, room // 4 * 3)
        if size < 3:
            raise ValueError("the path is too long for a 256-byte command")
        text = base64.b64encode(data[offset : offset + size]).decode()
        commands.append(f"PUT {path} {offset} {text}")
        offset += size
    return commands


def get_data(reply: dict, offset: int) -> bytes:
    """The bytes of one CMD:GET reply, checked against what it says it holds."""
    try:
        data = base64.b64decode(reply.get("data", ""), validate=True)
    except (binascii.Error, ValueError) as error:
        raise LinkError("the data of a GET reply is not base64") from error
    if reply.get("offset") != offset or reply.get("len") != len(data):
        raise LinkError("a GET reply does not match what was asked for")
    return data


# ---- FETCH and push-update: the parts without I/O ----


def _plain_number(text: str, most: int) -> int:
    """A decimal number with no sign and no leading zero, 0 to `most`; ValueError otherwise."""
    if not text.isascii() or not text.isdigit() or (len(text) > 1 and text[0] == "0") or int(text) > most:
        raise ValueError(text)
    return int(text)


def private_ipv4(text: str) -> bool:
    """True for a dotted IPv4 address in 10/8, 172.16/12 or 192.168/16, written the plain way."""
    parts = text.split(".")
    if len(parts) != 4:
        return False
    try:
        a, b, _, _ = (_plain_number(part, 255) for part in parts)
    except ValueError:
        return False
    return a == 10 or (a == 172 and 16 <= b <= 31) or (a == 192 and b == 168)


def fetch_url(url: str) -> str:
    """The URL as CMD:FETCH takes it, or ValueError with the reason. The reader checks again
    (devlink::checkFetchUrl); this is the same rule, so a wrong URL is caught before anything is sent."""
    if not url or len(url.encode()) > FETCH_MAX_URL:
        raise ValueError(f"the URL is empty or over {FETCH_MAX_URL} bytes")
    if not url.startswith("http://"):
        raise ValueError("only http:// is taken (no https, no other scheme): the reader fetches from the LAN only")
    host, slash, path = url[len("http://") :].partition("/")
    address, colon, port = host.partition(":")
    if not private_ipv4(address):
        raise ValueError(
            "the host must be an IPv4 address in 10.0.0.0/8, 172.16.0.0/12 or 192.168.0.0/16 "
            "(no host name, no user@, no IPv6)"
        )
    if colon:
        try:
            if _plain_number(port, 65535) == 0:
                raise ValueError(port)
        except ValueError:
            raise ValueError("the port must be 1 to 65535") from None
    path = slash + path
    if (
        len(path) < 2
        or path.endswith("/")
        or ".." in path
        or "//" in path
        or any(c not in FETCH_URL_PATH_CHARS for c in path)
    ):
        raise ValueError("the URL path may hold letters, digits and . _ - ~ / only, with no '..' and no '//'")
    return url


def sha256_text(text: str) -> str:
    """64 hex digits in lower case, or ValueError."""
    text = text.strip().lower()
    if len(text) != 64 or any(c not in "0123456789abcdef" for c in text):
        raise ValueError("the SHA-256 is 64 hex digits (sha256sum prints it)")
    return text


def fetch_command(url: str, path: str, sha256: str) -> str:
    """FETCH <url> <sd path> <sha256>."""
    path = sd_path(path)
    if not path.lower().endswith(".bin"):
        raise ValueError("FETCH writes a .bin only (the same paths FLASH reads)")
    text = f"FETCH {fetch_url(url)} {path} {sha256_text(sha256)}"
    if len(f"CMD:{text}".encode()) > MAX_COMMAND_BYTES:
        raise ValueError("the URL and the path are too long for a 256-byte command")
    return text


def sha256_file(path: str) -> tuple[str, int]:
    """The SHA-256 and the size of a local file."""
    digest = hashlib.sha256()
    size = 0
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(65536), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


def check_push_file(path: str) -> tuple[str, int]:
    """The SHA-256 and size of the image to push; ValueError when it cannot be one."""
    if not path.lower().endswith(".bin"):
        raise ValueError(f"{path} is not a .bin")
    digest, size = sha256_file(path)
    if size == 0:
        raise ValueError(f"{path} is empty")
    if size > FETCH_MAX_BYTES:
        raise ValueError(f"{path} is {size} bytes; an app slot holds {FETCH_MAX_BYTES}")
    return digest, size


def check_push_status(status: dict) -> None:
    """What STATUS must say before a push starts; UpdateError otherwise."""
    if not status.get("ok"):
        raise UpdateError(f"STATUS failed: {status.get('error', status)}")
    if status.get("build") != "dev":
        raise UpdateError(
            f"this reader runs build {status.get('build')!r}; FETCH and FLASH are only in x4pro-dev "
            "(the read-only x4pro-dev-user and the release cannot be updated this way)"
        )
    if not status.get("wifi"):
        raise UpdateError("the reader's Wi-Fi is off: join the network on the reader first, then run this again")


def check_fetch_reply(reply: dict, digest: str, size: int) -> None:
    """The reader must have written exactly the file that was served."""
    if not reply.get("ok"):
        reason = f" ({reply['reason']})" if reply.get("reason") else ""
        raise UpdateError(f"FETCH failed: {reply.get('error', reply)}{reason}")
    if reply.get("sha256") != digest or reply.get("bytes") != size:
        raise UpdateError(
            f"FETCH answered {reply.get('bytes')} bytes, sha256 {reply.get('sha256')}; "
            f"the file is {size} bytes, sha256 {digest}"
        )


def booted_as(status, expected: str, since_reboot_s: float) -> bool:
    """True when this STATUS is from the expected version, started after the REBOOT that was sent
    `since_reboot_s` ago. The uptime is what tells a new start from the old firmware still running
    (two developer builds often have the same version text)."""
    if not status or not status.get("ok") or status.get("version") != expected:
        return False
    uptime = status.get("uptime_ms")
    return isinstance(uptime, int) and uptime <= since_reboot_s * 1000 + 2000


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


# ---- the SD bench log (/.vaultink/bench.csv, written by the reader itself; no port needed) ----

BENCH_FILE_COLUMNS = [
    "epoch",
    "uptime_ms",
    "battery_mv",
    "battery_percent",
    "usb",
    "light_on",
    "light_brightness",
    "light_warmth",
    "cpu_mhz",
    "heap_free",
    "heap_min",
    "psram_free",
    "activity",
    "wifi",
    "ble",
    "wake_cause",
]
BENCH_GROUPS = ["light off", "light on", "light changed", "cable in"]


def parse_bench_file(lines):
    """The rows of a bench.csv as dicts (numbers as int, activity as text), and how many lines were
    skipped. A header line is not a row, wherever it is (two files may be joined); a line with the
    wrong number of fields or a field that is not a number (a write cut by a power loss) is skipped."""
    rows = []
    skipped = 0
    for cells in csv.reader(lines):
        if not cells or cells == BENCH_FILE_COLUMNS:
            continue
        if len(cells) != len(BENCH_FILE_COLUMNS):
            skipped += 1
            continue
        row = {}
        try:
            for column, cell in zip(BENCH_FILE_COLUMNS, cells):
                row[column] = cell if column == "activity" else int(cell)
        except ValueError:
            skipped += 1
            continue
        rows.append(row)
    return rows, skipped


def bench_stretches(rows):
    """One stretch for every two rows in a row that the reader stayed awake between, and the number
    of breaks. uptime_ms starts again at every boot and a wake from sleep is a boot, so a row whose
    uptime is not above the one before starts a new run: that pair is a break, not a stretch. The
    time of a stretch comes from uptime_ms, not from the clock, which may be unset (epoch 0) or
    corrected by a sync."""
    stretches = []
    breaks = 0
    for before, after in zip(rows, rows[1:]):
        elapsed_ms = after["uptime_ms"] - before["uptime_ms"]
        if elapsed_ms <= 0:
            breaks += 1
            continue
        hours = elapsed_ms / 3_600_000
        if before["usb"] or after["usb"]:
            group = "cable in"  # the cable feeds the reader: not a drain figure
        elif before["light_on"] and after["light_on"]:
            group = "light on"
        elif not before["light_on"] and not after["light_on"]:
            group = "light off"
        else:
            group = "light changed"
        percent_drop = before["battery_percent"] - after["battery_percent"]
        mv_drop = before["battery_mv"] - after["battery_mv"]
        stretches.append(
            {
                "epoch": before["epoch"],
                "uptime_ms": before["uptime_ms"],
                "minutes": elapsed_ms / 60_000,
                "hours": hours,
                "group": group,
                "percent_from": before["battery_percent"],
                "percent_to": after["battery_percent"],
                "mv_from": before["battery_mv"],
                "mv_to": after["battery_mv"],
                "percent_drop": percent_drop,
                "mv_drop": mv_drop,
                "percent_per_hour": percent_drop / hours,
                "mv_per_hour": mv_drop / hours,
                "activity": after["activity"],
            }
        )
    return stretches, breaks


def bench_groups(stretches):
    """Per group: the stretches, their minutes, and the drop per hour over all of them together
    (the whole drop over the whole time, so a long stretch counts for more than a short one)."""
    groups = []
    for name in BENCH_GROUPS:
        members = [stretch for stretch in stretches if stretch["group"] == name]
        if not members:
            continue
        hours = sum(stretch["hours"] for stretch in members)
        groups.append(
            {
                "group": name,
                "stretches": len(members),
                "minutes": hours * 60,
                "percent_per_hour": sum(stretch["percent_drop"] for stretch in members) / hours,
                "mv_per_hour": sum(stretch["mv_drop"] for stretch in members) / hours,
            }
        )
    return groups


def _is_number(text: str) -> bool:
    try:
        float(text)
    except ValueError:
        return False
    return True


def text_table(header, lines) -> str:
    """Plain columns: text to the left, numbers to the right."""
    table = [header] + lines
    widths = [max(len(row[i]) for row in table) for i in range(len(header))]
    numeric = [all(_is_number(row[i]) for row in lines) for i in range(len(header))]
    out = []
    for row in table:
        cells = [cell.rjust(widths[i]) if numeric[i] else cell.ljust(widths[i]) for i, cell in enumerate(row)]
        out.append("  ".join(cells).rstrip())
    return "\n".join(out)


def bench_file_report(lines, name: str = "bench.csv") -> str:
    """The summary `bench-file` prints. A drop is positive when the battery went down."""
    rows, skipped = parse_bench_file(lines)
    stretches, breaks = bench_stretches(rows)
    out = [
        f"{name}: {len(rows)} rows, {len(stretches)} awake stretches, "
        f"{breaks} breaks (sleep or restart), {skipped} lines skipped"
    ]
    if not stretches:
        out.append("No two rows from the same time awake: nothing to work out.")
        return "\n".join(out)
    out.append("")
    out.append("Drop per hour while awake (positive: the battery went down)")
    out.append(
        text_table(
            ["state", "stretches", "minutes", "percent/h", "mV/h"],
            [
                [
                    group["group"],
                    str(group["stretches"]),
                    f"{group['minutes']:.1f}",
                    f"{group['percent_per_hour']:.2f}",
                    f"{group['mv_per_hour']:.1f}",
                ]
                for group in bench_groups(stretches)
            ],
        )
    )
    out.append("")
    out.append("Every stretch")
    stretch_lines = []
    for stretch in stretches:
        if stretch["epoch"] > 0:
            start = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(stretch["epoch"]))
        else:
            start = f"up {stretch['uptime_ms'] / 60_000:.1f} min"
        stretch_lines.append(
            [
                start,
                f"{stretch['minutes']:.1f}",
                stretch["group"],
                f"{stretch['percent_from']}>{stretch['percent_to']}",
                f"{stretch['mv_from']}>{stretch['mv_to']}",
                f"{stretch['percent_per_hour']:.2f}",
                f"{stretch['mv_per_hour']:.1f}",
                stretch["activity"],
            ]
        )
    out.append(
        text_table(["start", "minutes", "state", "percent", "mV", "percent/h", "mV/h", "screen"], stretch_lines)
    )
    return "\n".join(out)


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


def command_lines(port, text: str, timeout: float = REPLY_TIMEOUT_S, on_line=None):
    """Like command(), for LS and FLASH: returns the reply and the JSON lines that came before it
    (entries, progress). `on_line` is called with each of those as it arrives."""
    port.drain()
    port.write(f"\nCMD:{text}\n".encode())
    deadline = time.monotonic() + timeout
    lines = []
    while True:
        if time.monotonic() >= deadline:
            raise LinkError("no reply in time (is this the x4pro-dev build, awake and out of USB Drive mode?)")
        line = parse_json_line(port.read_line(deadline))
        if line is None:
            continue
        if isinstance(line.get("ok"), bool):
            return line, lines
        lines.append(line)
        if on_line:
            on_line(line)


def get_file(port, path: str) -> tuple[dict, bytes]:
    """Read a whole file with CMD:GET, 2880 bytes a time. Returns the last reply and the bytes; the
    reply has ok false when the reader refused. A reply that is cut or mixed with a log line is
    asked for again, three times at most."""
    path = sd_path(path)
    data = b""
    while True:
        for attempt in range(CHUNK_TRIES):
            try:
                reply = command(port, f"GET {path} {len(data)} {GET_CHUNK_BYTES}")
                if not reply.get("ok"):
                    return reply, data
                part = get_data(reply, len(data))
                break
            except LinkError:
                if attempt == CHUNK_TRIES - 1:
                    raise
        data += part
        if reply.get("eof") or not part:
            return reply, data


def put_file(port, path: str, data: bytes) -> dict:
    """Write a whole file with CMD:PUT. Returns the last reply; ok false stops it there."""
    reply = {"ok": False, "error": "nothing sent"}
    for text in put_commands(path, data):
        reply = command(port, text)
        if not reply.get("ok"):
            break
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


def wait(port, activity: str = "", timeout_ms: int = DEFAULT_WAIT_MS) -> dict:
    """CMD:WAIT: the reply comes when the reader is at rest, or with ok false and error "timeout"
    when the time is up. The host waits three seconds longer than the reader."""
    return command(port, wait_command(activity, timeout_ms), timeout_ms / 1000 + REPLY_TIMEOUT_S)


def screenshot_checked(port, tries: int = SCREENSHOT_TRIES, timeout: float = SCREENSHOT_TIMEOUT_S):
    """A screenshot whose bytes match CMD:SHOTCRC (the CRC-32 of the reader's framebuffer), taken
    again when bytes were lost or the numbers differ (a frame drawn between the two also makes them
    differ). Returns the bytes and the number of tries it took. Needs the developer's own build."""
    problem = "no try made"
    for attempt in range(1, tries + 1):
        try:
            data = screenshot(port, timeout)
            reply = command(port, "SHOTCRC")
        except LinkError as error:
            problem = str(error)
            continue
        if not reply.get("ok"):
            raise LinkError(f"SHOTCRC refused: {reply.get('error', reply)} (is this the x4pro-dev build?)")
        if reply.get("bytes") == len(data) and reply.get("crc32") == zlib.crc32(data):
            return data, attempt
        problem = "the bytes do not match the reader's CRC"
    raise LinkError(f"no good screenshot in {tries} tries: {problem}")


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


# ---- FETCH and push-update: the parts with I/O ----


def lan_ip() -> str:
    """The address this computer uses on its network. No packet is sent: a UDP socket is only
    pointed at an address, and the system says which of its own it would use."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.168.255.254", 9))
        address = probe.getsockname()[0]
    except OSError as error:
        raise LinkError(f"cannot find this computer's network address ({error}); name it with --host-ip") from error
    finally:
        probe.close()
    if not private_ipv4(address):
        raise LinkError(
            f"this computer's address looks like {address}, which is not a private LAN address; "
            "name the one on the reader's network with --host-ip"
        )
    return address


def free_port(address: str, port: int = 0) -> int:
    """A port of this address that nothing listens on: `port` itself, or one the system picks for 0."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            probe.bind((address, port))
        except OSError as error:
            what = f"port {port} of {address}" if port else address
            raise LinkError(f"cannot serve on {what}: {error}") from error
        return probe.getsockname()[1]


class FileServer:
    """`python3 -m http.server` bound to one address, serving a temporary folder that holds a copy
    of one file and nothing else. Stopped, and the folder removed, by close() or at the end of a
    `with`."""

    def __init__(self, file: str, address: str, port: int = 0, name: str = PUSH_SERVED_NAME, ready_s: float = 10.0):
        self.address = address
        self.port = free_port(address, port)
        self.name = name
        self.folder = tempfile.mkdtemp(prefix="devlink-push-")
        self.process = None
        try:
            shutil.copyfile(file, os.path.join(self.folder, name))
            self.process = subprocess.Popen(  # pylint: disable=consider-using-with
                [sys.executable, "-m", "http.server", str(self.port), "--bind", address, "--directory", self.folder],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            self._wait_ready(ready_s)
        except BaseException:
            self.close()
            raise

    @property
    def url(self) -> str:
        return f"http://{self.address}:{self.port}/{self.name}"

    def _wait_ready(self, ready_s: float) -> None:
        deadline = time.monotonic() + ready_s
        while True:
            if self.process.poll() is not None:
                raise LinkError(
                    f"the file server did not start on {self.address}:{self.port} "
                    f"(python3 -m http.server ended with code {self.process.returncode})"
                )
            try:
                socket.create_connection((self.address, self.port), timeout=0.5).close()
                return
            except OSError:
                if time.monotonic() >= deadline:
                    raise LinkError(f"the file server on {self.address}:{self.port} does not answer") from None
                time.sleep(0.1)

    def close(self) -> None:
        if self.process is not None:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
            self.process = None
        shutil.rmtree(self.folder, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def wait_for_version(
    path, expected: str, timeout: float, rebooted_at: float, poll=None, sleep=time.sleep, clock=time.monotonic, say=print
):
    """Poll STATUS until the reader answers as `expected`, started after the reboot. Returns that
    STATUS; UpdateError with what was last seen when the time is up."""
    poll = poll or status_once
    deadline = rebooted_at + timeout
    last = None
    while True:
        status = poll(path)
        now = clock()
        if status and status.get("ok"):
            last = status
            if booted_as(status, expected, now - rebooted_at):
                return status
        if now >= deadline:
            break
        say(f"  waiting for the reader to start ({int(now - rebooted_at)} s)...")
        sleep(PUSH_POLL_S)
    if last is None:
        raise UpdateError(
            f"the reader did not answer for {int(timeout)} s after REBOOT: is it awake, and is the port back?"
        )
    if last.get("version") != expected:
        raise UpdateError(
            f"the reader runs {last.get('version')!r}, not {expected!r}: the new image did not start "
            "(or --expect is not the version of this file)"
        )
    raise UpdateError(f"the reader still answers as before the REBOOT (uptime {last.get('uptime_ms')} ms)")


def push_update(
    port_path,
    file: str,
    expected: str,
    sd: str = PUSH_SD_PATH,
    host_ip=None,
    http_port: int = 0,
    boot_timeout: float = PUSH_BOOT_TIMEOUT_S,
    serve=FileServer,
    find_ip=lan_ip,
    say=print,
    sleep=time.sleep,
) -> dict:
    """Send a local .bin to the reader and start it: serve the file on this computer's LAN address,
    CMD:FETCH, CMD:FLASH, CMD:REBOOT, then STATUS until the version is `expected`. Returns the last
    STATUS. ValueError before anything is sent, LinkError for the port, UpdateError for a step the
    reader refused."""
    digest, size = check_push_file(file)
    sd = sd_path(sd)
    if not expected:
        raise ValueError("--expect needs the version the new build reports in STATUS")
    if host_ip is not None and not private_ipv4(host_ip):
        raise ValueError("--host-ip must be this computer's address in 10/8, 172.16/12 or 192.168/16")
    say(f"1/5 {file}: {size} bytes, sha256 {digest}")

    port = open_port(port_path)
    try:
        status = command(port, "STATUS")
        check_push_status(status)
        say(f"    the reader runs {status.get('version')}, battery {status.get('battery_percent')}%")
        address = host_ip or find_ip()
        with serve(file, address, http_port) as server:
            text = fetch_command(server.url, sd, digest)
            say(f"2/5 FETCH {server.url} -> {sd}")
            reply, _ = command_lines(port, text, FETCH_TIMEOUT_S, lambda line: _say_progress(say, "fetch", line))
        check_fetch_reply(reply, digest, size)
        say(f"3/5 FLASH {sd}")
        reply, _ = command_lines(port, "FLASH " + sd, FLASH_TIMEOUT_S, lambda line: _say_progress(say, "flash", line))
        if not reply.get("ok"):
            reason = f" ({reply['reason']})" if reply.get("reason") else ""
            raise UpdateError(f"FLASH failed: {reply.get('error', reply)}{reason}. The reader keeps its firmware")
        say("4/5 REBOOT")
        reply = command(port, "REBOOT")
        if not reply.get("ok"):
            raise UpdateError(f"REBOOT failed: {reply.get('error', reply)}. The image is written: restart the reader")
        rebooted_at = time.monotonic()
    finally:
        port.close()
    say(f"5/5 waiting for version {expected}")
    status = wait_for_version(port_path, expected, boot_timeout, rebooted_at, sleep=sleep, say=say)
    say(f"Done: the reader runs {status.get('version')} (up {status.get('uptime_ms')} ms).")
    return status


def _say_progress(say, kind: str, line: dict) -> None:
    if "percent" in line:
        say(f"    {kind}: {line.get('percent')}% ({line.get('written')} of {line.get('total')} bytes)")
    elif line.get(kind):
        say(f"    {kind}: {line.get(kind)}")


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
    shot.add_argument("--verify", action="store_true", help="check against CMD:SHOTCRC, three tries (x4pro-dev)")
    waiting = sub.add_parser("wait", help="wait until the reader is at rest (x4pro-dev)")
    waiting.add_argument("--activity", default="", help="also wait for this screen, named as STATUS names it")
    waiting.add_argument("--timeout", type=int, default=DEFAULT_WAIT_MS, help="milliseconds, 100 to 60000 (5000)")
    run = sub.add_parser("bench", help="poll STATUS into a CSV file")
    run.add_argument("--every", type=float, default=60, help="seconds between polls (60)")
    run.add_argument("--minutes", type=float, default=30, help="how long to run (30)")
    run.add_argument("--out", required=True, help="CSV file, appended to")
    summary = sub.add_parser("bench-file", help="summarise a bench.csv copied from the SD card (no port needed)")
    summary.add_argument("file")
    key = sub.add_parser("key", help="press a key: " + " ".join(KEY_NAMES) + " (never POWER)")
    key.add_argument("name")
    key.add_argument("--hold", type=int, help="milliseconds held (120)")
    tap = sub.add_parser("tap", help="touch a screen pixel (as a screenshot shows it)")
    tap.add_argument("x", type=int)
    tap.add_argument("y", type=int)
    tap.add_argument("--hold", type=int, help="milliseconds held (80); 500 or more is a long press")
    swipe = sub.add_parser("swipe", help="swipe from one screen pixel to another")
    for name in ("x1", "y1", "x2", "y2"):
        swipe.add_argument(name, type=int)
    swipe.add_argument("--ms", type=int, help="how long the swipe takes (250); over 700 is a drag")
    listing = sub.add_parser("ls", help="list a folder of the SD card (/debug, /screenshots, /.vaultink)")
    listing.add_argument("dir")
    get = sub.add_parser("get", help="copy a file from the SD card")
    get.add_argument("sd_path")
    get.add_argument("file", help="where to save it on this computer")
    put = sub.add_parser("put", help="copy a file to the SD card (/debug/..., or /.vaultink/bench.cfg)")
    put.add_argument("file", help="the file on this computer")
    put.add_argument("sd_path")
    flash = sub.add_parser(
        "flash",
        help="flash a .bin that is already on the SD card; nothing is copied over the link, and the "
        "reader does not restart: send `cmd REBOOT` afterwards",
    )
    flash.add_argument("sd_path", help="the path of the .bin on the reader's SD card, e.g. /firmware.bin")
    fetch = sub.add_parser(
        "fetch",
        help="the reader downloads a .bin to its SD card over Wi-Fi: plain http from a private IPv4 "
        "address only, checked against the SHA-256 (x4pro-dev)",
    )
    fetch.add_argument("url", help="e.g. http://192.168.1.20:8000/firmware.bin")
    fetch.add_argument("sd_path", help="where on the reader's SD card, a .bin, e.g. /firmware-dev.bin")
    fetch.add_argument("sha256", help="the file's SHA-256, 64 hex digits")
    push = sub.add_parser(
        "push-update",
        help="send a local .bin and start it: serves the file on this computer, then FETCH, FLASH, "
        "REBOOT and STATUS until the version matches (x4pro-dev, reader already on Wi-Fi)",
    )
    push.add_argument("file", help="the .bin on this computer, e.g. .pio/build/x4pro-dev/firmware.bin")
    push.add_argument("--expect", required=True, help="the version the new build reports, e.g. 0.75.0-x4pro-dev")
    push.add_argument("--sd-path", default=PUSH_SD_PATH, help=f"where on the reader's SD card ({PUSH_SD_PATH})")
    push.add_argument("--host-ip", help="this computer's address on the reader's network (found when left out)")
    push.add_argument("--http-port", type=int, default=0, help="the port to serve on (a free one)")
    push.add_argument(
        "--boot-timeout", type=float, default=PUSH_BOOT_TIMEOUT_S, help="seconds to wait for the new version (90)"
    )
    args = parser.parse_args(argv)

    if args.action == "bench-file":
        try:
            with open(args.file, newline="", encoding="utf-8", errors="replace") as handle:
                print(bench_file_report(handle, os.path.basename(args.file)))
        except OSError as error:
            print(f"devlink: {error}", file=sys.stderr)
            return 2
        return 0
    try:
        if args.action == "bench":
            if args.every <= 0 or args.minutes <= 0:
                parser.error("--every and --minutes must be above 0")
            bench(args.port, args.every, args.minutes, args.out)
            return 0
        if args.action == "push-update":
            try:
                push_update(
                    args.port, args.file, args.expect, args.sd_path, args.host_ip, args.http_port, args.boot_timeout
                )
            except UpdateError as error:
                print(f"devlink: {error}", file=sys.stderr)
                return 1
            return 0
        if args.action == "screenshot" and not args.file.lower().endswith((".png", ".bmp")):
            parser.error("the output file must end in .png or .bmp")
        # What can be wrong before the port is opened: the command text, the local file.
        text = None
        payload = b""
        if args.action == "key":
            text = key_command(args.name, args.hold)
        elif args.action == "tap":
            text = tap_command(args.x, args.y, args.hold)
        elif args.action == "swipe":
            text = swipe_command(args.x1, args.y1, args.x2, args.y2, args.ms)
        elif args.action == "wait":
            wait_command(args.activity, args.timeout)
        elif args.action == "ls":
            text = "LS " + sd_path(args.dir)
        elif args.action == "flash":
            if not args.sd_path.startswith("/"):
                raise ValueError(
                    "flash takes the path of a .bin on the reader's SD card (e.g. /firmware.bin). Nothing is "
                    "copied over the link: put the file on the card first (USB Drive mode or a card reader)"
                )
            text = "FLASH " + sd_path(args.sd_path)
        elif args.action == "fetch":
            text = fetch_command(args.url, args.sd_path, args.sha256)
        elif args.action == "get":
            sd_path(args.sd_path)
        elif args.action == "put":
            with open(args.file, "rb") as handle:
                payload = handle.read()
            put_commands(args.sd_path, payload)
        port = open_port(args.port)
        try:
            if args.action == "screenshot":
                data = screenshot_checked(port)[0] if args.verify else screenshot(port)
                picture = encode_picture(args.file, data, args.width, args.height)
                with open(args.file, "wb") as handle:
                    handle.write(picture)
                print(f"{args.file}: {len(data)} bytes from the device")
                return 0
            if args.action == "wait":
                reply = wait(port, args.activity, args.timeout)
            elif args.action == "ls":
                reply, entries = command_lines(port, text)
                for entry in entries:
                    size = "dir" if entry.get("dir") else str(entry.get("size", ""))
                    print(f"{size:>10}  {entry.get('name', '')}")
            elif args.action in ("flash", "fetch"):
                reply, _ = command_lines(
                    port,
                    text,
                    FLASH_TIMEOUT_S if args.action == "flash" else FETCH_TIMEOUT_S,
                    lambda line: print(json.dumps(line, ensure_ascii=False), flush=True),
                )
            elif args.action == "get":
                reply, data = get_file(port, args.sd_path)
                if reply.get("ok"):
                    with open(args.file, "wb") as handle:
                        handle.write(data)
                    print(f"{args.file}: {len(data)} bytes from {args.sd_path}")
                    return 0
            elif args.action == "put":
                reply = put_file(port, args.sd_path, payload)
            elif text is not None:
                reply = command(port, text)
            else:
                reply = command(port, "STATUS" if args.action == "status" else " ".join(args.words))
        finally:
            port.close()
    except OSError as error:
        print(f"devlink: {error}", file=sys.stderr)
        return 2
    except (LinkError, ValueError) as error:
        print(f"devlink: {error}", file=sys.stderr)
        return 2
    if args.action == "status" and reply.get("ok"):
        for key, value in reply.items():
            print(f"{key}: {value}")
    else:
        print(json.dumps(reply, ensure_ascii=False))
    if args.action == "flash" and reply.get("ok"):
        print("Written, not started: run `devlink.py cmd REBOOT` to start the new firmware.")
    if args.action == "fetch" and reply.get("ok"):
        print(f"On the card, not flashed: run `devlink.py flash {args.sd_path}`, then `devlink.py cmd REBOOT`.")
    return 0 if reply.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
