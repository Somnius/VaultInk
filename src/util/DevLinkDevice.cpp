#include "DevLinkDevice.h"

#ifdef VAULTINK_DEVLINK

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BlePageTurner.h>
#include <DevLinkBench.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <TlsPolicy.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"

// The developer's own builds only (x4pro-dev, x4pro-dev-verify): input injection, the SD card,
// flashing and the download mode. The read-only link of x4pro-dev-user compiles none of it.
#ifdef VAULTINK_DEVLINK_FULL
#include <DevLinkArgs.h>
#include <DevLinkFetch.h>
#include <esp_ota_ops.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>
#include <wolfssl/wolfcrypt/hash.h>

#include <memory>
#include <new>

#include "network/FirmwareFlasher.h"
#include "util/DevLinkPaths.h"
#endif

extern GfxRenderer renderer;  // main.cpp

namespace devlinkdevice {
namespace {

int wakeCause = -1;

// HWCDC::write() with the 1 ms transmit timeout of this firmware gives up on what does not fit its
// queue, and can then push the rest in over bytes that are still queued. So wait until the queue
// (TX_QUEUE_BYTES, set in main.cpp) has room for the whole reply and send it as one write: a log
// line from another task then cannot land in the middle of it either. If the host never drains
// the queue the reply is sent in the pieces that fit, and cut after the second wait.
void send(const char* text, const size_t length) {
  constexpr unsigned long WAIT_MS = 300;
  const auto room = [] { return static_cast<size_t>(std::max(logSerial.availableForWrite(), 0)); };
  unsigned long start = millis();
  while (room() < length && millis() - start < WAIT_MS) delay(1);
  start = millis();
  size_t sent = 0;
  while (sent < length && millis() - start < WAIT_MS) {
    const size_t fits = std::min(room(), length - sent);
    if (fits == 0) {
      delay(1);
      continue;
    }
    sent += logSerial.write(reinterpret_cast<const uint8_t*>(text + sent), fits);
  }
}

void reply(devlink::JsonLine& json) {
  char line[devlink::JsonLine::CAPACITY + 1];
  const char* text = json.finish();
  const size_t length = json.length();
  memcpy(line, text, length);
  line[length] = '\n';
  send(line, length + 1);
}

void replyError(const char* error) {
  devlink::JsonLine json;
  json.addBool("ok", false).addString("error", error);
  reply(json);
}

uint32_t benchMinutes();

// `lite` (CMD:STATUS LITE, the developer's own builds only) reads no battery gauge and so takes no
// RenderLock: it answers while a screen is being drawn, and leaves the three battery fields out.
#ifdef VAULTINK_DEVLINK_FULL
void replyStatus(const unsigned long msSinceInput, const uint32_t droppedLines, const bool lite) {
#else
void replyStatus(const unsigned long msSinceInput, const uint32_t droppedLines) {
  constexpr bool lite = false;
#endif
  uint16_t millivolts = 0;
  uint16_t percent = 0;
  bool charging = false;
  if (!lite) {
    // The themes read the gauge from the render task; the lock keeps the two apart.
    RenderLock lock;
    const BatteryMonitor battery;
    millivolts = battery.readMillivolts();
    charging = battery.isCharging();
    percent = powerManager.getBatteryPercentage();
  }
  const auto heap = HalMemory::getInternalHeap();
  const auto psram = HalMemory::getPsramHeap();

  devlink::JsonLine json;
  json.addBool("ok", true)
      .addString("version", CROSSPOINT_VERSION)
#ifdef VAULTINK_DEVLINK_FULL
      .addString("build", "dev")
#else
      .addString("build", "dev-user")  // the read-only link (x4pro-dev-user)
#endif
      .addBool("verify_github", tlspolicy::VERIFY_GITHUB)  // true only in x4pro-dev-verify
      .addUnsigned("uptime_ms", millis())
      .addUnsigned("heap_free", heap.freeBytes)
      .addUnsigned("heap_min_free", heap.minFreeBytes)
      .addUnsigned("heap_largest", heap.largestBlockBytes)
      .addUnsigned("psram_free", psram.freeBytes);
  if (!lite) json.addUnsigned("battery_mv", millivolts).addUnsigned("battery_percent", percent);
  json.addBool("usb", gpio.isUsbConnected());
  if (!lite) json.addBool("charging", charging);
  json.addInt("wake_cause", wakeCause)
      .addInt("esp_wake_cause", static_cast<int>(esp_sleep_get_wakeup_cause()))
      .addInt("reset_reason", static_cast<int>(esp_reset_reason()))
      .addUnsigned("cpu_mhz", getCpuFrequencyMhz())
      .addBool("light_present", Frontlight.present())
      .addBool("light_on", Frontlight.isOn())
      .addUnsigned("light_brightness", Frontlight.brightness())
      .addUnsigned("light_warmth", Frontlight.warmth())
      .addString("activity", activityManager.currentActivityName())
      .addUnsigned("sleep_timeout_ms", SETTINGS.getSleepTimeoutMs())
      .addUnsigned("idle_ms", msSinceInput)
      .addBool("log_debug", logDebugOn)
      .addUnsigned("bench_minutes", benchMinutes())
      .addUnsigned("dropped_lines", droppedLines);
#ifdef VAULTINK_DEVLINK_FULL
  // What a test on the link steers by. Each only reads a state: no radio is started, no lock taken.
  json.addUnsigned("visit", activityManager.visit())
      .addUnsigned("renders", activityManager.renders())
      .addUnsigned("queued", HalGPIO::devInput().queued())
      .addBool("wifi", WiFi.getMode() != WIFI_MODE_NULL)
      .addBool("ble", bleturner::status().running)
      .addUnsigned("orientation", static_cast<unsigned>(renderer.getOrientation()))
      .addUnsigned("screen_w", static_cast<unsigned>(std::max(renderer.getScreenWidth(), 0)))
      .addUnsigned("screen_h", static_cast<unsigned>(std::max(renderer.getScreenHeight(), 0)));
#endif
  reply(json);
}

// ---- the SD bench logger ----

constexpr char BENCH_DIR[] = "/.vaultink";
constexpr char BENCH_FILE[] = "/.vaultink/bench.csv";
constexpr char BENCH_OLD_FILE[] = "/.vaultink/bench.old.csv";
// The interval in minutes as text. A file and not RTC memory: it must outlive a power-off too.
// No file means off.
constexpr char BENCH_SETTING_FILE[] = "/.vaultink/bench.cfg";

struct BenchState {
  uint32_t minutes = 0;  // 0: off
  uint32_t lastMs = 0;
  uint32_t rows = 0;    // written since boot
  uint32_t failed = 0;  // not written since boot
  bool loaded = false;  // the setting file was read
  bool hasLast = false;
};
BenchState bench;

void loadBench() {
  if (bench.loaded) return;
  bench.loaded = true;
  if (!Storage.exists(BENCH_SETTING_FILE)) return;
  HalFile file = Storage.open(BENCH_SETTING_FILE);
  if (!file) return;
  char text[12];
  const int n = file.read(text, sizeof(text));
  file.close();
  uint32_t minutes = 0;
  if (n > 0 && devlink::parseBenchMinutes(text, static_cast<size_t>(n), minutes)) {
    bench.minutes = minutes;
    if (minutes > 0) LOG_INF("BENCH", "Bench log on, a row every %u min", static_cast<unsigned>(minutes));
  } else {
    LOG_ERR("BENCH", "Could not read %s, bench log off", BENCH_SETTING_FILE);
  }
}

uint32_t benchMinutes() {
  loadBench();
  return bench.minutes;
}

bool saveBench(const uint32_t minutes) {
  if (minutes == 0) return !Storage.exists(BENCH_SETTING_FILE) || Storage.remove(BENCH_SETTING_FILE);
  Storage.ensureDirectoryExists(BENCH_DIR);
  HalFile file = Storage.open(BENCH_SETTING_FILE, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;
  char text[12];
  const int length = snprintf(text, sizeof(text), "%u\n", static_cast<unsigned>(minutes));
  const bool ok = length > 0 && file.write(text, static_cast<size_t>(length)) == static_cast<size_t>(length);
  file.close();
  return ok;
}

size_t benchFileBytes() {
  if (!Storage.exists(BENCH_FILE)) return 0;
  HalFile file = Storage.open(BENCH_FILE);
  if (!file) return 0;
  const size_t size = file.fileSize();
  file.close();
  return size;
}

// One open, one or two small writes, one close. False when the row is not on the card.
bool appendBenchRow(const char* row, const size_t length) {
  Storage.ensureDirectoryExists(BENCH_DIR);
  size_t size = benchFileBytes();
  if (devlink::benchFileAction(size, length) == devlink::BenchFileAction::Rotate) {
    if (Storage.exists(BENCH_OLD_FILE)) Storage.remove(BENCH_OLD_FILE);
    // A file that cannot be moved must not grow past the cap: this row is dropped.
    if (!Storage.rename(BENCH_FILE, BENCH_OLD_FILE)) return false;
    size = 0;
  }
  HalFile file = Storage.open(BENCH_FILE, O_WRONLY | O_CREAT | O_APPEND);
  if (!file) return false;
  bool ok = true;
  if (size == 0) {
    const char* header = devlink::benchHeader();
    ok = file.write(header, strlen(header)) == strlen(header);
  }
  ok = ok && file.write(row, length) == length;
  file.close();
  return ok;
}

void replyBench(const devlink::Parsed& parsed) {
  loadBench();
  devlink::JsonLine json;
  if (parsed.argsLength > 0) {
    uint32_t minutes = 0;
    if (!devlink::parseBenchMinutes(parsed.args, parsed.argsLength, minutes)) {
      replyError("usage: BENCH [minutes 0-1440], 0 is off, 5 is the usual");
      return;
    }
    bench.minutes = minutes;
    bench.hasLast = false;  // the first row with the new setting comes on the next pass
    const bool saved = saveBench(minutes);
    if (!saved) LOG_ERR("BENCH", "Could not save the bench setting");
    json.addBool("ok", true).addBool("saved", saved);
  } else {
    json.addBool("ok", true);
  }
  json.addUnsigned("minutes", bench.minutes)
      .addBool("on", bench.minutes > 0)
      .addString("file", BENCH_FILE)
      .addUnsigned("file_bytes", benchFileBytes())
      .addUnsigned("rows", bench.rows)
      .addUnsigned("failed", bench.failed);
  reply(json);
}

void replyLog(const devlink::Parsed& parsed) {
  uint32_t level = 0;
  if (!devlink::parseUnsigned(parsed.args, parsed.argsLength, level) || level > 3) {
    replyError("usage: LOG <0-3>");
    return;
  }
  devlink::JsonLine json;
#ifdef LOG_DBG_RUNTIME
  // The Logging library fixes the levels when it is compiled; the one thing it can switch while
  // running is whether debug lines print (the switch Settings -> Debug Logs also uses).
  logDebugOn = level >= 2;
  json.addBool("ok", true)
      .addUnsigned("level", level)
      .addBool("log_debug", logDebugOn)
      .addString("note", "only debug lines switch at runtime (2-3 on, 0-1 off); errors and info always print");
#else
  json.addBool("ok", false).addString("error", "this build has no runtime log level");
#endif
  reply(json);
}

#ifdef VAULTINK_DEVLINK_FULL
// Everything from here to the matching #endif is in the developer's own builds only.

// ---- keys and touches (CMD:KEY, CMD:TAP, CMD:SWIPE) ----
// The events go into HalGPIO's queue and are played where the hardware is read, so the screens,
// the gestures and the auto-sleep timer see them as input. The power key is never pressed.

constexpr char QUEUE_FULL[] = "queue full (16 events): wait and send it again";

void replyQueued(devlink::JsonLine& json) {
  json.addBool("ok", true).addUnsigned("queued", HalGPIO::devInput().queued());
  reply(json);
}

void replyKey(const devlink::Parsed& parsed) {
  devlink::KeyArgs key;
  switch (devlink::parseKeyArgs(parsed.args, parsed.argsLength, key)) {
    case devlink::KeyName::Power:
      replyError("POWER is not allowed: the link never presses the power key");
      return;
    case devlink::KeyName::Unknown:
      replyError("usage: KEY <BACK|CONFIRM|LEFT|RIGHT|UP|DOWN> [hold_ms 20-5000]");
      return;
    case devlink::KeyName::Ok:
      break;
  }
  if (!HalGPIO::devInput().queueKey(key.button, key.holdMs)) {
    replyError(QUEUE_FULL);
    return;
  }
  devlink::JsonLine json;
  replyQueued(json);
}

void replyTouch(const devlink::Parsed& parsed, const bool swipe) {
  devlink::TouchArgs touch;
  const bool parsedOk = swipe ? devlink::parseSwipeArgs(parsed.args, parsed.argsLength, touch)
                              : devlink::parseTapArgs(parsed.args, parsed.argsLength, touch);
  if (!parsedOk) {
    replyError(swipe ? "usage: SWIPE x1 y1 x2 y2 [ms 20-5000]" : "usage: TAP x y [hold_ms 20-5000]");
    return;
  }
  if (!gpio.hasTouch()) {
    replyError("this board has no touch screen");
    return;
  }
  // Screen pixels as a screenshot shows them, held inside the screen, then turned into panel
  // pixels with the orientation the screen has now.
  const uint16_t screenWidth = static_cast<uint16_t>(std::max(renderer.getScreenWidth(), 1));
  const uint16_t screenHeight = static_cast<uint16_t>(std::max(renderer.getScreenHeight(), 1));
  const uint16_t panelWidth = renderer.getDisplayWidth();
  const uint16_t panelHeight = renderer.getDisplayHeight();
  const uint8_t orientation = static_cast<uint8_t>(renderer.getOrientation());
  const uint16_t x1 = devlink::clampToExtent(touch.x1, screenWidth);
  const uint16_t y1 = devlink::clampToExtent(touch.y1, screenHeight);
  const uint16_t x2 = devlink::clampToExtent(touch.x2, screenWidth);
  const uint16_t y2 = devlink::clampToExtent(touch.y2, screenHeight);
  uint16_t px1 = 0;
  uint16_t py1 = 0;
  uint16_t px2 = 0;
  uint16_t py2 = 0;
  devlink::logicalToPanel(orientation, x1, y1, panelWidth, panelHeight, px1, py1);
  devlink::logicalToPanel(orientation, x2, y2, panelWidth, panelHeight, px2, py2);
  devlink::InputInjector& input = HalGPIO::devInput();
  input.setPanel(panelWidth, panelHeight);
  if (!input.queueTouch(px1, py1, px2, py2, touch.holdMs)) {
    replyError(QUEUE_FULL);
    return;
  }
  devlink::JsonLine json;
  json.addUnsigned("x", x1).addUnsigned("y", y1);
  if (swipe) json.addUnsigned("x2", x2).addUnsigned("y2", y2);
  replyQueued(json);
}

// ---- the SD card (CMD:LS, CMD:GET, CMD:PUT) and CMD:FLASH ----
// Only the paths devlinkpaths::isAllowed lets through (util/DevLinkPaths.h).

constexpr char PATH_REFUSED[] = "path refused (docs/devlink.md, Security)";
constexpr size_t LS_MAX_ENTRIES = 200;
constexpr uint32_t GET_MAX_BYTES = 2880;  // 3840 base64 characters: the reply line stays under 4 KB
constexpr size_t PUT_MAX_BYTES = 180;     // what fits a 256-byte command line as base64
constexpr uint32_t PUT_MAX_FILE_BYTES = 1024 * 1024;
constexpr uint16_t FLASH_MIN_BATTERY_PERCENT = 30;

void sendText(const char* text) { send(text, strlen(text)); }

void replyLs(const devlink::Parsed& parsed) {
  std::string path(parsed.args, parsed.argsLength);
  if (path.size() > 1 && path.back() == '/') path.pop_back();
  if (!devlinkpaths::isAllowed(path, devlinkpaths::Access::List)) {
    replyError(PATH_REFUSED);
    return;
  }
  HalFile dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    replyError("no such folder");
    return;
  }
  size_t count = 0;
  bool truncated = false;
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (count >= LS_MAX_ENTRIES) {
      truncated = true;
      entry.close();
      break;
    }
    char name[128] = {};
    entry.getName(name, sizeof(name));
    const bool isDir = entry.isDirectory();
    const size_t size = isDir ? 0 : entry.fileSize();
    entry.close();
    // One line an entry, without "ok": the line with "ok" ends the list.
    devlink::JsonLine line;
    line.addString("name", name).addBool("dir", isDir).addUnsigned("size", size);
    reply(line);
    ++count;
  }
  dir.close();
  devlink::JsonLine json;
  json.addBool("ok", true).addUnsigned("count", count).addBool("truncated", truncated);
  reply(json);
}

void replyGet(const devlink::Parsed& parsed) {
  devlink::FileArgs args;
  if (!devlink::parseGetArgs(parsed.args, parsed.argsLength, args) || args.length == 0 || args.length > GET_MAX_BYTES) {
    replyError("usage: GET <path> <offset> <len 1-2880>");
    return;
  }
  const std::string path(args.path, args.pathLength);
  if (!devlinkpaths::isAllowed(path, devlinkpaths::Access::Read)) {
    replyError(PATH_REFUSED);
    return;
  }
  HalFile file = Storage.open(path.c_str());
  if (!file || file.isDirectory()) {
    if (file) file.close();
    replyError("no such file");
    return;
  }
  const size_t size = file.fileSize();
  if (args.offset > size || !file.seek(args.offset)) {
    file.close();
    replyError("offset past the end of the file");
    return;
  }
  // The data goes out as it is read, 540 bytes (720 characters) at a time, so nothing large is
  // held; "len" and "ok" come last and say whether all of it was read.
  char text[96];
  snprintf(text, sizeof(text), "{\"offset\":%u,\"size\":%u,\"data\":\"", static_cast<unsigned>(args.offset),
           static_cast<unsigned>(size));
  sendText(text);
  uint32_t total = 0;
  bool failed = false;
  while (total < args.length) {
    uint8_t raw[540];
    char encoded[720];
    const size_t want = std::min<size_t>(sizeof(raw), args.length - total);
    const int got = file.read(raw, want);
    if (got < 0) {
      failed = true;
      break;
    }
    if (got == 0) break;
    send(encoded, devlink::base64Encode(raw, static_cast<size_t>(got), encoded, sizeof(encoded)));
    total += static_cast<uint32_t>(got);
    if (static_cast<size_t>(got) < want) break;
  }
  file.close();
  if (failed) {
    snprintf(text, sizeof(text), "\",\"len\":%u,\"ok\":false,\"error\":\"read failed\"}\n",
             static_cast<unsigned>(total));
  } else {
    snprintf(text, sizeof(text), "\",\"len\":%u,\"eof\":%s,\"ok\":true}\n", static_cast<unsigned>(total),
             args.offset + total >= size ? "true" : "false");
  }
  sendText(text);
}

void replyPut(const devlink::Parsed& parsed) {
  devlink::FileArgs args;
  if (!devlink::parsePutArgs(parsed.args, parsed.argsLength, args)) {
    replyError("usage: PUT <path> <offset> <base64, 180 bytes at most>");
    return;
  }
  const std::string path(args.path, args.pathLength);
  if (!devlinkpaths::isAllowed(path, devlinkpaths::Access::Write)) {
    replyError(PATH_REFUSED);
    return;
  }
  uint8_t data[PUT_MAX_BYTES];
  size_t length = 0;
  if (!devlink::base64Decode(args.data, args.dataLength, data, sizeof(data), length) || length == 0) {
    replyError("the data is not base64, or is over 180 bytes");
    return;
  }
  if (args.offset > PUT_MAX_FILE_BYTES || args.offset + length > PUT_MAX_FILE_BYTES) {
    replyError("a file written over the link is 1 MB at most");
    return;
  }
  // Offset 0 starts the file again; any other offset must be the file's size, so a part that is
  // sent twice or out of order is refused and not written in the wrong place.
  oflag_t flags = O_WRONLY | O_CREAT | O_TRUNC;
  if (args.offset > 0) {
    size_t size = 0;
    bool isFile = false;
    if (Storage.exists(path.c_str())) {
      HalFile existing = Storage.open(path.c_str());
      if (existing) {
        isFile = !existing.isDirectory();
        size = isFile ? existing.fileSize() : 0;
        existing.close();
      }
    }
    if (!isFile || size != args.offset) {
      devlink::JsonLine json;
      json.addBool("ok", false)
          .addString("error", "the offset must be 0 or the size of the file")
          .addUnsigned("size", size);
      reply(json);
      return;
    }
    flags = O_WRONLY | O_APPEND;
  } else {
    Storage.ensureDirectoryExists(devlinkpaths::parentOf(path).c_str());
  }
  HalFile file = Storage.open(path.c_str(), flags);
  if (!file) {
    replyError("cannot open the file for writing");
    return;
  }
  const bool written = file.write(data, length) == length;
  file.close();
  if (devlinkpaths::lowered(path) == BENCH_SETTING_FILE) {
    // The bench setting changed under the logger: read it again on the next pass.
    bench = BenchState{};
  }
  if (!written) {
    replyError("write failed");
    return;
  }
  devlink::JsonLine json;
  json.addBool("ok", true).addUnsigned("written", length).addUnsigned("size", args.offset + length);
  reply(json);
}

// True when nothing is at the path afterwards.
bool removeIfThere(const char* path) { return !Storage.exists(path) || Storage.remove(path); }

constexpr size_t HASH_CHUNK_BYTES = 2048;

// The SHA-256 of a file on the card, read 2 KB at a time. False when it cannot be read.
bool hashFile(const char* path, uint8_t digest[devlink::SHA256_BYTES]) {
  const std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[HASH_CHUNK_BYTES]);
  const std::unique_ptr<wc_Sha256> sha(new (std::nothrow) wc_Sha256);
  if (!chunk || !sha) return false;
  HalFile file;
  if (!Storage.openFileForRead("DEVLINK", path, file) || !file) return false;
  if (wc_InitSha256(sha.get()) != 0) {
    wc_Sha256Free(sha.get());
    file.close();
    return false;
  }
  bool ok = true;
  while (ok) {
    const int got = file.read(chunk.get(), HASH_CHUNK_BYTES);
    if (got == 0) break;
    ok = got > 0 && wc_Sha256Update(sha.get(), chunk.get(), static_cast<word32>(got)) == 0;
  }
  file.close();
  ok = ok && wc_Sha256Final(sha.get(), digest) == 0;
  wc_Sha256Free(sha.get());
  return ok;
}

// A line every tenth of the way; without "ok", so the host knows more is coming.
void flashProgress(const size_t written, const size_t total, void* ctx) {
  auto* lastTenth = static_cast<unsigned*>(ctx);
  const unsigned tenth = total > 0 ? static_cast<unsigned>(static_cast<uint64_t>(written) * 10 / total) : 0;
  if (tenth == *lastTenth) return;
  *lastTenth = tenth;
  devlink::JsonLine json;
  json.addString("flash", "writing")
      .addUnsigned("written", written)
      .addUnsigned("total", total)
      .addUnsigned("percent", tenth * 10);
  reply(json);
}

void replyFlashRefused(const char* error, const char* reason, const char* slot) {
  devlink::JsonLine json;
  json.addBool("ok", false).addString("error", error).addString("reason", reason).addString("slot", slot);
  reply(json);
}

// The checks of Settings -> SD firmware update (SdFirmwareUpdateActivity): the size against the
// slot, then the whole image (header, segments, checksum, SHA-256, chip, board tag), then
// firmware_flash writes it to the slot that is not running and points the next boot at it. No
// restart here: the host sends CMD:REBOOT when it wants the new firmware.
// "FLASH <sd path> <sha256>" also hashes the file first and refuses one that is not the file the
// host means (the card can hold an older download under the same name).
void replyFlash(const devlink::Parsed& parsed) {
  devlink::FlashArgs args;
  if (!devlink::parseFlashArgs(parsed.args, parsed.argsLength, args)) {
    replyError("usage: FLASH <sd path of a .bin> [sha256, 64 hex]");
    return;
  }
  const std::string path(args.path, args.pathLength);
  if (!devlinkpaths::isAllowed(path, devlinkpaths::Access::Flash)) {
    replyError("path refused: an absolute path to a .bin on the card, no '..', not in the device's own folders");
    return;
  }
  uint16_t percent = 0;
  {
    RenderLock lock;  // the themes read the gauge from the render task
    percent = powerManager.getBatteryPercentage();
  }
  if (percent < FLASH_MIN_BATTERY_PERCENT && !gpio.isUsbConnected()) {
    devlink::JsonLine json;
    json.addBool("ok", false)
        .addString("error", "battery below 30% and no USB power")
        .addUnsigned("battery_percent", percent);
    reply(json);
    return;
  }
  HalFile file;
  if (!Storage.openFileForRead("DEVLINK", path.c_str(), file) || !file) {
    replyError("cannot open the file");
    return;
  }
  const size_t size = file.fileSize();
  file.close();
  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    replyError("no slot to write to");
    return;
  }
  char slot[sizeof(dest->label) + 1] = {};
  memcpy(slot, dest->label, sizeof(dest->label));
  if (size > dest->size) {
    replyFlashRefused("image refused", "TOO_LARGE", slot);
    return;
  }
  if (args.hasSha256) {
    uint8_t digest[devlink::SHA256_BYTES] = {};
    if (!hashFile(path.c_str(), digest)) {
      replyError("the file could not be hashed: nothing was flashed");
      return;
    }
    if (memcmp(digest, args.sha256, sizeof(digest)) != 0) {
      char hex[devlink::SHA256_HEX + 1];
      devlink::sha256Hex(digest, hex);
      devlink::JsonLine json;
      json.addBool("ok", false)
          .addString("error", "sha256 mismatch: nothing was flashed")
          .addString("sha256", hex)
          .addString("slot", slot);
      reply(json);
      return;
    }
  }
  const auto checked = firmware_flash::validateImageFile(path.c_str(), dest->size);
  if (checked != firmware_flash::Result::OK) {
    replyFlashRefused("image refused", firmware_flash::resultName(checked), slot);
    return;
  }
  {
    devlink::JsonLine json;
    json.addString("flash", "checked").addUnsigned("bytes", size).addString("slot", slot);
    reply(json);
  }
  LOG_INF("DEVLINK", "CMD:FLASH %s (%u bytes) to %s", path.c_str(), static_cast<unsigned>(size), slot);
  unsigned lastTenth = 0;
  // Checked again inside, as the Settings screen does: the card can change between the two.
  const auto result = firmware_flash::flashFromSdPath(path.c_str(), flashProgress, &lastTenth);
  if (result != firmware_flash::Result::OK) {
    replyFlashRefused("flash failed", firmware_flash::resultName(result), slot);
    return;
  }
  devlink::JsonLine json;
  json.addBool("ok", true)
      .addString("action", "flash")
      .addString("slot", slot)
      .addUnsigned("bytes", size)
      .addBool("rebooted", false)
      .addString("next", "send CMD:REBOOT to start it");
  reply(json);
}

// ---- CMD:FETCH <http url> <sd path> <sha256> ----
// Copies one file from a computer on the same network to the card, so a build can be sent without
// taking the card out: FETCH, then FLASH, then REBOOT. Plain http to a private IPv4 address only
// (devlink::checkFetchUrl), to a path FLASH could read (a .bin outside the device's folders), with
// the reader already on Wi-Fi: nothing here joins a network or knows a password. The bytes go to
// "<path>.part" as they arrive and are hashed on the way; the part becomes <path> only when its
// SHA-256 is the one asked for, and is removed otherwise. A file already at <path> is replaced
// only under devlinkpaths::fetchMayReplace, and no folder is made but /debug. Like FLASH, it holds
// the main loop until it is done; any byte on the serial port (a new command) stops it, and so do
// the two time limits (devlink::fetchWaitStep).

constexpr char FETCH_USAGE[] = "usage: FETCH http://<private IPv4>[:port]/<file> <sd path of a .bin> <sha256, 64 hex>";
constexpr int32_t FETCH_CONNECT_MS = 5000;
constexpr size_t FETCH_CHUNK_BYTES = 2048;

void fetchProgress(const size_t written, const size_t total, unsigned& lastTenth) {
  const unsigned tenth = total > 0 ? static_cast<unsigned>(static_cast<uint64_t>(written) * 10 / total) : 0;
  if (tenth == lastTenth) return;
  lastTenth = tenth;
  devlink::JsonLine json;
  json.addString("fetch", "writing")
      .addUnsigned("written", written)
      .addUnsigned("total", total)
      .addUnsigned("percent", tenth * 10);
  reply(json);
}

void replyFetchRefused(const char* error, const char* reason) {
  devlink::JsonLine json;
  json.addBool("ok", false).addString("error", error).addString("reason", reason);
  reply(json);
}

// Waits until there is something to read. Nullptr then; else why the transfer stops. The caller
// sets lastByteMs when it has really read a byte.
const char* fetchWait(WiFiClient& client, const unsigned long startMs, const unsigned long lastByteMs) {
  while (true) {
    const devlink::FetchWait step = devlink::fetchWaitStep(millis(), startMs, lastByteMs, logSerial.available() > 0,
                                                           client.available() > 0, client.connected());
    if (step == devlink::FetchWait::Ready) return nullptr;
    if (step != devlink::FetchWait::Wait) return devlink::fetchWaitText(step);
    delay(1);
  }
}

// The answer to a transfer that stopped after the part file was made: the part is removed, and
// "deleted" says whether that worked.
void replyFetchFailed(const char* error, const std::string& part, const uint32_t written, const char* gotHex) {
  const bool deleted = removeIfThere(part.c_str());
  if (!deleted) LOG_ERR("DEVLINK", "CMD:FETCH could not remove %s", part.c_str());
  devlink::JsonLine json;
  json.addBool("ok", false).addString("error", error).addUnsigned("bytes", written);
  if (gotHex) json.addString("sha256", gotHex);
  json.addBool("deleted", deleted);
  if (!deleted) json.addString("left", part.c_str());
  reply(json);
}

void replyFetch(const devlink::Parsed& parsed) {
  devlink::FetchArgs args;
  if (!devlink::parseFetchArgs(parsed.args, parsed.argsLength, args)) {
    replyError(FETCH_USAGE);
    return;
  }
  devlink::FetchUrl url;
  const devlink::UrlCheck urlCheck = devlink::checkFetchUrl(args.url, args.urlLength, url);
  if (urlCheck != devlink::UrlCheck::Ok) {
    replyFetchRefused("url refused", devlink::urlCheckText(urlCheck));
    return;
  }
  const std::string path(args.path, args.pathLength);
  if (!devlinkpaths::isAllowed(path, devlinkpaths::Access::Flash)) {
    replyError("path refused: an absolute path to a .bin on the card, no '..', not in the device's own folders");
    return;
  }
  // What is on the card now. Nothing is written, removed or made before these pass.
  const std::string part = path + devlinkpaths::FETCH_PART_SUFFIX;
  const bool mayReplace = devlinkpaths::fetchMayReplace(path);
  if (!mayReplace && (Storage.exists(path.c_str()) || Storage.exists(part.c_str()))) {
    replyError("a file is already there: FETCH replaces only a .bin whose name starts with fw, or one under /debug");
    return;
  }
  const std::string parent = devlinkpaths::parentOf(path);
  const bool makeParent = parent != "/" && !Storage.exists(parent.c_str());
  if (makeParent && !devlinkpaths::fetchMayCreateFolder(parent)) {
    replyError("the folder is not on the card: FETCH makes no folders (only /debug)");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    replyError("not on Wi-Fi: join a network on the reader first (FETCH starts no radio and knows no password)");
    return;
  }
  uint16_t percent = 0;
  {
    RenderLock lock;  // the themes read the gauge from the render task
    percent = powerManager.getBatteryPercentage();
  }
  if (percent < FLASH_MIN_BATTERY_PERCENT && !gpio.isUsbConnected()) {
    devlink::JsonLine json;
    json.addBool("ok", false)
        .addString("error", "battery below 30% and no USB power")
        .addUnsigned("battery_percent", percent);
    reply(json);
    return;
  }
  // No larger than the slot FLASH would write it to.
  uint32_t maxBytes = devlink::FETCH_MAX_BYTES;
  if (const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr)) {
    maxBytes = std::min<uint32_t>(maxBytes, dest->size);
  }
  const std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[FETCH_CHUNK_BYTES]);
  const std::unique_ptr<wc_Sha256> sha(new (std::nothrow) wc_Sha256);
  if (!chunk || !sha) {
    replyError("out of memory");
    return;
  }

  const unsigned long startMs = millis();
  unsigned long lastByteMs = startMs;
  WiFiClient client;
  if (client.connect(IPAddress(url.ip[0], url.ip[1], url.ip[2], url.ip[3]), url.port, FETCH_CONNECT_MS) == 0) {
    replyError("cannot connect to the server");
    return;
  }
  // HTTP/1.0: the answer is the file as it is, then the server closes. Nothing is sent but these lines.
  char host[32];
  snprintf(host, sizeof(host), "%u.%u.%u.%u:%u", url.ip[0], url.ip[1], url.ip[2], url.ip[3], url.port);
  client.print("GET ");
  client.write(reinterpret_cast<const uint8_t*>(url.path), url.pathLength);
  client.print(" HTTP/1.0\r\nHost: ");
  client.print(host);
  client.print("\r\nConnection: close\r\n\r\n");

  devlink::HttpHead head;
  devlink::HttpHead::State headState = devlink::HttpHead::State::More;
  while (headState == devlink::HttpHead::State::More) {
    if (const char* stopped = fetchWait(client, startMs, lastByteMs)) {
      client.stop();
      replyError(stopped);
      return;
    }
    const int c = client.read();
    if (c < 0) {
      delay(1);  // said to be there and was not: the stall limit ends this, not a busy loop
      continue;
    }
    lastByteMs = millis();
    headState = head.feed(static_cast<char>(c));
  }
  if (headState == devlink::HttpHead::State::Bad) {
    client.stop();
    replyFetchRefused("answer refused", "not an HTTP answer, or its head is too long");
    return;
  }
  const devlink::HeadCheck headCheck = devlink::checkHead(head, maxBytes);
  if (headCheck != devlink::HeadCheck::Ok) {
    client.stop();
    devlink::JsonLine json;
    json.addBool("ok", false)
        .addString("error", "answer refused")
        .addString("reason", devlink::headCheckText(headCheck))
        .addInt("status", head.status());
    reply(json);
    return;
  }
  const uint32_t total = head.contentLength();

  // A part left by a transfer that lost power is ours to remove (mayReplace; refused above otherwise).
  if (!removeIfThere(part.c_str())) {
    client.stop();
    replyError("an old .part file is in the way and cannot be removed");
    return;
  }
  if (makeParent) Storage.ensureDirectoryExists(parent.c_str());
  // O_EXCL: the part is a new file or nothing.
  HalFile file = Storage.open(part.c_str(), O_WRONLY | O_CREAT | O_EXCL);
  if (!file) {
    client.stop();
    replyError("cannot create the .part file");
    return;
  }
  if (wc_InitSha256(sha.get()) != 0) {
    wc_Sha256Free(sha.get());
    client.stop();
    file.close();
    replyFetchFailed("the hash could not be started", part, 0, nullptr);
    return;
  }
  LOG_INF("DEVLINK", "CMD:FETCH %s (%u bytes) to %s", host, static_cast<unsigned>(total), path.c_str());
  {
    devlink::JsonLine json;
    json.addString("fetch", "started").addUnsigned("total", total);
    reply(json);
  }

  uint32_t written = 0;
  unsigned lastTenth = 0;
  const char* failed = nullptr;
  while (written < total) {
    failed = fetchWait(client, startMs, lastByteMs);
    if (failed) break;
    // Never more than the length the head named: what a server sends after it is not written.
    const size_t want = std::min<size_t>(FETCH_CHUNK_BYTES, total - written);
    const int got = client.read(chunk.get(), want);
    if (got <= 0) {
      delay(1);  // lastByteMs is not touched, so the stall limit ends a socket that gives nothing
      continue;
    }
    lastByteMs = millis();
    const size_t count = static_cast<size_t>(got);
    if (file.write(chunk.get(), count) != count) {
      failed = "write failed (is the card full?)";
      break;
    }
    if (wc_Sha256Update(sha.get(), chunk.get(), static_cast<word32>(count)) != 0) {
      failed = "the hash failed";
      break;
    }
    written += static_cast<uint32_t>(count);
    fetchProgress(written, total, lastTenth);
  }
  client.stop();
  file.close();

  uint8_t digest[devlink::SHA256_BYTES] = {};
  if (!failed && wc_Sha256Final(sha.get(), digest) != 0) failed = "the hash failed";
  wc_Sha256Free(sha.get());
  if (failed) {
    replyFetchFailed(failed, part, written, nullptr);
    return;
  }
  char hex[devlink::SHA256_HEX + 1];
  devlink::sha256Hex(digest, hex);
  if (memcmp(digest, args.sha256, sizeof(digest)) != 0) {
    replyFetchFailed("sha256 mismatch: the download was not kept", part, written, hex);
    return;
  }
  // Only now is the file at the path touched: the old one goes (mayReplace), the part takes its name.
  if (Storage.exists(path.c_str()) && !(mayReplace && Storage.remove(path.c_str()))) {
    replyFetchFailed("the file that is already there could not be replaced", part, written, hex);
    return;
  }
  if (!Storage.rename(part.c_str(), path.c_str())) {
    replyFetchFailed("the .part file could not be renamed", part, written, hex);
    return;
  }
  devlink::JsonLine json;
  json.addBool("ok", true)
      .addUnsigned("bytes", written)
      .addString("sha256", hex)
      .addString("path", path.c_str())
      .addUnsigned("ms", millis() - startMs)
      .addString("next", "send CMD:FLASH <sd path> <sha256>, then CMD:REBOOT");
  reply(json);
}

// ---- CMD:WAIT ----
// Answered later, from waitTick() in the main loop, when the reader is at rest: no made-up input
// left in the queue, no screen change or frame pending, the frame of the screen in front painted,
// and (when a name was given) that screen in front. Nothing here waits or takes the RenderLock.

struct PendingWait {
  devlink::WaitGate gate;
  devlink::WaitArgs args;
};
PendingWait pendingWait;

void replyWait(const devlink::Parsed& parsed) {
  devlink::WaitArgs args;
  if (!devlink::parseWaitArgs(parsed.args, parsed.argsLength, args)) {
    replyError("usage: WAIT [activity] [timeout_ms 100-60000], the name 31 bytes at most");
    return;
  }
  // A new WAIT takes the place of one that is still pending; that one gets no reply.
  pendingWait.args = args;
  pendingWait.gate.start(millis(), args.timeoutMs);
}

void answerWait(const bool done) {
  devlink::JsonLine json;
  json.addBool("ok", done);
  if (!done) json.addString("error", "timeout").addString("wanted", pendingWait.args.activity);
  json.addString("activity", activityManager.currentActivityName())
      .addUnsigned("visit", activityManager.visit())
      .addUnsigned("renders", activityManager.renders())
      .addUnsigned("queued", HalGPIO::devInput().queued())
      .addUnsigned("waited_ms", pendingWait.gate.waitedMs());
  reply(json);
}

// ---- CMD:SCREENSHOT in pieces, and CMD:SHOTCRC ----

constexpr size_t SHOT_PIECE_BYTES = 512;       // half the transmit queue
constexpr unsigned long SHOT_STALL_MS = 2000;  // no room for this long: the host is not reading

// Sends all of it, a piece at a time, each only when the transmit queue has room for it (see
// send() for why a write that does not fit is not just tried). False when the host stopped
// reading: the rest is not sent.
bool sendAll(const uint8_t* data, const size_t length) {
  size_t sent = 0;
  unsigned long lastProgress = millis();
  while (sent < length) {
    const size_t room = static_cast<size_t>(std::max(logSerial.availableForWrite(), 0));
    const size_t fits = std::min({room, length - sent, SHOT_PIECE_BYTES});
    if (fits == 0) {
      if (millis() - lastProgress > SHOT_STALL_MS) return false;
      delay(1);
      continue;
    }
    const size_t written = logSerial.write(data + sent, fits);
    if (written > 0) lastProgress = millis();
    sent += written;
  }
  return true;
}

void replyShotCrc() {
  const uint8_t* frame = renderer.getFrameBuffer();
  const size_t size = renderer.getBufferSize();
  if (!frame || size == 0) {
    replyError("no framebuffer");
    return;
  }
  // The buffer is read as the screenshot reads it, without the RenderLock: a frame drawn between
  // the two gives a different number, and the host takes the screenshot again.
  devlink::JsonLine json;
  json.addBool("ok", true)
      .addUnsigned("crc32", devlink::crc32(frame, size))
      .addUnsigned("bytes", size)
      .addUnsigned("visit", activityManager.visit())
      .addUnsigned("renders", activityManager.renders());
  reply(json);
}

#endif  // VAULTINK_DEVLINK_FULL

// Give the last reply time to leave the queue before the USB port goes away. Not flush(): with the
// 1 ms timeout it marks the host as gone after one millisecond without progress.
void drainBeforeRestart() { delay(100); }

}  // namespace

void noteWakeCause(const int cause) { wakeCause = cause; }

void handle(const devlink::Parsed& parsed, const unsigned long msSinceInput, const uint32_t droppedLines) {
  switch (parsed.command) {
    case devlink::Command::Ping: {
      devlink::JsonLine json;
      json.addBool("ok", true).addBool("pong", true).addUnsigned("uptime_ms", millis());
      reply(json);
      return;
    }
    case devlink::Command::Help: {
      devlink::JsonLine json;
      json.addBool("ok", true).addString("commands", devlink::commandList());
      reply(json);
      return;
    }
    case devlink::Command::Status:
#ifdef VAULTINK_DEVLINK_FULL
      replyStatus(msSinceInput, droppedLines, parsed.argsLength == 4 && memcmp(parsed.args, "LITE", 4) == 0);
#else
      replyStatus(msSinceInput, droppedLines);
#endif
      return;
    case devlink::Command::Log:
      replyLog(parsed);
      return;
    case devlink::Command::Bench:
      replyBench(parsed);
      return;
#ifdef VAULTINK_DEVLINK_FULL
    case devlink::Command::Key:
      replyKey(parsed);
      return;
    case devlink::Command::Tap:
      replyTouch(parsed, false);
      return;
    case devlink::Command::Swipe:
      replyTouch(parsed, true);
      return;
    case devlink::Command::Ls:
      replyLs(parsed);
      return;
    case devlink::Command::Get:
      replyGet(parsed);
      return;
    case devlink::Command::Put:
      replyPut(parsed);
      return;
    case devlink::Command::Flash:
      replyFlash(parsed);
      return;
    case devlink::Command::Fetch:
      replyFetch(parsed);
      return;
    case devlink::Command::Wait:
      replyWait(parsed);
      return;
    case devlink::Command::ShotCrc:
      replyShotCrc();
      return;
#endif
    case devlink::Command::Reboot: {
      devlink::JsonLine json;
      json.addBool("ok", true).addString("action", "reboot");
      reply(json);
      drainBeforeRestart();
      ESP.restart();
      return;
    }
#ifdef VAULTINK_DEVLINK_FULL
    case devlink::Command::Bootloader: {
      devlink::JsonLine json;
      json.addBool("ok", true).addString("action", "bootloader");
      reply(json);
      drainBeforeRestart();
      // The ROM reads this bit after a software reset and starts its download mode, with no button
      // held. It is the write the Arduino core's usb_persist_restart(RESTART_BOOTLOADER) makes for
      // the S3 (esp32-hal-tinyusb.c); the bit is RTC_CNTL_FORCE_DOWNLOAD_BOOT in soc/rtc_cntl_reg.h.
      REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
      esp_restart();
      return;
    }
#else
    case devlink::Command::Bootloader:
      // The name is in every build's table; the read-only link does not act on it.
      replyError("BOOTLOADER is not in this build (read-only developer link)");
      return;
#endif
    default:
      replyError("unknown");
      return;
  }
}

#ifdef VAULTINK_DEVLINK_FULL
void waitTick() {
  if (!pendingWait.gate.pending()) return;
  const char* wanted = pendingWait.args.activity;
  const bool atRest = HalGPIO::devInput().queued() == 0 && activityManager.isAtRest() &&
                      (wanted[0] == '\0' || strcmp(wanted, activityManager.currentActivityName()) == 0);
  switch (pendingWait.gate.tick(millis(), atRest)) {
    case devlink::WaitGate::Result::Done:
      answerWait(true);
      return;
    case devlink::WaitGate::Result::TimedOut:
      answerWait(false);
      return;
    default:
      return;
  }
}

void sendScreenshot() {
  const uint8_t* frame = renderer.getFrameBuffer();
  const size_t size = renderer.getBufferSize();
  char text[40];
  snprintf(text, sizeof(text), "SCREENSHOT_START:%u\n", static_cast<unsigned>(size));
  send(text, strlen(text));
  if (!frame || !sendAll(frame, size)) {
    // No end marker: the host sees that bytes are missing and asks again.
    LOG_ERR("DEVLINK", "Screenshot not sent whole: the host stopped reading");
    return;
  }
  sendText("SCREENSHOT_END\n");
}
#endif

void benchTick() {
  loadBench();
  const uint32_t now = millis();
  if (!devlink::benchDue(now, bench.lastMs, bench.hasLast, bench.minutes)) return;

  devlink::BenchRow row;
  {
    // The themes read the gauge from the render task. STATUS waits for that task; a row is not
    // worth holding the main loop for, so this only takes the lock when it is free and otherwise
    // tries again on the next pass. The readings are the ones STATUS and the battery log take.
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) return;
    const BatteryMonitor battery;
    row.batteryMv = battery.readMillivolts();
    row.batteryPercent = powerManager.getBatteryPercentage();
  }
  bench.lastMs = now;
  bench.hasLast = true;

  const int64_t epoch = trustedtime::trustedNow();
  row.epoch = epoch > 0 ? epoch : 0;
  row.uptimeMs = now;
  row.usb = gpio.isUsbConnected();
  row.lightOn = Frontlight.isOn();
  row.lightBrightness = Frontlight.brightness();
  row.lightWarmth = Frontlight.warmth();
  row.cpuMhz = getCpuFrequencyMhz();
  const auto heap = HalMemory::getInternalHeap();
  row.heapFree = heap.freeBytes;
  row.heapMin = heap.minFreeBytes;
  row.psramFree = HalMemory::getPsramHeap().freeBytes;
  row.activity = activityManager.currentActivityName();
  // Both only read a state: neither starts a radio.
  row.wifi = WiFi.getMode() != WIFI_MODE_NULL;
  row.ble = bleturner::status().running;
  row.wakeCause = wakeCause;

  char line[devlink::BENCH_ROW_MAX];
  const size_t length = devlink::formatBenchRow(row, line, sizeof(line));
  if (length > 0 && appendBenchRow(line, length)) {
    ++bench.rows;
  } else {
    // Only logged. The next try is a whole interval away, so a bad card costs one try a row.
    ++bench.failed;
    LOG_ERR("BENCH", "Row not written to %s", BENCH_FILE);
  }
}

}  // namespace devlinkdevice

#endif  // VAULTINK_DEVLINK
