#include "DevLinkDevice.h"

#ifdef VAULTINK_DEVLINK

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BlePageTurner.h>
#include <DevLinkBench.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <Logging.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"

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

void replyStatus(const unsigned long msSinceInput, const uint32_t droppedLines) {
  uint16_t millivolts = 0;
  uint16_t percent = 0;
  bool charging = false;
  {
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
      .addString("build", "dev")
      .addUnsigned("uptime_ms", millis())
      .addUnsigned("heap_free", heap.freeBytes)
      .addUnsigned("heap_min_free", heap.minFreeBytes)
      .addUnsigned("heap_largest", heap.largestBlockBytes)
      .addUnsigned("psram_free", psram.freeBytes)
      .addUnsigned("battery_mv", millivolts)
      .addUnsigned("battery_percent", percent)
      .addBool("usb", gpio.isUsbConnected())
      .addBool("charging", charging)
      .addInt("wake_cause", wakeCause)
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
      replyStatus(msSinceInput, droppedLines);
      return;
    case devlink::Command::Log:
      replyLog(parsed);
      return;
    case devlink::Command::Bench:
      replyBench(parsed);
      return;
    case devlink::Command::Reboot: {
      devlink::JsonLine json;
      json.addBool("ok", true).addString("action", "reboot");
      reply(json);
      drainBeforeRestart();
      ESP.restart();
      return;
    }
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
    default:
      replyError("unknown");
      return;
  }
}

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
