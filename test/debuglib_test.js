// Tests of site/debuglib.js (the debug page's parsing, picture and redaction functions).
// No packages, no network, no browser:
//
//   node test/debuglib_test.js
//
// The exit code is 0 when every case passes.
"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const path = require("node:path");
const lib = require(path.join(__dirname, "..", "site", "debuglib.js"));

let passed = 0;
const failures = [];
function test(name, body) {
  try {
    body();
    ++passed;
  } catch (error) {
    failures.push(name + ": " + (error && error.message ? error.message : error));
  }
}

const bytes = (text) => new TextEncoder().encode(text);
function concat(parts) {
  const out = new Uint8Array(parts.reduce((total, part) => total + part.length, 0));
  let at = 0;
  for (const part of parts) {
    out.set(part, at);
    at += part.length;
  }
  return out;
}
// Feeds `data` to a new parser in pieces of `size` bytes and returns what came out.
function run(data, size) {
  const out = { lines: [], shots: [], errors: [] };
  const parser = lib.createStreamParser({
    onLine: (line) => out.lines.push(line),
    onScreenshot: (shot) => out.shots.push(shot),
    onError: (message) => out.errors.push(message),
  });
  for (let at = 0; at < data.length; at += size) parser.feed(data.subarray(at, Math.min(at + size, data.length)));
  out.parser = parser;
  return out;
}
// The test framebuffer: the same bytes scripts/devlink.py was given for the hash below.
function testFramebuffer() {
  const data = new Uint8Array(48000);
  for (let i = 0; i < data.length; ++i) data[i] = (i * 31 + (i >> 8) * 7) & 0xff;
  return data;
}

// ---- lines and replies ----

test("parseJsonLine: an object, or null", () => {
  assert.deepEqual(lib.parseJsonLine('{"ok":true,"pong":true}'), { ok: true, pong: true });
  assert.deepEqual(lib.parseJsonLine('  {"name":"a.txt","dir":false}\r'), { name: "a.txt", dir: false });
  assert.equal(lib.parseJsonLine("[12] [INF] [MAIN] hello"), null);
  assert.equal(lib.parseJsonLine('{"ok":true'), null);
  assert.equal(lib.parseJsonLine("{not json}"), null);
  assert.equal(lib.parseJsonLine("[1,2]"), null);
  assert.equal(lib.parseJsonLine(""), null);
});

test("parseReply: only a line with a boolean ok is a reply", () => {
  assert.deepEqual(lib.parseReply('{"ok":false,"error":"unknown"}'), { ok: false, error: "unknown" });
  assert.equal(lib.parseReply('{"flash":"writing","percent":40}'), null);
  assert.equal(lib.parseReply('{"ok":"yes"}'), null);
  assert.equal(lib.parseReply("SCREENSHOT_END"), null);
});

test("parseScreenshotStart", () => {
  assert.equal(lib.parseScreenshotStart("SCREENSHOT_START:48000"), 48000);
  assert.equal(lib.parseScreenshotStart("SCREENSHOT_START:48000\r"), 48000);
  assert.equal(lib.parseScreenshotStart("SCREENSHOT_START:"), null);
  assert.equal(lib.parseScreenshotStart("SCREENSHOT_START:12x"), null);
  assert.equal(lib.parseScreenshotStart("SCREENSHOT_START:-5"), null);
  assert.equal(lib.parseScreenshotStart("x SCREENSHOT_START:5"), null);
  assert.equal(lib.parseScreenshotStart("SCREENSHOT_END"), null);
});

test("commandBytes: a bare newline, the command, a newline", () => {
  assert.equal(new TextDecoder().decode(lib.commandBytes("LOG 2")), "\nCMD:LOG 2\n");
});

// ---- the stream ----

test("stream: partial chunks give whole lines, in any chunk size", () => {
  const data = bytes('[1] [INF] boot\n{"ok":true,"pong":true}\n\nlast\nrest without end');
  for (const size of [1, 2, 3, 7, 1000]) {
    const out = run(data, size);
    assert.deepEqual(out.lines, ["[1] [INF] boot", '{"ok":true,"pong":true}', "", "last"], "size " + size);
    assert.equal(out.shots.length, 0);
  }
});

test("stream: CRLF line ends are dropped", () => {
  const out = run(bytes("one\r\ntwo\r\n"), 3);
  assert.deepEqual(out.lines, ["one", "two"]);
});

test("stream: a UTF-8 character cut between two chunks is kept whole", () => {
  const data = bytes("Σημείωση.md\n");
  for (const size of [1, 2, 5]) assert.deepEqual(run(data, size).lines, ["Σημείωση.md"]);
});

test("stream: a bad byte becomes a replacement character, not an exception", () => {
  const out = run(Uint8Array.from([0x61, 0xff, 0x62, 10]), 4);
  assert.deepEqual(out.lines, ["a�b"]);
});

test("stream: a JSON reply is found among log lines", () => {
  const out = run(bytes('[5] [DBG] [X] {not a reply}\n{"ok":true,"version":"0.74.0-dev"}\n[6] more\n'), 4);
  const replies = out.lines.map(lib.parseReply).filter((reply) => reply !== null);
  assert.deepEqual(replies, [{ ok: true, version: "0.74.0-dev" }]);
});

test("stream: a picture's bytes are taken raw, newlines and markers inside included", () => {
  const picture = testFramebuffer();
  // Bytes that look like line ends and like the end marker, inside the picture.
  picture.set(bytes("\nSCREENSHOT_END\n\r\nCMD:REBOOT\n"), 100);
  const data = concat([bytes("log before\nSCREENSHOT_START:48000\n"), picture, bytes("SCREENSHOT_END\nlog after\n")]);
  for (const size of [1, 5, 64, 4096, 100000]) {
    const out = run(data, size);
    assert.deepEqual(out.errors, [], "size " + size);
    assert.deepEqual(out.lines, ["log before", "SCREENSHOT_START:48000", "SCREENSHOT_END", "log after"]);
    assert.equal(out.shots.length, 1);
    assert.equal(out.shots[0].length, 48000);
    assert.deepEqual(Buffer.from(out.shots[0]), Buffer.from(picture));
    assert.equal(out.parser.state().mode, "lines");
  }
});

test("stream: a log line may come between the bytes and SCREENSHOT_END", () => {
  const data = concat([bytes("SCREENSHOT_START:4\r\n"), Uint8Array.from([10, 10, 13, 0]), bytes("[9] [INF] late line\nSCREENSHOT_END\r\n")]);
  const out = run(data, 3);
  assert.deepEqual(out.errors, []);
  assert.deepEqual(out.lines, ["SCREENSHOT_START:4", "[9] [INF] late line", "SCREENSHOT_END"]);
  assert.deepEqual(Array.from(out.shots[0]), [10, 10, 13, 0]);
});

test("stream: the state says how far a picture is", () => {
  const out = run(concat([bytes("SCREENSHOT_START:10\n"), Uint8Array.from([1, 2, 3])]), 50);
  assert.deepEqual(out.parser.state(), { mode: "bytes", got: 3, want: 10 });
  out.parser.feed(Uint8Array.from([4, 5, 6, 7, 8, 9, 10]));
  assert.deepEqual(out.parser.state(), { mode: "end" });
  assert.equal(out.shots.length, 0);
});

test("stream: no SCREENSHOT_END after the bytes is an error, and lines go on", () => {
  const lines = [];
  for (let i = 0; i < 30; ++i) lines.push("line " + i + "\n");
  const out = run(concat([bytes("SCREENSHOT_START:2\n"), Uint8Array.from([1, 2]), bytes(lines.join(""))]), 9);
  assert.equal(out.shots.length, 0);
  assert.equal(out.errors.length, 1);
  assert.match(out.errors[0], /SCREENSHOT_END did not follow/);
  assert.equal(out.lines.length, 31);
  assert.equal(out.parser.state().mode, "lines");
});

test("stream: a count that is not a picture is refused and the stream stays in lines", () => {
  for (const count of ["0", "4194305", "9999999999"]) {
    const out = run(bytes("SCREENSHOT_START:" + count + "\nnext\n"), 6);
    assert.equal(out.errors.length, 1, count);
    assert.deepEqual(out.lines, ["SCREENSHOT_START:" + count, "next"]);
    assert.equal(out.parser.state().mode, "lines");
  }
});

test("stream: reset drops a half picture (the reader slept, the port closed)", () => {
  const out = run(concat([bytes("half a li"), bytes("ne\nSCREENSHOT_START:48000\n"), new Uint8Array(1000)]), 11);
  assert.equal(out.parser.state().mode, "bytes");
  assert.equal(out.parser.reset(), true);
  assert.equal(out.parser.reset(), false);
  out.parser.feed(bytes('{"ok":true}\n'));
  assert.equal(out.lines[out.lines.length - 1], '{"ok":true}');
  assert.equal(out.shots.length, 0);
});

test("stream: a second picture after the first", () => {
  const one = Uint8Array.from([1, 2, 3]);
  const two = Uint8Array.from([4, 5]);
  const data = concat([bytes("SCREENSHOT_START:3\n"), one, bytes("SCREENSHOT_END\nSCREENSHOT_START:2\n"), two, bytes("SCREENSHOT_END\n")]);
  const out = run(data, 2);
  assert.deepEqual(out.shots.map((shot) => Array.from(shot)), [[1, 2, 3], [4, 5]]);
});

test("stream: a line with no end is cut at the limit, not kept forever", () => {
  const out = run(new Uint8Array(lib.MAX_LINE_BYTES * 2 + 5).fill(0x41), 1000);
  assert.equal(out.lines.length, 2);
  assert.equal(out.lines[0].length, lib.MAX_LINE_BYTES);
});

// ---- the picture ----

test("rotateFramebuffer: the same rows as scripts/devlink.py", () => {
  // sha256 of b"".join(rows) from devlink.py's rotate_framebuffer for testFramebuffer()'s bytes.
  const picture = lib.rotateFramebuffer(testFramebuffer());
  assert.equal(picture.width, 480);
  assert.equal(picture.height, 800);
  assert.equal(picture.rows.length, 800);
  assert.equal(picture.rows[0].length, 60);
  const hash = crypto.createHash("sha256");
  for (const row of picture.rows) hash.update(row);
  assert.equal(hash.digest("hex"), "ea09a54947cf558bde6c49916afbd9f90a5ca800169796c7ad1d8ad81e17482a");
});

test("rotateFramebuffer: picture pixel (x, y) is framebuffer pixel (y, height - 1 - x)", () => {
  const data = new Uint8Array(lib.FRAMEBUFFER_BYTES);
  // One white framebuffer pixel at column 13, row 470.
  data[470 * 100 + (13 >> 3)] |= 0x80 >> (13 % 8);
  const picture = lib.rotateFramebuffer(data);
  let white = 0;
  for (let y = 0; y < 800; ++y) for (let x = 0; x < 480; ++x) if ((picture.rows[y][x >> 3] >> (7 - (x % 8))) & 1) ++white;
  assert.equal(white, 1);
  const x = 480 - 1 - 470;
  assert.equal((picture.rows[13][x >> 3] >> (7 - (x % 8))) & 1, 1);
});

test("rotateFramebuffer: the wrong size is refused", () => {
  assert.throws(() => lib.rotateFramebuffer(new Uint8Array(47999)), /not a 800x480/);
  assert.throws(() => lib.rotateFramebuffer(new Uint8Array(0)));
});

test("framebufferToRgba: black and white, opaque", () => {
  const data = new Uint8Array(lib.FRAMEBUFFER_BYTES).fill(0xff);
  data[0] = 0x7f; // framebuffer pixel (0, 0) black: picture pixel (479, 0)
  const image = lib.framebufferToRgba(data);
  assert.equal(image.width, 480);
  assert.equal(image.height, 800);
  assert.equal(image.rgba.length, 480 * 800 * 4);
  assert.deepEqual(Array.from(image.rgba.subarray(479 * 4, 480 * 4)), [0, 0, 0, 255]);
  assert.deepEqual(Array.from(image.rgba.subarray(0, 4)), [255, 255, 255, 255]);
});

// ---- STATUS ----

test("statusRows: every field but ok, booleans as words, times readable", () => {
  const rows = lib.statusRows({ ok: true, version: "0.74.0-dev", build: "dev-user", uptime_ms: 125000, usb: true, charging: false, new_field: 7 });
  assert.deepEqual(rows.map((row) => row.key), ["version", "build", "uptime_ms", "usb", "charging", "new_field"]);
  assert.equal(rows[2].value, "125000 ms (2 min 5 s)");
  assert.equal(rows[3].value, "yes");
  assert.equal(rows[4].value, "no");
  assert.equal(rows[5].label, "");
  assert.deepEqual(lib.statusRows(null), []);
});

// ---- redaction ----

const redact = (text, options) => lib.redact(text, options).text;

test("redact: Wi-Fi names in the firmware's own wording", () => {
  assert.equal(redact("[WIFI] Attempting saved network: My Home Net"), "[WIFI] Attempting saved network: <ssid>");
  assert.equal(redact("[WIFI] Saved network failed: My Home Net"), "[WIFI] Saved network failed: <ssid>");
  assert.equal(redact("[WEB] Added Wi-Fi network: Cafe 5G"), "[WEB] Added Wi-Fi network: <ssid>");
  assert.equal(redact("[WEB] Deleted Wi-Fi network at index 2 (SSID: Cafe 5G)"), "[WEB] Deleted Wi-Fi network at index 2 (SSID: <ssid>)");
  assert.equal(redact("[WCS] Updated credentials for: Cafe 5G"), "[WCS] Updated credentials for: <ssid>");
  assert.equal(redact("Discarding corrupted password for Cafe 5G (checksum mismatch)"), "Discarding corrupted password for <ssid> (checksum mismatch)");
  assert.equal(redact("[WEBACT] SSID: VaultInk-Setup"), "[WEBACT] SSID: <ssid>");
});

test("redact: connected to <name>", () => {
  assert.equal(redact("Connected to Lefs Phone, IP 192.168.1.23"), "Connected to <ssid>, IP <ip>");
  assert.equal(redact("wifi connected to HomeNet"), "wifi connected to <ssid>");
  assert.equal(redact("Connected to 10.0.0.7"), "Connected to <ip>");
});

test("redact: addresses", () => {
  assert.equal(redact("[WEBACT] IP: 192.168.4.1"), "[WEBACT] IP: <ip>");
  assert.equal(redact("[FTP] Connecting to 10.0.0.5:21 (not encrypted)"), "[FTP] Connecting to <ip>:21 (not encrypted)");
  assert.equal(redact("fe80::1ff:fe23:4567:890a and 2001:db8:0:1:2:3:4:5"), "<ip> and <ip>");
  assert.equal(redact("Connected BSSID: aa:bb:cc:dd:ee:ff, channel: 6, RSSI: -60 dBm"), "Connected BSSID: <mac>, channel: 6, RSSI: -60 dBm");
});

test("redact: hosts and URLs", () => {
  assert.equal(redact("mDNS started: http://vaultink-ab12.local/"), "mDNS started: http://<host>");
  assert.equal(redact("LiveSync https://user:pw@couch.example.com:5984/db/_changes?x=1"), "LiveSync https://<host>/<path>");
  assert.equal(redact("[SFTP] Connecting to nas.example.org"), "[SFTP] Connecting to <host>");
  assert.equal(redact("[FTP] Connecting to nas:2121 (not encrypted)"), "[FTP] Connecting to <host>:2121 (not encrypted)");
  assert.equal(redact("server sync.example.org answered 200"), "server <host> answered 200");
  assert.equal(redact("write to me@lefteros.com"), "write to <email>");
});

test("redact: paths and file names, the ending kept", () => {
  assert.equal(redact("Opening /books/My Great Book.epub now"), "Opening <path>.epub now");
  assert.equal(redact("[CLIP] Failed to read clipping header: /.crosspoint/clips/abc.bin"), "[CLIP] Failed to read clipping header: <path>.bin");
  assert.equal(redact("moving: /Vaults/Work -> /Vaults/Old"), "moving: <path> -> <path>");
  assert.equal(redact("note Vaults/Work/Plan 2026.md saved"), "note <path>.md saved");
  assert.equal(redact('opened "My Book.epub" and other.pdf'), 'opened "<file>.epub" and <file>.pdf');
  assert.equal(redact('{"file":"/.vaultink/bench.csv","file_bytes":12}'), '{"file":"<path>.csv","file_bytes":12}');
  assert.equal(redact("Άνοιγμα /Σημειώσεις/Ημερολόγιο.md"), "Άνοιγμα <path>.md");
});

test("redact: what is not private stays readable", () => {
  for (const line of [
    "version 0.74.0-dev at 12:34:56",
    "[12345] [INF] [OTA] Developer build 0.74.0-dev, latest release 0.75.0: offered",
    "Serial.begin(115200) in main.cpp, e.g. heap 123456",
    "read 3/5 pages, 12 KB/s, a / b",
    "uptime_ms: 125000 ms (2 min 5 s)",
    "activity: ReaderActivity",
    "std::string and esp_reset_reason: 3",
  ]) {
    assert.equal(redact(line), line);
  }
});

test("redact: the user's own words, any case, longest first", () => {
  const out = lib.redact("Opening book: Moby Dick by MOBY fans", { extraWords: ["moby", "Moby Dick", " ", "x"] });
  assert.equal(out.text, "Opening book: <hidden> by <hidden> fans");
  assert.equal(out.counts.hidden, 2);
});

test("redact: counts, and a second pass changes nothing", () => {
  const text = "Connected to Home, IP 192.168.1.2\nOpening /books/A B.epub\nhttps://example.org/x\n";
  const once = lib.redact(text);
  assert.deepEqual(once.counts, { host: 1, path: 2, ip: 1, ssid: 1 });
  assert.equal(lib.redact(once.text).text, once.text);
});

test("redact: several lines are handled line by line", () => {
  assert.equal(redact("SSID: One\nSSID: Two Words\r\nend"), "SSID: <ssid>\nSSID: <ssid>\r\nend");
});

// ---- the report ----

test("buildReport: STATUS only by default choices", () => {
  const report = lib.buildReport({
    when: "2026-11-13 10:00",
    note: "",
    status: { ok: true, version: "0.74.0-dev", build: "dev-user", usb: true },
    includeStatus: true,
    logLines: ["secret line"],
    includeLog: false,
    lastLines: 200,
    screenshots: [],
  });
  assert.match(report, /^VaultInk debug report\n/);
  assert.match(report, /Firmware version: 0\.74\.0-dev\n/);
  assert.match(report, /== STATUS ==\nversion: 0\.74\.0-dev\nbuild: dev-user\nusb: yes\n/);
  assert.doesNotMatch(report, /secret line/);
  assert.doesNotMatch(report, /Screenshots/);
});

test("buildReport: the last N log lines, the note and the picture names", () => {
  const report = lib.buildReport({
    when: "today",
    note: " It froze. ",
    status: null,
    includeStatus: true,
    logLines: ["a", "b", "c", "d"],
    includeLog: true,
    lastLines: 2,
    screenshots: ["vaultink-screenshot-1.png"],
  });
  assert.match(report, /Firmware version: unknown/);
  assert.match(report, /== What happened ==\nIt froze\.\n/);
  assert.match(report, /\(no STATUS reply/);
  assert.match(report, /== Log: the last 2 of 4 lines ==\nc\nd\n/);
  assert.doesNotMatch(report, /\nb\n/);
  assert.match(report, /- vaultink-screenshot-1\.png\n/);
});

test("buildReport: nothing ticked leaves only the header", () => {
  const report = lib.buildReport({ when: "today", status: { ok: true, version: "1.0.0" }, includeStatus: false, includeLog: false });
  assert.equal(report, "VaultInk debug report\nMade with the VaultInk debug page on today.\nFirmware version: 1.0.0\n");
});

test("buildRedactedReport: the reader's lines and the note are redacted, the report's own words are not", () => {
  const out = lib.buildRedactedReport(
    {
      when: "11/14/2026, 10:00:00",
      note: "It froze in /books/Moby Dick.epub",
      status: { ok: true, version: "0.74.0-dev", activity: "ReaderActivity" },
      includeStatus: true,
      logLines: ["[WIFI] Attempting saved network: Home Net", "Connected to Home Net, IP 192.168.1.9", "[1] [INF] heap 1234"],
      includeLog: true,
      lastLines: 200,
      screenshots: ["vaultink-screenshot-1.png"],
    },
    { extraWords: ["froze"] }
  );
  assert.equal(
    out.text,
    [
      "VaultInk debug report",
      "Made with the VaultInk debug page on 11/14/2026, 10:00:00.",
      "Firmware version: 0.74.0-dev",
      "",
      "== What happened ==",
      "It <hidden> in <path>.epub",
      "",
      "== STATUS ==",
      "version: 0.74.0-dev",
      "activity: ReaderActivity",
      "",
      "== Log: the last 3 of 3 lines ==",
      "[WIFI] Attempting saved network: <ssid>",
      "Connected to <ssid>, IP <ip>",
      "[1] [INF] heap 1234",
      "",
      "== Screenshots ==",
      "Saved as separate files next to this report. Attach them to the issue yourself:",
      "- vaultink-screenshot-1.png",
      "A picture is not redacted: it shows what was on the screen.",
      "",
    ].join("\n")
  );
  assert.deepEqual(out.counts, { hidden: 1, path: 1, ssid: 2, ip: 1 });
});

console.log(passed + " passed, " + failures.length + " failed");
for (const failure of failures) console.log("FAILED " + failure);
process.exit(failures.length === 0 ? 0 : 1);
