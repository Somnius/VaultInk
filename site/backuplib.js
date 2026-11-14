// VaultInk backup and restore page: the parts with no page and no serial port in them.
// The page (backup.html) loads this file with a <script> tag and uses the global BackupLib;
// test/backup_test.js loads the same file with Node and gives it a mocked loader.
//
// The loader is esptool-js 0.7.0 (ESPLoader and Transport). Every function here that talks to
// the reader takes the loader (and the transport) as an argument, so nothing in this file opens
// a port.
(function (root, factory) {
  "use strict";
  const lib = factory();
  if (typeof module === "object" && module.exports) module.exports = lib;
  else root.BackupLib = lib;
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  const USB_VENDOR_ESPRESSIF = 0x303a;
  const CHIP_NAME = "ESP32-S3"; // esptool-js: loader.chip.CHIP_NAME
  const FLASH_SIZE_NAME = "16MB"; // esptool-js: loader.detectFlashSize()
  const FLASH_BYTES = 16 * 1024 * 1024; // 16,777,216
  // esptool-js's readFlash() copies everything it has so far for each 1 kB packet, so one call
  // for 16 MB gets slower and slower. A window keeps each copy small.
  const READ_WINDOW = 256 * 1024;
  const IMAGE_MAGIC = 0xe9; // first byte of an ESP image; the bootloader is at offset 0 on the S3
  // ESP32-S3: bit 0 of this register makes the next reset start the ROM loader again.
  const RTC_CNTL_OPTION1_REG = 0x6000812c;
  const RTC_CNTL_FORCE_DOWNLOAD_BOOT = 0x1;
  const RESET_PULSE_MS = 100;

  // ---- numbers and names ----

  // The windows that cover `total` bytes: [{offset, size}], the last one may be shorter.
  function readWindows(total, windowSize) {
    if (!Number.isInteger(total) || total < 0) throw new Error("bad total: " + total);
    if (!Number.isInteger(windowSize) || windowSize <= 0) throw new Error("bad window: " + windowSize);
    const out = [];
    for (let offset = 0; offset < total; offset += windowSize) {
      out.push({ offset: offset, size: Math.min(windowSize, total - offset) });
    }
    return out;
  }

  function formatBytes(n) {
    if (n >= 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB";
    if (n >= 1024) return Math.round(n / 1024) + " kB";
    return Math.round(n) + " B";
  }

  // "3.0 MB of 16.0 MB, 410 kB/s"
  function progressText(done, total, elapsedMs) {
    const text = formatBytes(done) + " of " + formatBytes(total);
    if (!(elapsedMs > 0) || done <= 0) return text;
    return text + ", " + formatBytes(done / (elapsedMs / 1000)) + "/s";
  }

  function percent(done, total) {
    if (!(total > 0)) return 0;
    return Math.max(0, Math.min(100, Math.floor((done / total) * 100)));
  }

  function toHex(bytes) {
    let out = "";
    for (let i = 0; i < bytes.length; ++i) out += (bytes[i] < 16 ? "0" : "") + bytes[i].toString(16);
    return out;
  }

  // "AA:bb:cc-dd ee ff" -> "aabbccddeeff"; null when it is not six bytes of hex.
  function normaliseMac(mac) {
    const hex = String(mac == null ? "" : mac).replace(/[:\-\s]/g, "").toLowerCase();
    return /^[0-9a-f]{12}$/.test(hex) ? hex : null;
  }

  // vaultink-x4pro-backup-<mac>-<YYYY-MM-DD>.bin, the date in the computer's own time zone.
  function backupFilename(mac, date) {
    const hex = normaliseMac(mac);
    if (hex === null) throw new Error("bad MAC address: " + mac);
    const two = (n) => (n < 10 ? "0" : "") + n;
    const day = date.getFullYear() + "-" + two(date.getMonth() + 1) + "-" + two(date.getDate());
    return "vaultink-x4pro-backup-" + hex + "-" + day + ".bin";
  }

  // The MAC in a name this page made, or null (a renamed file, or a browser's "name (1).bin" is
  // still read as long as the start is intact).
  function macFromFilename(name) {
    const match = /vaultink-x4pro-backup-([0-9a-fA-F]{12})-/.exec(String(name == null ? "" : name));
    return match ? match[1].toLowerCase() : null;
  }

  // "same": the name carries the connected reader's MAC; "different": it carries another one;
  // "unknown": the name has no MAC in it, or no reader is connected.
  function compareMac(filename, deviceMac) {
    const fromName = macFromFilename(filename);
    const device = normaliseMac(deviceMac);
    if (fromName === null || device === null) return "unknown";
    return fromName === device ? "same" : "different";
  }

  // ---- checks ----

  // Is this the reader the page is for? info: {chipName, flashSize, secureDownloadMode}.
  function checkDevice(info) {
    const chip = String((info && info.chipName) || "");
    const flash = String((info && info.flashSize) || "");
    if (info && info.secureDownloadMode) {
      return { ok: false, reason: "This chip is in Secure Download Mode: its flash cannot be read this way." };
    }
    if (chip !== CHIP_NAME) {
      return {
        ok: false,
        reason: "This is " + (chip || "an unknown chip") + ", not an " + CHIP_NAME +
          ". This page is only for the Xteink X4 Pro.",
      };
    }
    if (flash !== FLASH_SIZE_NAME) {
      return {
        ok: false,
        reason: "The flash is " + (flash || "of an unknown size") + ", not " + FLASH_SIZE_NAME +
          ". This page is only for the Xteink X4 Pro.",
      };
    }
    return { ok: true, reason: "" };
  }

  // Is this file a whole-flash backup? file: {size, firstByte}.
  function checkBackupFile(file) {
    const size = file ? file.size : NaN;
    if (size !== FLASH_BYTES) {
      return {
        ok: false,
        reason: "The file is " + (Number.isFinite(size) ? size.toLocaleString("en-US") : "?") +
          " bytes. A backup of the whole flash is exactly " + FLASH_BYTES.toLocaleString("en-US") +
          " bytes. A firmware file from the releases page is not a backup; install that from the loader page.",
      };
    }
    if (file.firstByte !== IMAGE_MAGIC) {
      return {
        ok: false,
        reason: "The file does not start with the byte 0xE9, so it does not start with a bootloader. " +
          "It is not a backup this page can write.",
      };
    }
    return { ok: true, reason: "" };
  }

  // ---- hashes ----

  // SHA-256 of an ArrayBuffer or typed array as hex, with the browser's (or Node's) Web Crypto.
  async function sha256Hex(data, subtle) {
    const api = subtle || (typeof crypto !== "undefined" && crypto.subtle);
    if (!api) throw new Error("This browser has no Web Crypto (crypto.subtle).");
    return toHex(new Uint8Array(await api.digest("SHA-256", data)));
  }

  // MD5 of a Uint8Array as hex. Web Crypto has no MD5, and the flasher stub answers with the MD5
  // of what is in the flash: esptool-js compares the two when writeFlash() gets this function.
  // It only finds transfer errors; it is not a security check.
  const MD5_SHIFT = [7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21];
  const MD5_K = new Int32Array(64);
  for (let i = 0; i < 64; ++i) MD5_K[i] = Math.floor(Math.abs(Math.sin(i + 1)) * 4294967296) | 0;

  function md5Hex(bytes) {
    const length = bytes.length;
    const padded = new Uint8Array((((length + 8) >>> 6) + 1) << 6);
    padded.set(bytes);
    padded[length] = 0x80;
    const view = new DataView(padded.buffer);
    view.setUint32(padded.length - 8, (length << 3) >>> 0, true);
    view.setUint32(padded.length - 4, Math.floor(length / 0x20000000), true);

    let a0 = 0x67452301, b0 = 0xefcdab89 | 0, c0 = 0x98badcfe | 0, d0 = 0x10325476;
    const m = new Int32Array(16);
    for (let at = 0; at < padded.length; at += 64) {
      for (let i = 0; i < 16; ++i) m[i] = view.getInt32(at + i * 4, true);
      let a = a0, b = b0, c = c0, d = d0;
      for (let i = 0; i < 64; ++i) {
        let f, g;
        const round = i >> 4;
        if (round === 0) { f = (b & c) | (~b & d); g = i; }
        else if (round === 1) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (round === 2) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7 * i) & 15; }
        const sum = (a + f + MD5_K[i] + m[g]) | 0;
        const shift = MD5_SHIFT[(round << 2) | (i & 3)];
        a = d; d = c; c = b;
        b = (b + ((sum << shift) | (sum >>> (32 - shift)))) | 0;
      }
      a0 = (a0 + a) | 0; b0 = (b0 + b) | 0; c0 = (c0 + c) | 0; d0 = (d0 + d) | 0;
    }
    const out = new Uint8Array(16);
    const outView = new DataView(out.buffer);
    outView.setInt32(0, a0, true);
    outView.setInt32(4, b0, true);
    outView.setInt32(8, c0, true);
    outView.setInt32(12, d0, true);
    return toHex(out);
  }

  // ---- talking to the reader (through the loader it is given) ----

  // What Connect shows: {chip, chipName, flashSize, mac, secureDownloadMode}. `loader` is a new
  // ESPLoader; main() resets the chip into the ROM loader, reads the chip and starts the stub.
  async function identify(loader) {
    const chip = await loader.main();
    const secure = Boolean(loader.secureDownloadMode);
    return {
      chip: String(chip),
      chipName: String(loader.chip && loader.chip.CHIP_NAME),
      flashSize: secure ? "" : String((await loader.detectFlashSize()) || ""),
      mac: secure ? "" : String(await loader.chip.readMac(loader)),
      secureDownloadMode: secure,
    };
  }

  // Reads `total` bytes from offset 0, one window a call. Returns the windows' bytes in order
  // (the page makes a Blob of the list, so the 16 MB are never joined in one buffer here).
  // options: {total, windowSize, onProgress(done, total)}.
  async function readFlash(loader, options) {
    const total = options && options.total !== undefined ? options.total : FLASH_BYTES;
    const windowSize = (options && options.windowSize) || READ_WINDOW;
    const onProgress = (options && options.onProgress) || function () {};
    const chunks = [];
    for (const win of readWindows(total, windowSize)) {
      const chunk = await loader.readFlash(win.offset, win.size, (packet, inWindow) => {
        onProgress(win.offset + inWindow, total);
      });
      if (!(chunk instanceof Uint8Array) || chunk.length !== win.size) {
        throw new Error(
          "The read at 0x" + win.offset.toString(16) + " returned " +
            (chunk && typeof chunk.length === "number" ? chunk.length : 0) + " bytes, not " + win.size +
            ". The backup is not complete; nothing was saved."
        );
      }
      chunks.push(chunk);
      onProgress(win.offset + win.size, total);
    }
    return chunks;
  }

  // The options for esptool-js's writeFlash(): the whole image at offset 0, nothing in its
  // header changed, sent compressed, and the MD5 of the flash compared with the file's after.
  function writeOptions(data, onProgress) {
    return {
      fileArray: [{ data: data, address: 0 }],
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: function (fileIndex, written, total) {
        if (onProgress) onProgress(written, total);
      },
      calculateMD5Hash: md5Hex,
    };
  }

  // Checks the file again, then writes it. Nothing is sent to the reader when a check fails.
  async function restore(loader, data, onProgress) {
    if (!(data instanceof Uint8Array)) throw new Error("The file was not read.");
    const check = checkBackupFile({ size: data.length, firstByte: data[0] });
    if (!check.ok) throw new Error(check.reason);
    await loader.writeFlash(writeOptions(data, onProgress));
  }

  // Leaves the ROM loader and starts what is in the flash.
  //
  // On the X4 Pro the USB port is the ESP32-S3's own USB-Serial/JTAG, where DTR drives GPIO0 and
  // RTS drives the reset line. esptool-js's Transport.setRTS() sends the last DTR state again
  // with every RTS change (lib/webserial.js), so if DTR is still on, an RTS pulse resets the
  // chip with GPIO0 low and it lands in the loader again. This function does not depend on how
  // DTR was left. In this order:
  //   1. clear the "start the loader again" bit (when the write fails, go on: the pulse matters);
  //   2. DTR off, so GPIO0 is high;
  //   3. RTS on, wait, RTS off: one reset pulse.
  // The order is the one explorink.com's open flasher (MIT) describes for the same chip.
  // Returns {cleared}: false when step 1 failed.
  async function resetToFirmware(loader, transport, sleep) {
    const wait = sleep || ((ms) => new Promise((resolve) => setTimeout(resolve, ms)));
    let cleared = true;
    try {
      await loader.writeReg(RTC_CNTL_OPTION1_REG, 0, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    } catch (error) {
      cleared = false;
    }
    await transport.setDTR(false);
    await transport.setRTS(true);
    await wait(RESET_PULSE_MS);
    await transport.setRTS(false);
    return { cleared: cleared };
  }

  // ---- the page's state ----

  function initialState(supported) {
    return {
      supported: Boolean(supported),
      connected: false,
      deviceOk: false,
      busy: false,
      fileOk: false,
      agreed: false,
    };
  }

  // The state after an event. Events: {type: "connected", deviceOk}, "disconnected", "busy",
  // "idle", {type: "file", ok}, {type: "agree", value}.
  function nextState(state, event) {
    const next = Object.assign({}, state);
    switch (event.type) {
      case "connected":
        next.connected = true;
        next.deviceOk = Boolean(event.deviceOk);
        break;
      case "disconnected":
        next.connected = false;
        next.deviceOk = false;
        next.busy = false;
        next.agreed = false; // a new connection can be another reader: tick the box again
        break;
      case "busy":
        next.busy = true;
        break;
      case "idle":
        next.busy = false;
        break;
      case "file":
        next.fileOk = Boolean(event.ok);
        next.agreed = false; // another file: tick the box again
        break;
      case "agree":
        next.agreed = Boolean(event.value);
        break;
      default:
        throw new Error("unknown event: " + event.type);
    }
    return next;
  }

  // Which controls can be used in this state (true = enabled).
  function controls(state) {
    const idle = state.supported && !state.busy;
    const ready = idle && state.connected && state.deviceOk;
    return {
      connect: idle && !state.connected,
      restart: idle && state.connected,
      read: ready,
      pickFile: !state.busy,
      agree: !state.busy && state.fileOk,
      write: ready && state.fileOk && state.agreed,
    };
  }

  return {
    USB_VENDOR_ESPRESSIF: USB_VENDOR_ESPRESSIF,
    CHIP_NAME: CHIP_NAME,
    FLASH_SIZE_NAME: FLASH_SIZE_NAME,
    FLASH_BYTES: FLASH_BYTES,
    READ_WINDOW: READ_WINDOW,
    IMAGE_MAGIC: IMAGE_MAGIC,
    RTC_CNTL_OPTION1_REG: RTC_CNTL_OPTION1_REG,
    RTC_CNTL_FORCE_DOWNLOAD_BOOT: RTC_CNTL_FORCE_DOWNLOAD_BOOT,
    RESET_PULSE_MS: RESET_PULSE_MS,
    readWindows: readWindows,
    formatBytes: formatBytes,
    progressText: progressText,
    percent: percent,
    toHex: toHex,
    normaliseMac: normaliseMac,
    backupFilename: backupFilename,
    macFromFilename: macFromFilename,
    compareMac: compareMac,
    checkDevice: checkDevice,
    checkBackupFile: checkBackupFile,
    sha256Hex: sha256Hex,
    md5Hex: md5Hex,
    identify: identify,
    readFlash: readFlash,
    writeOptions: writeOptions,
    restore: restore,
    resetToFirmware: resetToFirmware,
    initialState: initialState,
    nextState: nextState,
    controls: controls,
  };
});
