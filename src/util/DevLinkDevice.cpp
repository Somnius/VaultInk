#include "DevLinkDevice.h"

#ifdef VAULTINK_DEVLINK

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <Logging.h>
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
      .addUnsigned("dropped_lines", droppedLines);
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

}  // namespace devlinkdevice

#endif  // VAULTINK_DEVLINK
