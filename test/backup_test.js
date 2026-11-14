// Tests of site/backuplib.js (the backup and restore page's logic), with a mocked loader.
// No packages, no network, no browser, no serial port:
//
//   node test/backup_test.js
//
// The exit code is 0 when every case passes. Two optional checks against real files:
//   BACKUP_TEST_ESPTOOL_BUNDLE=<esptool-js 0.7.0 bundle.js>  the mock's method names exist in it
//   BACKUP_TEST_FACTORY_IMAGE=<a VaultInk x4pro factory .bin>  the image checks pass on it
// (the factory image is also looked for in .pio/build/x4pro/firmware.factory.bin).
"use strict";

const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const lib = require(path.join(__dirname, "..", "site", "backuplib.js"));

let passed = 0;
let skipped = 0;
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

// ---- the mock ----

// What esptool-js 0.7.0 really has, of what the page's library may use (lib/esploader.js,
// lib/targets/esp32s3.js, lib/webserial.js). Anything else read from a mock throws, so a call to
// a method the real library does not have (writeRegister, say) fails the test.
const REAL_API = {
  loader: ["main", "detectFlashSize", "readFlash", "flashMd5sum", "writeFlash", "writeReg", "readReg",
    "isUsbJtagSerialPort", "secureDownloadMode", "chip"],
  chip: ["CHIP_NAME", "readMac"],
  transport: ["setDTR", "setRTS"],
};
function strict(kind, target) {
  for (const name of Object.keys(target)) assert.ok(REAL_API[kind].includes(name), "the mock has " + kind + "." + name);
  return new Proxy(target, {
    get(object, name) {
      if (typeof name === "symbol") return object[name];
      if (!REAL_API[kind].includes(name)) throw new Error("esptool-js 0.7.0 has no " + kind + "." + String(name));
      return object[name];
    },
  });
}
const isU32 = (value) => Number.isInteger(value) && value >= 0 && value <= 0xffffffff;

// The bytes of the mocked flash at `offset`.
function flashBytes(offset, size) {
  const out = new Uint8Array(size);
  for (let i = 0; i < size; ++i) out[i] = ((offset + i) * 7 + ((offset + i) >> 11)) & 0xff;
  return out;
}

// A loader and a transport that write down what they are asked, in one shared list.
function mock(options) {
  const o = options || {};
  const calls = [];
  const loader = strict("loader", {
    secureDownloadMode: Boolean(o.secure),
    chip: strict("chip", {
      CHIP_NAME: o.chipName || "ESP32-S3",
      readMac: async () => o.mac || "aa:bb:cc:dd:ee:ff",
    }),
    isUsbJtagSerialPort: () => o.usbJtag !== false,
    main: async () => {
      calls.push(["main"]);
      return "ESP32-S3 (QFN56) (revision v0.2)";
    },
    detectFlashSize: async () => (o.flashSize === undefined ? "16MB" : o.flashSize),
    readFlash: async (offset, size, onPacket) => {
      assert.ok(isU32(offset) && isU32(size), "readFlash(addr, size)");
      calls.push(["readFlash", offset, size]);
      if (o.onRead) o.onRead(offset);
      const returned = o.shortAt === offset ? size - 1 : size;
      const out = flashBytes(offset, returned);
      if (o.flipAt === offset) out[5] ^= 0x10; // one bit wrong on the way
      if (onPacket) {
        onPacket(out.subarray(0, 1024), Math.min(1024, returned), size);
        onPacket(out.subarray(0, 1024), returned, size);
      }
      return o.notBytes ? "timeout" : out;
    },
    flashMd5sum: async (offset, size) => {
      assert.ok(isU32(offset) && isU32(size), "flashMd5sum(addr, size)");
      calls.push(["flashMd5sum", offset, size]);
      const hex = crypto.createHash("md5").update(flashBytes(offset, size)).digest("hex");
      return o.upperMd5 ? hex.toUpperCase() : hex;
    },
    writeFlash: async (flashOptions) => {
      calls.push(["writeFlash", flashOptions]);
      flashOptions.reportProgress(0, 10, 40);
      flashOptions.reportProgress(0, 40, 40);
    },
    // The real one: writeReg(addr, value, mask = 0xffffffff, delayUs = 0, delayAfterUs = 0).
    writeReg: async (...args) => {
      assert.ok(args.length >= 2 && args.length <= 5, "writeReg takes 2 to 5 arguments");
      for (const arg of args) assert.ok(isU32(arg), "writeReg argument " + arg + " is not a 32-bit unsigned number");
      calls.push(["writeReg"].concat(args));
      if (o.writeRegFails) throw new Error("no answer");
    },
    readReg: async (...args) => {
      assert.ok(args.length === 1 && isU32(args[0]), "readReg(addr)");
      calls.push(["readReg", args[0]]);
      return o.swdConf === undefined ? 0x00000012 : o.swdConf;
    },
  });
  const transport = strict("transport", {
    setDTR: async (state) => {
      assert.equal(typeof state, "boolean");
      calls.push(["setDTR", state]);
    },
    setRTS: async (state) => {
      assert.equal(typeof state, "boolean");
      calls.push(["setRTS", state]);
    },
  });
  const sleep = async (ms) => calls.push(["sleep", ms]);
  return { loader, transport, sleep, calls };
}

// A 16 MB image with what checkBackupImage() looks at: a bootloader header, VaultInk's partition
// table and an app header; everything else erased (0xFF).
function header(data, offset, segments, chipId) {
  data.set([0xe9, segments, 0x02, 0x4f, 0x98, 0x88, 0x3c, 0x40, 0xee, 0, 0, 0, chipId & 0xff, chipId >> 8, 0, 0], offset);
  data.fill(0, offset + 16, offset + 24);
}
function partition(data, index, type, subtype, offset, size, label) {
  const at = 0x8000 + index * 32;
  const view = new DataView(data.buffer);
  data.fill(0, at, at + 32);
  data.set([0xaa, 0x50, type, subtype], at);
  view.setUint32(at + 4, offset, true);
  view.setUint32(at + 8, size, true);
  for (let i = 0; i < label.length; ++i) data[at + 12 + i] = label.charCodeAt(i);
}
function image() {
  const data = new Uint8Array(lib.FLASH_BYTES).fill(0xff);
  header(data, 0, 3, 9);
  partition(data, 0, 1, 2, 0x9000, 0x5000, "nvs");
  partition(data, 1, 1, 0, 0xe000, 0x2000, "otadata");
  partition(data, 2, 0, 0x10, 0x10000, 0x7f0000, "app0");
  partition(data, 3, 0, 0x11, 0x800000, 0x7f0000, "app1");
  partition(data, 4, 1, 3, 0xff0000, 0x10000, "coredump");
  data.set([0xeb, 0xeb], 0x8000 + 5 * 32);
  header(data, 0x10000, 7, 9);
  return data;
}

// ---- the mock itself ----

test("the mock refuses a method esptool-js 0.7.0 does not have", async () => {
  const m = mock();
  assert.throws(() => m.loader.writeRegister, /has no loader.writeRegister/);
  assert.throws(() => m.loader.hardReset, /has no loader.hardReset/);
  assert.throws(() => m.transport.setSignals, /has no transport.setSignals/);
  assert.throws(() => m.loader.chip.getChipId, /has no chip.getChipId/);
  await assert.rejects(m.loader.writeReg(1), /2 to 5 arguments/);
  await assert.rejects(m.loader.writeReg(1, -1), /not a 32-bit unsigned/);
  await assert.rejects(m.loader.writeReg(1, 2, 3, 4, 5, 6), /2 to 5 arguments/);
});

test("the mock's method names are in the real esptool-js 0.7.0 bundle (optional)", () => {
  const file = process.env.BACKUP_TEST_ESPTOOL_BUNDLE;
  if (!file) {
    ++skipped;
    return;
  }
  const bundle = fs.readFileSync(file, "utf8");
  for (const name of ["main", "detectFlashSize", "readFlash", "flashMd5sum", "writeFlash", "writeReg", "readReg", "readMac", "setDTR", "setRTS"]) {
    assert.ok(bundle.includes("async " + name + "("), "the bundle has no async " + name + "()");
  }
  for (const name of ["isUsbJtagSerialPort(){", "secureDownloadMode", "CHIP_NAME=\"ESP32-S3\""]) {
    assert.ok(bundle.includes(name), "the bundle has no " + name);
  }
  assert.equal(bundle.includes("writeRegister("), false);
  // writeReg(addr, value, mask = 0xffffffff, delayUs = 0, delayAfterUs = 0), minified.
  assert.match(bundle, /async writeReg\(\w+,\w+,\w+=4294967295,\w+=0,\w+=0\)/);
  assert.match(bundle, /async flashMd5sum\(\w+,\w+\)/);
});

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

test("readFlash reads every window in order and checks each against the reader's MD5", async () => {
  const m = mock();
  const seen = [];
  const chunks = await lib.readFlash(m.loader, { onProgress: (done, total) => seen.push([done, total]) });
  assert.equal(m.calls.length, 128);
  for (let i = 0; i < 64; ++i) {
    // The window, then the checksum of the same window, never 16 MB in one call.
    assert.deepEqual(m.calls[2 * i], ["readFlash", i * 262144, 262144]);
    assert.deepEqual(m.calls[2 * i + 1], ["flashMd5sum", i * 262144, 262144]);
  }
  assert.equal(chunks.length, 64);
  assert.equal(chunks.reduce((sum, chunk) => sum + chunk.length, 0), lib.FLASH_BYTES);
  assert.deepEqual(chunks[5].subarray(0, 64), flashBytes(5 * 262144, 64));
  // Progress counts from the start of the flash, never goes back, and ends at the total.
  for (let i = 1; i < seen.length; ++i) assert.ok(seen[i][0] >= seen[i - 1][0], "progress went back");
  assert.deepEqual(seen[0], [1024, lib.FLASH_BYTES]);
  assert.deepEqual(seen[seen.length - 1], [lib.FLASH_BYTES, lib.FLASH_BYTES]);
});

test("a window that differs from the flash by one bit stops the backup, with the offset", async () => {
  const m = mock({ flipAt: 3 * 262144 });
  await assert.rejects(
    lib.readFlash(m.loader, {}),
    /block at 0xc0000 \(262144 bytes\) is not what the reader has.*nothing was saved/
  );
  assert.deepEqual(m.calls[m.calls.length - 1], ["flashMd5sum", 3 * 262144, 262144]);
  assert.equal(m.calls.filter((call) => call[0] === "readFlash").length, 4);
});

test("the reader's MD5 is compared whatever its letter case", async () => {
  const m = mock({ upperMd5: true });
  const chunks = await lib.readFlash(m.loader, { total: 2048, windowSize: 1024 });
  assert.equal(chunks.length, 2);
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
  const m = mock();
  const chunks = await lib.readFlash(m.loader, { total: 10, windowSize: 4 });
  assert.deepEqual(chunks.map((chunk) => chunk.length), [4, 4, 2]);
  assert.deepEqual(m.calls, [
    ["readFlash", 0, 4], ["flashMd5sum", 0, 4],
    ["readFlash", 4, 4], ["flashMd5sum", 4, 4],
    ["readFlash", 8, 2], ["flashMd5sum", 8, 2],
  ]);
});

test("Abort stops between two windows and returns nothing", async () => {
  let stop = false;
  const m = mock({ onRead: (offset) => { if (offset === 2 * 1024) stop = true; } });
  let error = null;
  let result = "nothing";
  try {
    result = await lib.readFlash(m.loader, { total: 10 * 1024, windowSize: 1024, shouldAbort: () => stop });
  } catch (caught) {
    error = caught;
  }
  assert.equal(result, "nothing");
  assert.equal(error.name, "AbortError");
  assert.match(error.message, /stopped at 0xc00\. Nothing was saved/);
  // The window that was on its way is finished and checked; the next one is not asked for.
  assert.deepEqual(m.calls[m.calls.length - 1], ["flashMd5sum", 2 * 1024, 1024]);
  assert.equal(m.calls.filter((call) => call[0] === "readFlash").length, 3);
});

test("Abort before the first window reads nothing", async () => {
  const m = mock();
  await assert.rejects(lib.readFlash(m.loader, { shouldAbort: () => true }), { name: "AbortError" });
  assert.deepEqual(m.calls, []);
});

// ---- watchdogs ----

test("watchdogs: esptool's sequence for the ESP32-S3 on USB-Serial/JTAG, in order", async () => {
  const m = mock({ swdConf: 0x00000012 });
  assert.equal(await lib.disableWatchdogs(m.loader), "done");
  assert.deepEqual(m.calls, [
    ["writeReg", 0x600080b0, 0x50d83aa1], // RTC_CNTL_WDTWPROTECT_REG = RTC_CNTL_WDT_WKEY
    ["writeReg", 0x60008098, 0], // RTC_CNTL_WDTCONFIG0_REG = 0
    ["writeReg", 0x600080b0, 0],
    ["writeReg", 0x600080b8, 0x8f1d312a], // RTC_CNTL_SWD_WPROTECT_REG = RTC_CNTL_SWD_WKEY
    ["readReg", 0x600080b4],
    ["writeReg", 0x600080b4, 0x80000012], // RTC_CNTL_SWD_CONF_REG |= RTC_CNTL_SWD_AUTO_FEED_EN
    ["writeReg", 0x600080b8, 0],
  ]);
});

test("watchdogs: the top bit already set stays an unsigned number", async () => {
  const m = mock({ swdConf: 0xc0000001 });
  assert.equal(await lib.disableWatchdogs(m.loader), "done");
  assert.deepEqual(m.calls[5], ["writeReg", 0x600080b4, 0xc0000001]);
});

test("watchdogs: skipped on another chip and on another kind of port; a failure is reported, not thrown", async () => {
  const other = mock({ chipName: "ESP32-C3" });
  assert.equal(await lib.disableWatchdogs(other.loader), "skipped");
  assert.deepEqual(other.calls, []);
  const uart = mock({ usbJtag: false });
  assert.equal(await lib.disableWatchdogs(uart.loader), "skipped");
  assert.deepEqual(uart.calls, []);
  const failing = mock({ writeRegFails: true });
  assert.equal(await lib.disableWatchdogs(failing.loader), "failed");
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

// ---- the file checks ----

test("the quick check: exactly 16,777,216 bytes and 0xE9 first", () => {
  assert.deepEqual(lib.checkBackupFile({ size: 16777216, firstByte: 0xe9 }), { ok: true, reason: "" });
  assert.match(lib.checkBackupFile({ size: 16777215, firstByte: 0xe9 }).reason, /16,777,215 bytes.*exactly 16,777,216/);
  assert.equal(lib.checkBackupFile({ size: 16777217, firstByte: 0xe9 }).ok, false);
  assert.equal(lib.checkBackupFile({ size: 0, firstByte: undefined }).ok, false);
  assert.match(lib.checkBackupFile({ size: 16777216, firstByte: 0xff }).reason, /0xE9/);
  assert.equal(lib.checkBackupFile({ size: 16777216, firstByte: 0x00 }).ok, false);
  assert.equal(lib.checkBackupFile(null).ok, false);
});

test("the image check passes an image with a bootloader, a partition table and an app", () => {
  const data = image();
  assert.deepEqual(lib.checkBackupImage(data), { ok: true, reason: "" });
  assert.deepEqual(lib.partitionEntries(data).map((e) => [e.label, e.type, e.offset, e.size]), [
    ["nvs", 1, 0x9000, 0x5000],
    ["otadata", 1, 0xe000, 0x2000],
    ["app0", 0, 0x10000, 0x7f0000],
    ["app1", 0, 0x800000, 0x7f0000],
    ["coredump", 1, 0xff0000, 0x10000],
  ]);
});

test("the image check: an erased file, a zero file, a short file, something that is not bytes", () => {
  assert.match(lib.checkBackupImage(new Uint8Array(lib.FLASH_BYTES).fill(0xff)).reason, /0xE9/);
  assert.match(lib.checkBackupImage(new Uint8Array(lib.FLASH_BYTES)).reason, /0xE9/);
  assert.match(lib.checkBackupImage(image().subarray(0, lib.FLASH_BYTES - 4096)).reason, /exactly 16,777,216/);
  assert.match(lib.checkBackupImage(null).reason, /not read/);
  assert.match(lib.checkBackupImage(new ArrayBuffer(lib.FLASH_BYTES)).reason, /not read/);
});

test("the image check: the bootloader's chip id and segment count", () => {
  const c3 = image();
  header(c3, 0, 3, 5); // ESP_CHIP_ID_ESP32C3
  assert.match(lib.checkBackupImage(c3).reason, /bootloader.*chip id is 0x0005, not 0x0009/);
  const none = image();
  none[1] = 0;
  assert.match(lib.checkBackupImage(none).reason, /segment count is 0/);
  const many = image();
  many[1] = 17;
  assert.match(lib.checkBackupImage(many).reason, /segment count is 17/);
  const edge = image();
  edge[1] = 16;
  assert.equal(lib.checkBackupImage(edge).ok, true);
});

test("the image check: the partition table at 0x8000", () => {
  const erased = image();
  erased.fill(0xff, 0x8000, 0x8c00);
  assert.match(lib.checkBackupImage(erased).reason, /no partition table at 0x8000/);
  const halfMagic = image();
  halfMagic[0x8001] = 0x51;
  assert.match(lib.checkBackupImage(halfMagic).reason, /no partition table/);
  const noApp = image();
  partition(noApp, 2, 1, 0x82, 0x10000, 0x7f0000, "spiffs");
  partition(noApp, 3, 1, 0x82, 0x800000, 0x7f0000, "spiffs2");
  assert.match(lib.checkBackupImage(noApp).reason, /no app partition/);
  const tooFar = image();
  partition(tooFar, 4, 1, 3, 0xff0000, 0x20000, "coredump");
  assert.match(lib.checkBackupImage(tooFar).reason, /"coredump" ends after 16 MB/);
});

test("the image check: an app partition must hold an ESP32-S3 image", () => {
  const noApp = image();
  noApp.fill(0xff, 0x10000, 0x10020);
  assert.match(lib.checkBackupImage(noApp).reason, /No app partition holds a firmware.*at 0x10000 it does not start with the byte 0xE9/);
  const wrongChip = image();
  header(wrongChip, 0x10000, 7, 0);
  assert.match(lib.checkBackupImage(wrongChip).reason, /at 0x10000 its chip id is 0x0000/);
  // The firmware may be in the second slot only.
  const second = image();
  second.fill(0xff, 0x10000, 0x10020);
  header(second, 0x800000, 6, 9);
  assert.equal(lib.checkBackupImage(second).ok, true);
  // A table that is not VaultInk's: the app is where the table says, not at a fixed offset.
  const moved = image();
  moved.fill(0xff, 0x10000, 0x10020);
  partition(moved, 2, 0, 0x00, 0x20000, 0x400000, "factory");
  partition(moved, 3, 1, 0x82, 0x420000, 0x100000, "spiffs");
  header(moved, 0x20000, 5, 9);
  assert.equal(lib.checkBackupImage(moved).ok, true);
});

test("the image checks pass on a real VaultInk factory image, padded to 16 MB (optional)", () => {
  const wanted = process.env.BACKUP_TEST_FACTORY_IMAGE ||
    path.join(__dirname, "..", ".pio", "build", "x4pro", "firmware.factory.bin");
  if (!fs.existsSync(wanted)) {
    ++skipped;
    return;
  }
  const factory = fs.readFileSync(wanted);
  assert.ok(factory.length > 0x10000 + 24 && factory.length <= lib.FLASH_BYTES);
  assert.match(lib.checkBackupImage(new Uint8Array(factory)).reason, /exactly 16,777,216/);
  const data = new Uint8Array(lib.FLASH_BYTES).fill(0xff);
  data.set(factory);
  assert.deepEqual(lib.checkBackupImage(data), { ok: true, reason: "" });
  assert.ok(lib.partitionEntries(data).some((e) => e.type === 0 && data[e.offset] === 0xe9));
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
  for (const length of [1, 55, 56, 57, 63, 64, 65, 119, 120, 1000, 262144, 262144 + 3]) {
    const data = flashBytes(length, length);
    assert.equal(lib.md5Hex(data), md5(data), "length " + length);
  }
  const whole = image();
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
  // The fields of esptool-js 0.7.0's FlashOptions (lib/types/flashOptions.d.ts), and no other.
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
  const noTable = image();
  noTable.fill(0xff, 0x8000, 0x8c00);
  await assert.rejects(lib.restore(m.loader, noTable), /no partition table/);
  const wrongChip = image();
  header(wrongChip, 0, 3, 2);
  await assert.rejects(lib.restore(m.loader, wrongChip), /chip id is 0x0002/);
  await assert.rejects(lib.restore(m.loader, null), /not read/);
  await assert.rejects(lib.restore(m.loader, new ArrayBuffer(lib.FLASH_BYTES)), /not read/);
  assert.deepEqual(m.calls, []);
});

// ---- reset ----

test("reset: clear the bit, DTR off, then one RTS pulse, in that order", async () => {
  const m = mock();
  const result = await lib.resetToFirmware(m.loader, m.transport, m.sleep);
  assert.deepEqual(m.calls, [
    ["writeReg", 0x6000812c, 0, 0x1], // writeReg(addr, value, mask)
    ["setDTR", false],
    ["setRTS", true],
    ["sleep", 100],
    ["setRTS", false],
  ]);
  assert.deepEqual(result, { bit: "cleared" });
});

test("reset: the pulse still happens when the register write fails, and the result says so", async () => {
  const m = mock({ writeRegFails: true });
  const result = await lib.resetToFirmware(m.loader, m.transport, m.sleep);
  assert.deepEqual(m.calls.slice(1), [["setDTR", false], ["setRTS", true], ["sleep", 100], ["setRTS", false]]);
  assert.deepEqual(result, { bit: "failed" });
});

test("reset: the ESP32-S3 register is not written on another chip", async () => {
  for (const chipName of ["ESP32-C3", "ESP32-S2", "ESP32-S31", "ESP32"]) {
    const m = mock({ chipName: chipName });
    const result = await lib.resetToFirmware(m.loader, m.transport, m.sleep);
    assert.deepEqual(m.calls, [["setDTR", false], ["setRTS", true], ["sleep", 100], ["setRTS", false]], chipName);
    assert.deepEqual(result, { bit: "skipped" });
  }
});

test("reset never asserts DTR", async () => {
  const m = mock();
  await lib.resetToFirmware(m.loader, m.transport, m.sleep);
  assert.equal(m.calls.some((call) => call[0] === "setDTR" && call[1] === true), false);
  assert.deepEqual(m.calls[m.calls.length - 1], ["setRTS", false]);
});

// ---- the page's state ----

const MINE = "aa:bb:cc:dd:ee:ff";
const MY_FILE = "vaultink-x4pro-backup-aabbccddeeff-2026-11-14.bin";
const OTHER_FILE = "vaultink-x4pro-backup-112233445566-2026-11-14.bin";
function after(events, start) {
  return events.reduce((state, event) => lib.nextState(state, event), start || lib.initialState(true));
}
const CONNECT = [{ type: "connecting" }, { type: "connected", deviceOk: true, mac: MINE }];

test("at the start only Connect and the file picker work", () => {
  assert.deepEqual(lib.controls(lib.initialState(true)), {
    connect: true, restart: false, read: false, pickFile: true, agree: false, agreeOther: false, write: false,
  });
});

test("without Web Serial nothing that needs the reader works", () => {
  const c = lib.controls(lib.initialState(false));
  assert.equal(c.connect, false);
  assert.equal(c.read, false);
  assert.equal(c.write, false);
});

test("while connecting everything is locked; a failed connect unlocks Connect", () => {
  const s = after([{ type: "connecting" }]);
  assert.deepEqual(lib.controls(s), {
    connect: false, restart: false, read: false, pickFile: false, agree: false, agreeOther: false, write: false,
  });
  assert.equal(lib.controls(after([{ type: "idle" }], s)).connect, true);
});

test("a refused device can be restarted but not read or written", () => {
  const s = after([
    { type: "connecting" },
    { type: "connected", deviceOk: false, mac: MINE },
    { type: "file", ok: true, name: MY_FILE },
    { type: "agree", value: true },
  ]);
  assert.deepEqual(lib.controls(s), {
    connect: false, restart: true, read: false, pickFile: true, agree: true, agreeOther: false, write: false,
  });
});

test("Write needs a connected X4 Pro, a good file and the tick, all three", () => {
  let s = after(CONNECT);
  assert.equal(lib.controls(s).read, true);
  assert.equal(lib.controls(s).write, false);
  s = after([{ type: "file", ok: true, name: MY_FILE }], s);
  assert.equal(lib.macVerdict(s), "same");
  assert.equal(lib.controls(s).agree, true);
  assert.equal(lib.controls(s).agreeOther, false);
  assert.equal(lib.controls(s).write, false);
  s = after([{ type: "agree", value: true }], s);
  assert.equal(lib.controls(s).write, true);
  s = after([{ type: "agree", value: false }], s);
  assert.equal(lib.controls(s).write, false);
  // A good file and the tick, but no reader.
  const t = after([{ type: "file", ok: true, name: MY_FILE }, { type: "agree", value: true }]);
  assert.equal(lib.macVerdict(t), "none");
  assert.equal(lib.controls(t).write, false);
  // A bad file cannot be agreed to.
  const u = after(CONNECT.concat([{ type: "file", ok: false, name: "x.bin" }, { type: "agree", value: true }]));
  assert.equal(lib.controls(u).agree, false);
  assert.equal(lib.controls(u).write, false);
  assert.equal(u.fileName, "");
});

test("a file from another reader needs the second tick box too", () => {
  let s = after(CONNECT.concat([{ type: "file", ok: true, name: OTHER_FILE }]));
  assert.equal(lib.macVerdict(s), "different");
  assert.equal(lib.controls(s).agreeOther, true);
  s = after([{ type: "agree", value: true }], s);
  assert.equal(lib.controls(s).write, false, "the first tick alone is not enough");
  s = after([{ type: "agreeOther", value: true }], s);
  assert.equal(lib.controls(s).write, true);
  s = after([{ type: "agreeOther", value: false }], s);
  assert.equal(lib.controls(s).write, false);
  // The second tick alone is not enough either.
  const only = after(CONNECT.concat([{ type: "file", ok: true, name: OTHER_FILE }, { type: "agreeOther", value: true }]));
  assert.equal(lib.controls(only).write, false);
  // A name without a MAC warns on the page but asks for one tick only.
  const unknown = after(CONNECT.concat([{ type: "file", ok: true, name: "stock.bin" }, { type: "agree", value: true }]));
  assert.equal(lib.macVerdict(unknown), "unknown");
  assert.equal(lib.controls(unknown).agreeOther, false);
  assert.equal(lib.controls(unknown).write, true);
});

test("the ticks do not survive another reader: tick first, connect after", () => {
  // File and both ticks while reader A is connected, then A goes and B (the file's reader) comes.
  let s = after(CONNECT.concat([
    { type: "file", ok: true, name: OTHER_FILE },
    { type: "agree", value: true },
    { type: "agreeOther", value: true },
  ]));
  assert.equal(lib.controls(s).write, true);
  s = after([{ type: "disconnected" }], s);
  assert.equal(s.agreed, false);
  assert.equal(s.agreedOther, false);
  // Ticked with no reader connected, then a reader with another MAC connects: both are cleared.
  s = after([{ type: "agree", value: true }, { type: "agreeOther", value: true }], s);
  s = after([{ type: "connecting" }, { type: "connected", deviceOk: true, mac: MINE }], s);
  assert.equal(lib.macVerdict(s), "different");
  assert.equal(s.agreed, false);
  assert.equal(s.agreedOther, false);
  assert.equal(lib.controls(s).write, false);
});

test("another file clears both ticks", () => {
  const s = after(CONNECT.concat([
    { type: "file", ok: true, name: OTHER_FILE },
    { type: "agree", value: true },
    { type: "agreeOther", value: true },
    { type: "file", ok: true, name: MY_FILE },
  ]));
  assert.equal(s.agreed, false);
  assert.equal(s.agreedOther, false);
  assert.equal(lib.macVerdict(s), "same");
  assert.equal(lib.controls(s).write, false);
});

test("while the reader is busy every control is locked", () => {
  let s = after(CONNECT.concat([
    { type: "file", ok: true, name: MY_FILE },
    { type: "agree", value: true },
    { type: "busy" },
  ]));
  assert.deepEqual(lib.controls(s), {
    connect: false, restart: false, read: false, pickFile: false, agree: false, agreeOther: false, write: false,
  });
  s = after([{ type: "idle" }], s);
  assert.equal(lib.controls(s).write, true);
  assert.equal(lib.controls(s).read, true);
});

test("the session number: work started under an older connection is stale", () => {
  let s = lib.initialState(true);
  assert.equal(s.session, 0);
  s = after(CONNECT, s);
  assert.equal(s.session, 1);
  const mine = s.session; // a read or a write starts here
  s = after([{ type: "busy" }], s);
  assert.equal(lib.isCurrent(s, mine), true);
  s = after([{ type: "idle" }, { type: "agree", value: true }, { type: "file", ok: true, name: MY_FILE }], s);
  assert.equal(lib.isCurrent(s, mine), true, "only connections count");
  s = after([{ type: "busy" }, { type: "disconnected" }], s);
  assert.equal(s.session, 2);
  assert.equal(s.busy, false);
  assert.equal(lib.isCurrent(s, mine), false, "the reader went away under the work");
  s = after(CONNECT, s);
  assert.equal(s.session, 3);
  assert.equal(lib.isCurrent(s, mine), false, "a newer connection is not the old work's");
  assert.equal(lib.isCurrent(s, 3), true);
  // A connect that failed and the next one are different sessions too.
  const failed = after([{ type: "connecting" }, { type: "idle" }, { type: "connecting" }]);
  assert.equal(failed.session, 2);
});

test("after a disconnect: Connect again, the file is kept, the ticks are not", () => {
  const gone = after(CONNECT.concat([
    { type: "file", ok: true, name: MY_FILE },
    { type: "agree", value: true },
    { type: "busy" },
    { type: "disconnected" },
  ]));
  assert.equal(gone.fileOk, true);
  assert.equal(gone.deviceMac, "");
  assert.deepEqual(lib.controls(gone), {
    connect: true, restart: false, read: false, pickFile: true, agree: true, agreeOther: false, write: false,
  });
  assert.throws(() => lib.nextState(gone, { type: "nonsense" }), /unknown event/);
});

test("nextState does not change the state it is given", () => {
  const s = lib.initialState(true);
  lib.nextState(s, { type: "connecting" });
  lib.nextState(s, { type: "connected", deviceOk: true, mac: MINE });
  assert.deepEqual(s, lib.initialState(true));
});

// ---- the page and the library agree ----

const pageText = () => fs.readFileSync(path.join(__dirname, "..", "site", "backup.html"), "utf8");
const squash = (text) => text.replace(/\s+/g, " ");

test("backup.html pins esptool-js 0.7.0 and loads backuplib.js", () => {
  const page = pageText();
  assert.ok(page.includes("https://cdn.jsdelivr.net/npm/esptool-js@0.7.0/bundle.js"));
  assert.equal(/esptool-js@(?!0\.7\.0\/)/.test(page), false, "another esptool-js version in the page");
  assert.ok(page.includes('<script src="backuplib.js"></script>'));
  assert.ok(page.includes("not tested on a real reader yet"));
  assert.ok(page.includes("This replaces everything on the reader, bootloader included."));
  assert.ok(page.includes("Keep this file. A backup of the stock firmware is the only way back to it."));
  assert.ok(page.includes("The file is from a different reader (MAC differs): write anyway"));
});

test("backup.html calls no loader method the real library lacks", () => {
  const page = pageText();
  const used = new Set();
  for (const match of page.matchAll(/\b(?:loader|c\.loader|conn\.loader)\.([A-Za-z_]+)/g)) used.add(match[1]);
  for (const name of used) assert.ok(REAL_API.loader.includes(name), "backup.html uses loader." + name);
  const transportUsed = new Set();
  for (const match of page.matchAll(/\b(?:transport|c\.transport|conn\.transport)\.([A-Za-z_]+)/g)) transportUsed.add(match[1]);
  // disconnect() is Transport's own (lib/webserial.js); the page closes the port with it.
  for (const name of transportUsed) assert.ok(["setDTR", "setRTS", "disconnect"].includes(name), "backup.html uses transport." + name);
  assert.equal(page.includes("writeRegister"), false);
});

test("backup.html says what a failed bit clear means, and that a write cannot be stopped", () => {
  const page = squash(pageText());
  assert.ok(page.includes("the reader may re-enter download mode; unplug it and power it on with the power button"));
  assert.ok(page.includes("A write cannot be stopped safely"));
  assert.ok(page.includes('id="abort"'));
  assert.ok(page.includes('id="agreeOther"'));
});

test("the boot mode steps are the loader page's, word for word", () => {
  const page = squash(pageText());
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
  console.log(passed - skipped + " passed, " + failures.length + " failed" + (skipped ? ", " + skipped + " skipped (optional files not given)" : ""));
  for (const failure of failures) console.log("FAILED " + failure);
  process.exit(failures.length === 0 ? 0 : 1);
});
