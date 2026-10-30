#include "ScreenLock.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>
#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"
#include "CrossPointState.h"

#ifndef SIMULATOR
#include <esp_rtc_time.h>
#endif

namespace screenlock {
namespace {

constexpr const char* PIN_FILE = "/.vaultink/lock-pin.txt";

// Not secret (the card can be read elsewhere); it only keeps the digits out of plain sight.
std::string digest(const std::string& pin) {
  uint32_t h = 2166136261u;
  for (const char c : std::string("vaultink-lock:") + pin) {
    h ^= static_cast<uint8_t>(c);
    h *= 16777619u;
  }
  char out[16];
  snprintf(out, sizeof(out), "%08lx", static_cast<unsigned long>(h));
  return out;
}

// Microseconds on a clock that keeps running in deep sleep and starts again at a cold start.
uint64_t clockUs() {
#ifdef SIMULATOR
  return static_cast<uint64_t>(time(nullptr)) * 1000000ULL;
#else
  return esp_rtc_get_time_us();
#endif
}

std::string storedDigest() {
  std::string text;
  if (!Storage.readFileToString("LCK", PIN_FILE, 64, text)) return "";
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
  return text;
}

}  // namespace

bool hasPin() { return !storedDigest().empty(); }

bool enabled() {
  if (SETTINGS.lockMode == 2) return true;
  return SETTINGS.lockMode == 1 && hasPin();
}

bool checkPin(const std::string& pin) {
  const std::string stored = storedDigest();
  return !stored.empty() && stored == digest(pin);
}

bool savePin(const std::string& pin) {
  Storage.ensureDirectoryExists("/.vaultink");
  return Storage.writeFile(PIN_FILE, String((digest(pin) + "\n").c_str()));
}

void removePin() { Storage.remove(PIN_FILE); }

void noteSleep() { APP_STATE.lockSleptAtUs = clockUs(); }

bool lockAtBoot() {
  if (!enabled()) return false;
  if (APP_STATE.lockPending) return true;
  const uint64_t now = clockUs();
  const uint64_t slept = APP_STATE.lockSleptAtUs;
  // A clock that went back means a cold start (battery out, power off): lock to be safe.
  if (slept == 0 || now < slept) return true;
  const uint8_t idx = SETTINGS.lockAfter < 8 ? SETTINGS.lockAfter : 4;
  const uint64_t afterMs = static_cast<uint64_t>(CrossPointSettings::LOCK_AFTER_MINUTES[idx]) * 60000ULL;
  return (now - slept) / 1000ULL >= afterMs;
}

void setPending(const bool pending) {
  if (APP_STATE.lockPending == (pending ? 1 : 0)) return;
  APP_STATE.lockPending = pending ? 1 : 0;
  APP_STATE.saveToFile();
}

}  // namespace screenlock
