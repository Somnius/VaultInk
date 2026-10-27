#include "DebugLog.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>

#include <cstring>

#include "CrossPointSettings.h"

namespace debuglog {
namespace {

constexpr const char* DIR = "/debug";
constexpr size_t BUFFER_BYTES = 32 * 1024;  // lines waiting for the next write (PSRAM)
constexpr size_t FILE_BYTES = 256 * 1024;
constexpr int FILES = 4;
constexpr unsigned long FLUSH_MS = 5000;

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
char* buffer = nullptr;  // filled by sink()
char* spare = nullptr;   // swapped in by flush(), so nothing is allocated under the lock
size_t used = 0;
size_t dropped = 0;  // lines lost while the buffer was full
unsigned long lastFlush = 0;
bool flushing = false;
bool active = false;  // the sink is installed

void sink(const char* line) {
  if (!buffer) return;
  const size_t len = strlen(line);
  portENTER_CRITICAL(&lock);
  if (used + len <= BUFFER_BYTES) {
    memcpy(buffer + used, line, len);
    used += len;
  } else {
    ++dropped;
  }
  portEXIT_CRITICAL(&lock);
}

std::string logPath(const int n) { return std::string(DIR) + "/log-" + std::to_string(n) + ".txt"; }

void rotate() {
  Storage.remove(logPath(FILES - 1).c_str());
  for (int n = FILES - 2; n >= 0; --n) {
    const std::string from = logPath(n);
    if (Storage.exists(from.c_str())) Storage.rename(from.c_str(), logPath(n + 1).c_str());
  }
}

}  // namespace

void apply() {
  const bool on = SETTINGS.debugLogs != 0;
  if (on && !buffer) {
    buffer = static_cast<char*>(malloc(BUFFER_BYTES));  // over 4 KB: PSRAM on the device
    spare = static_cast<char*>(malloc(BUFFER_BYTES));
    if (!spare) {
      free(buffer);
      buffer = nullptr;
    }
  }
  active = on && buffer;
  if (active) {
    Storage.ensureDirectoryExists(DIR);
    setLogSink(sink);
    logDebugOn = true;
    LOG_INF("DBG", "Debug log on, version %s", CROSSPOINT_VERSION);
  } else if (!on) {
    flush();
    setLogSink(nullptr);
#ifdef LOG_DBG_RUNTIME
    logDebugOn = false;
#endif
  }
}

void begin(const bool rebootedFromPanic) {
  apply();
  if (!SETTINGS.debugLogs || !rebootedFromPanic || !Storage.exists("/crash_report.txt")) return;
  // Keep each crash: /crash_report.txt is overwritten by the next one.
  for (int n = 0; n < 100; ++n) {
    const std::string path = std::string(DIR) + "/crash-" + std::to_string(n) + ".txt";
    if (Storage.exists(path.c_str())) continue;
    std::string text;
    if (Storage.readFileToString("DBG", "/crash_report.txt", 64 * 1024, text)) {
      HalFile file;
      if (Storage.openFileForWrite("DBG", path, file)) {
        file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        file.close();
      }
    }
    break;
  }
}

void flush() {
  if (!buffer || flushing) return;
  flushing = true;  // the writes below log too; those lines wait for the next flush
  lastFlush = millis();
  // Swap the buffers under the lock; the full one is written outside it.
  char* out = nullptr;
  size_t n = 0;
  size_t lost = 0;
  portENTER_CRITICAL(&lock);
  if (used > 0) {
    out = buffer;
    n = used;
    lost = dropped;
    buffer = spare;
    used = 0;
    dropped = 0;
  }
  portEXIT_CRITICAL(&lock);
  if (n > 0) {
    HalFile file;
    const std::string path = logPath(0);
    if (Storage.exists(path.c_str())) {
      auto probe = Storage.open(path.c_str());
      const size_t size = probe ? probe.fileSize() : 0;
      if (probe) probe.close();
      if (size >= FILE_BYTES) rotate();
    }
    file = Storage.open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);
    if (file) {
      if (lost > 0) {
        char note[64];
        const int len = snprintf(note, sizeof(note), "[%lu] [DBG] %u lines lost (buffer full)\n", millis(),
                                 static_cast<unsigned>(lost));
        file.write(reinterpret_cast<const uint8_t*>(note), static_cast<size_t>(len));
      }
      file.write(reinterpret_cast<const uint8_t*>(out), n);
      file.close();
    }
    spare = out;  // only flush() swaps, and it doesn't run twice at once
  }
  flushing = false;
}

void tick() {
  // The settings screen toggles the setting itself; pick the change up here.
  if (active != (SETTINGS.debugLogs != 0)) apply();
  if (active && millis() - lastFlush >= FLUSH_MS) flush();
}

void clear() {
  if (buffer) {
    portENTER_CRITICAL(&lock);
    used = 0;
    portEXIT_CRITICAL(&lock);
  }
  Storage.removeDir(DIR);
  if (SETTINGS.debugLogs) Storage.ensureDirectoryExists(DIR);
}

}  // namespace debuglog
