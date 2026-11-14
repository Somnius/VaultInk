// Tests of site/backuplib.js (the backup and restore page's logic), with a mocked loader.
// No packages, no network, no browser, no serial port:
//
//   node test/backup_test.js
//
// The exit code is 0 when every case passes.
"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const lib = require(path.join(__dirname, "..", "site", "backuplib.js"));

let passed = 0;
const failures = [];
const pending = [];
function test(name, body) {
  pending.push(
    Promise.resolve()
      .then(body)
      .then(
        () => ++passed,
        (error) => failures.push(name + ": " + (error && error.message ? error.message : error))
      )
  );
}

// A loader and a transport that write down what they are asked, in one shared list.
function mock(options) {
  const o = options || {};
  const calls = [];
  const loader = {
    secureDownloadMode: Boolean(o.secure),
    chip: {
      CHIP_NAME: o.chipName || "ESP32-S3",
      readMac: async () => o.mac || "aa:bb:cc:dd:ee:ff",
    },
    main: async () => {
      calls.push(["main"]);
      return "ESP32-S3 (QFN56) (revision v0.2)";
    },
    detectFlashSize: async () => (o.flashSize === undefined ? "16MB" : o.flashSize),
    readFlash: async (offset, size, onPacket) => {
      calls.push(["readFlash", offset, size]);
      const returned = o.shortAt === offset ? size - 1 : size;
      const out = new Uint8Array(returned).fill((offset / (o.windowSize || lib.READ_WINDOW)) & 0xff);
      if (onPacket) {
        onPacket(out.subarray(0, 1024), Math.min(1024, returned), size);
        onPacket(out.subarray(0, 1024), returned, size);
      }
      return o.notBytes ? "timeout" : out;
    },
    writeFlash: async (flashOptions) => {
      calls.push(["writeFlash", flashOptions]);
      flashOptions.reportProgress(0, 10, 40);
      flashOptions.reportProgress(0, 40, 40);
    },
    writeReg: async (address, value, mask) => {
      calls.push(["writeReg", address, value, mask]);
      if (o.writeRegFails) throw new Error("no answer");
    },
  };
  const transport = {
    setDTR: async (state) => calls.push(["setDTR", state]),
    setRTS: async (state) => calls.push(["setRTS", state]),
  };
  const sleep = async (ms) => calls.push(["sleep", ms]);
  return { loader, transport, sleep, calls };
}

function image() {
  const data = new Uint8Array(lib.FLASH_BYTES);
  data[0] = 0xe9;
  return data;
}

// ---- window maths ----

test("16 MB in 256 kB windows is 64 whole windows", () => {
  const windows = lib.readWindows(lib.FLASH_BYTES, lib.READ_WINDOW);
  assert.equal(lib.FLASH_BYTES, 16777216);
  assert.equal(lib.READ_WINDOW, 262144);
  assert.equal(windows.length, 64);
  assert.deepEqual(windows[0], { offset: 0, size: 262144 });
  assert.deepEqual(windows[63], { offset: 16515072, size: 262144 });
  let at = 0;
  for (const win of windows) {
    assert.equal(win.offset, at);
    at += win.size;
  }
  assert.equal(at, lib.FLASH_BYTES);
});

test("a total that is not a multiple of the window ends with a short window", () => {
  assert.deepEqual(lib.readWindows(10, 4), [
    { offset: 0, size: 4 },
    { offset: 4, size: 4 },
    { offset: 8, size: 2 },
  ]);
  assert.deepEqual(lib.readWindows(0, 4), []);
  assert.deepEqual(lib.readWindows(3, 4), [{ offset: 0, size: 3 }]);
  assert.throws(() => lib.readWindows(10, 0));
  assert.throws(() => lib.readWindows(-1, 4));
  assert.throws(() => lib.readWindows(1.5, 4));
});

// ---- reading ----

test("readFlash asks for every window in order and never for all 16 MB at once", async () => {
  const m = mock();
  const seen = [];
  const chunks = await lib.readFlash(m.loader, { onProgress: (done, total) => seen.push([done, total]) });
  const reads = m.calls.filter((call) => call[0] === "readFlash");
  assert.equal(reads.length, 64);
  reads.forEach((call, index) => {
    assert.equal(call[1], index * 262144);
    assert.equal(call[2], 262144);
  });
  assert.equal(chunks.length, 64);
  assert.equal(chunks.reduce((sum, chunk) => sum + chunk.length, 0), lib.FLASH_BYTES);
  assert.equal(chunks[5][0], 5);
  // Progress counts from the start of the flash, never goes back, and ends at the total.
  for (let i = 1; i < seen.length; ++i) assert.ok(seen[i][0] >= seen[i - 1][0], "progress went back");
  assert.deepEqual(seen[0], [1024, lib.FLASH_BYTES]);
  assert.deepEqual(seen[seen.length - 1], [lib.FLASH_BYTES, lib.FLASH_BYTES]);
});

test("a short window stops the backup with the offset in the message", async () => {
  const m = mock({ shortAt: 2 * 262144 });
  await assert.rejects(
    lib.readFlash(m.loader, {}),
    /read at 0x80000 returned 262143 bytes, not 262144.*not complete/
  );
  assert.equal(m.calls.filter((call) => call[0] === "readFlash").length, 3);
});

test("a window that is not bytes stops the backup", async () => {
  const m = mock({ notBytes: true });
  await assert.rejects(lib.readFlash(m.loader, {}), /read at 0x0 returned/);
});

test("readFlash with a small total and window", async () => {
  const m = mock({ windowSize: 4 });
  const chunks = await lib.readFlash(m.loader, { total: 10, windowSize: 4 });
  assert.deepEqual(chunks.map((chunk) => chunk.length), [4, 4, 2]);
  assert.deepEqual(m.calls.map((call) => call.slice(0, 3)), [
    ["readFlash", 0, 4],
    ["readFlash", 4, 4],
    ["readFlash", 8, 2],
  ]);
});

// ---- the device check ----

test("only an ESP32-S3 with 16 MB passes", () => {
  assert.deepEqual(lib.checkDevice({ chipName: "ESP32-S3", flashSize: "16MB" }), { ok: true, reason: "" });
  assert.match(lib.checkDevice({ chipName: "ESP32-C3", flashSize: "16MB" }).reason, /This is ESP32-C3, not an ESP32-S3/);
  assert.match(lib.checkDevice({ chipName: "ESP32-S3", flashSize: "8MB" }).reason, /flash is 8MB, not 16MB/);
  assert.match(lib.checkDevice({ chipName: "ESP32-S3", flashSize: "" }).reason, /unknown size/);
  assert.equal(lib.checkDevice({ chipName: "ESP32-S31", flashSize: "16MB" }).ok, false);
  assert.equal(lib.checkDevice({ chipName: "ESP32-S3 (QFN56)", flashSize: "16MB" }).ok, false);
  assert.equal(lib.checkDevice({}).ok, false);
  assert.equal(lib.checkDevice(null).ok, false);
  assert.match(
    lib.checkDevice({ chipName: "ESP32-S3", flashSize: "16MB", secureDownloadMode: true }).reason,
    /Secure Download Mode/
  );
});

test("identify reads the chip, the flash size and the MAC from the loader", async () => {
  const m = mock({ mac: "10:20:30:40:50:60" });
  assert.deepEqual(await lib.identify(m.loader), {
    chip: "ESP32-S3 (QFN56) (revision v0.2)",
    chipName: "ESP32-S3",
    flashSize: "16MB",
    mac: "10:20:30:40:50:60",
    secureDownloadMode: false,
  });
  assert.deepEqual(m.calls, [["main"]]);
  const unknown = await lib.identify(mock({ flashSize: null }).loader);
  assert.equal(unknown.flashSize, "");
  assert.equal(lib.checkDevice(unknown).ok, false);
  const secure = await lib.identify(mock({ secure: true }).loader);
  assert.equal(secure.mac, "");
  assert.equal(lib.checkDevice(secure).ok, false);
});

// ---- the file check ----

test("a backup is exactly 16,777,216 bytes and starts with 0xE9", () => {
  assert.deepEqual(lib.checkBackupFile({ size: 16777216, firstByte: 0xe9 }), { ok: true, reason: "" });
  assert.match(lib.checkBackupFile({ size: 16777215, firstByte: 0xe9 }).reason, /16,777,215 bytes.*exactly 16,777,216/);
  assert.equal(lib.checkBackupFile({ size: 16777217, firstByte: 0xe9 }).ok, false);
  assert.equal(lib.checkBackupFile({ size: 0, firstByte: undefined }).ok, false);
  assert.match(lib.checkBackupFile({ size: 16777216, firstByte: 0xff }).reason, /0xE9/);
  assert.equal(lib.checkBackupFile({ size: 16777216, firstByte: 0x00 }).ok, false);
  assert.equal(lib.checkBackupFile(null).ok, false);
});

// ---- names ----

test("the backup's name has the MAC without colons and the local date", () => {
  assert.equal(
    lib.backupFilename("AA:BB:CC:0d:0e:0F", new Date(2026, 10, 14, 23, 59)),
    "vaultink-x4pro-backup-aabbcc0d0e0f-2026-11-14.bin"
  );
  assert.equal(
    lib.backupFilename("aabbccddeeff", new Date(2027, 0, 5, 0, 0)),
    "vaultink-x4pro-backup-aabbccddeeff-2027-01-05.bin"
  );
  assert.throws(() => lib.backupFilename("aa:bb", new Date()), /bad MAC/);
  assert.throws(() => lib.backupFilename("", new Date()), /bad MAC/);
});

test("the MAC is read back from a name, also from a browser's renamed copy", () => {
  assert.equal(lib.macFromFilename("vaultink-x4pro-backup-aabbcc0d0e0f-2026-11-14.bin"), "aabbcc0d0e0f");
  assert.equal(lib.macFromFilename("vaultink-x4pro-backup-AABBCC0D0E0F-2026-11-14 (1).bin"), "aabbcc0d0e0f");
  assert.equal(lib.macFromFilename("stock.bin"), null);
  assert.equal(lib.macFromFilename("vaultink-x4pro-backup-aabbcc-2026-11-14.bin"), null);
  assert.equal(lib.macFromFilename(null), null);
});

test("MAC mismatch: same, different, unknown", () => {
  const name = lib.backupFilename("aa:bb:cc:dd:ee:ff", new Date(2026, 10, 14));
  assert.equal(lib.compareMac(name, "AA:BB:CC:DD:EE:FF"), "same");
  assert.equal(lib.compareMac(name, "aa:bb:cc:dd:ee:00"), "different");
  assert.equal(lib.compareMac("my-old-reader.bin", "aa:bb:cc:dd:ee:ff"), "unknown");
  assert.equal(lib.compareMac(name, ""), "unknown");
  assert.equal(lib.compareMac(name, null), "unknown");
});

// ---- text ----

test("bytes, rate and percent", () => {
  assert.equal(lib.formatBytes(512), "512 B");
  assert.equal(lib.formatBytes(262144), "256 kB");
  assert.equal(lib.formatBytes(16777216), "16.0 MB");
  assert.equal(lib.progressText(1048576, 16777216, 2000), "1.0 MB of 16.0 MB, 512 kB/s");
  assert.equal(lib.progressText(0, 16777216, 0), "0 B of 16.0 MB");
  assert.equal(lib.percent(0, 100), 0);
  assert.equal(lib.percent(999, 1000), 99);
  assert.equal(lib.percent(1000, 1000), 100);
  assert.equal(lib.percent(5, 0), 0);
  assert.equal(lib.percent(2000, 1000), 100);
});

// ---- hashes ----

test("md5Hex is MD5 (checked against Node's)", () => {
  const md5 = (data) => crypto.createHash("md5").update(data).digest("hex");
  assert.equal(lib.md5Hex(new Uint8Array(0)), "d41d8cd98f00b204e9800998ecf8427e");
  assert.equal(lib.md5Hex(new TextEncoder().encode("abc")), "900150983cd24fb0d6963f7d28e17f72");
  for (const length of [1, 55, 56, 57, 63, 64, 65, 119, 120, 1000, 262144 + 3]) {
    const data = new Uint8Array(length);
    for (let i = 0; i < length; ++i) data[i] = (i * 31 + length) & 0xff;
    assert.equal(lib.md5Hex(data), md5(data), "length " + length);
  }
  const whole = image();
  whole[12345] = 7;
  assert.equal(lib.md5Hex(whole), md5(whole));
});

test("sha256Hex is SHA-256 (checked against Node's)", async () => {
  const data = new TextEncoder().encode("abc");
  assert.equal(
    await lib.sha256Hex(data),
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
  );
  assert.equal(await lib.sha256Hex(data.buffer), crypto.createHash("sha256").update(data).digest("hex"));
});

// ---- writing ----

test("the write options: offset 0, keep, compressed, MD5 check, no erase-all", () => {
  const data = image();
  const seen = [];
  const options = lib.writeOptions(data, (written, total) => seen.push([written, total]));
  assert.equal(options.fileArray.length, 1);
  assert.equal(options.fileArray[0].address, 0);
  assert.equal(options.fileArray[0].data, data);
  assert.equal(options.flashMode, "keep");
  assert.equal(options.flashFreq, "keep");
  assert.equal(options.flashSize, "keep");
  assert.equal(options.compress, true);
  assert.equal(options.eraseAll, false);
  assert.equal(options.calculateMD5Hash, lib.md5Hex);
  options.reportProgress(0, 5, 9);
  assert.deepEqual(seen, [[5, 9]]);
  assert.deepEqual(Object.keys(options).sort(), [
    "calculateMD5Hash", "compress", "eraseAll", "fileArray", "flashFreq", "flashMode", "flashSize", "reportProgress",
  ]);
});

test("restore passes those options to the loader once", async () => {
  const m = mock();
  const data = image();
  const seen = [];
  await lib.restore(m.loader, data, (written, total) => seen.push([written, total]));
  assert.equal(m.calls.length, 1);
  assert.equal(m.calls[0][0], "writeFlash");
  const options = m.calls[0][1];
  assert.equal(options.fileArray[0].data, data);
  assert.equal(options.fileArray[0].address, 0);
  assert.equal(options.compress, true);
  assert.equal(options.flashSize, "keep");
  assert.equal(typeof options.calculateMD5Hash(new Uint8Array(4)), "string");
  assert.deepEqual(seen, [[10, 40], [40, 40]]);
});

test("restore writes nothing when the file is wrong", async () => {
  const m = mock();
  const short = image().subarray(0, lib.FLASH_BYTES - 4096);
  await assert.rejects(lib.restore(m.loader, short), /exactly 16,777,216/);
  const noMagic = image();
  noMagic[0] = 0;
  await assert.rejects(lib.restore(m.loader, noMagic), /0xE9/);
  await assert.rejects(lib.restore(m.loader, null), /not read/);
  await assert.rejects(lib.restore(m.loader, new ArrayBuffer(lib.FLASH_BYTES)), /not read/);
  assert.deepEqual(m.calls, []);
});

// ---- reset ----

test("reset: clear the bit, DTR off, then one RTS pulse, in that order", async () => {
  const m = mock();
  const result = await lib.resetToFirmware(m.loader, m.transport, m.sleep);
  assert.deepEqual(m.calls, [
    ["writeReg", 0x6000812c, 0, 0x1],
    ["setDTR", false],
    ["setRTS", true],
    ["sleep", 100],
    ["setRTS", false],
  ]);
  assert.deepEqual(result, { cleared: true });
});

test("reset: the pulse still happens when the register write fails", async () => {
  const m = mock({ writeRegFails: true });
  const result = await lib.resetToFirmware(m.loader, m.transport, m.sleep);
  assert.deepEqual(m.calls.slice(1), [["setDTR", false], ["setRTS", true], ["sleep", 100], ["setRTS", false]]);
  assert.deepEqual(result, { cleared: false });
});

test("reset never asserts DTR", async () => {
  const m = mock();
  await lib.resetToFirmware(m.loader, m.transport, m.sleep);
  assert.equal(m.calls.some((call) => call[0] === "setDTR" && call[1] === true), false);
  assert.deepEqual(m.calls[m.calls.length - 1], ["setRTS", false]);
});

// ---- the page's state ----

test("at the start only Connect and the file picker work", () => {
  assert.deepEqual(lib.controls(lib.initialState(true)), {
    connect: true, restart: false, read: false, pickFile: true, agree: false, write: false,
  });
});

test("without Web Serial nothing that needs the reader works", () => {
  const c = lib.controls(lib.initialState(false));
  assert.equal(c.connect, false);
  assert.equal(c.read, false);
  assert.equal(c.write, false);
});

test("a refused device can be restarted but not read or written", () => {
  let s = lib.nextState(lib.initialState(true), { type: "connected", deviceOk: false });
  s = lib.nextState(s, { type: "file", ok: true });
  s = lib.nextState(s, { type: "agree", value: true });
  assert.deepEqual(lib.controls(s), {
    connect: false, restart: true, read: false, pickFile: true, agree: true, write: false,
  });
});

test("Write needs a connected X4 Pro, a good file and the tick, all three", () => {
  let s = lib.nextState(lib.initialState(true), { type: "connected", deviceOk: true });
  assert.equal(lib.controls(s).read, true);
  assert.equal(lib.controls(s).write, false);
  s = lib.nextState(s, { type: "file", ok: true });
  assert.equal(lib.controls(s).agree, true);
  assert.equal(lib.controls(s).write, false);
  s = lib.nextState(s, { type: "agree", value: true });
  assert.equal(lib.controls(s).write, true);
  s = lib.nextState(s, { type: "agree", value: false });
  assert.equal(lib.controls(s).write, false);
  // A good file and the tick, but no reader.
  let t = lib.nextState(lib.initialState(true), { type: "file", ok: true });
  t = lib.nextState(t, { type: "agree", value: true });
  assert.equal(lib.controls(t).write, false);
  // A bad file cannot be agreed to.
  let u = lib.nextState(lib.initialState(true), { type: "connected", deviceOk: true });
  u = lib.nextState(u, { type: "file", ok: false });
  assert.equal(lib.controls(u).agree, false);
  u = lib.nextState(u, { type: "agree", value: true });
  assert.equal(lib.controls(u).write, false);
});

test("while the reader is busy every control is locked", () => {
  let s = lib.nextState(lib.initialState(true), { type: "connected", deviceOk: true });
  s = lib.nextState(s, { type: "file", ok: true });
  s = lib.nextState(s, { type: "agree", value: true });
  s = lib.nextState(s, { type: "busy" });
  assert.deepEqual(lib.controls(s), {
    connect: false, restart: false, read: false, pickFile: false, agree: false, write: false,
  });
  s = lib.nextState(s, { type: "idle" });
  assert.equal(lib.controls(s).write, true);
  assert.equal(lib.controls(s).read, true);
});

test("another file or another connection clears the tick", () => {
  let s = lib.nextState(lib.initialState(true), { type: "connected", deviceOk: true });
  s = lib.nextState(s, { type: "file", ok: true });
  s = lib.nextState(s, { type: "agree", value: true });
  assert.equal(lib.nextState(s, { type: "file", ok: true }).agreed, false);
  const gone = lib.nextState(lib.nextState(s, { type: "busy" }), { type: "disconnected" });
  assert.equal(gone.agreed, false);
  assert.equal(gone.busy, false);
  assert.equal(gone.fileOk, true);
  assert.deepEqual(lib.controls(gone), {
    connect: true, restart: false, read: false, pickFile: true, agree: true, write: false,
  });
  assert.throws(() => lib.nextState(s, { type: "nonsense" }), /unknown event/);
});

test("nextState does not change the state it is given", () => {
  const s = lib.initialState(true);
  lib.nextState(s, { type: "connected", deviceOk: true });
  assert.deepEqual(s, lib.initialState(true));
});

// ---- the page and the library agree ----

test("backup.html pins esptool-js 0.7.0 and loads backuplib.js", () => {
  const page = fs.readFileSync(path.join(__dirname, "..", "site", "backup.html"), "utf8");
  assert.ok(page.includes("https://cdn.jsdelivr.net/npm/esptool-js@0.7.0/bundle.js"));
  assert.equal(/esptool-js@(?!0\.7\.0\/)/.test(page), false, "another esptool-js version in the page");
  assert.ok(page.includes('<script src="backuplib.js"></script>'));
  assert.ok(page.includes("not tested on a real reader yet"));
  assert.ok(page.includes("This replaces everything on the reader, bootloader included."));
  assert.ok(page.includes("Keep this file. A backup of the stock firmware is the only way back to it."));
});

test("the boot mode steps are the loader page's, word for word", () => {
  const squash = (text) => text.replace(/\s+/g, " ");
  const page = squash(fs.readFileSync(path.join(__dirname, "..", "site", "backup.html"), "utf8"));
  const index = squash(fs.readFileSync(path.join(__dirname, "..", "site", "index.html"), "utf8"));
  for (const sentence of [
    "<h3>Put the X4 Pro into boot mode</h3>",
    "With the reader switched off, press and hold <strong>Power</strong>, then also press and hold <strong>both page-turn buttons</strong>, and keep holding them.",
    "<h3>Connect it to your computer</h3>",
    "Use a USB-C <em>data</em> cable (some cables only charge).",
  ]) {
    assert.ok(index.includes(sentence), "index.html no longer says: " + sentence);
    assert.ok(page.includes(sentence), "backup.html does not say: " + sentence);
  }
});

Promise.all(pending).then(() => {
  console.log(passed + " passed, " + failures.length + " failed");
  for (const failure of failures) console.log("FAILED " + failure);
  process.exit(failures.length === 0 ? 0 : 1);
});
