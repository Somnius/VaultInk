#include "Entropy.h"

#include <Arduino.h>
#include <DiceEntropy.h>
#include <HalPowerManager.h>
#include <TrustedTime.h>

#include <atomic>
#include <ctime>

#if defined(ESP_PLATFORM)
#include <esp_random.h>
#endif

#if defined(ESP_PLATFORM) && !defined(SIMULATOR)
#include <BatteryMonitor.h>
#endif

namespace entropy {

uint32_t seed() {
  static std::atomic<uint32_t> counter{0};
  dice::entropy::Noise n;
#if defined(ESP_PLATFORM)
  n.hardware = esp_random();
#endif
  n.micros = static_cast<uint32_t>(micros());
  n.millis = static_cast<uint32_t>(millis());
  n.counter = counter.fetch_add(1);

  // The date and time of the roll, as yyyymmddhhmmss.
  int64_t now = trustedtime::trustedNow();
  if (now <= 0) now = static_cast<int64_t>(time(nullptr));
  n.epochSeconds = static_cast<uint32_t>(now);
  const time_t stamp = static_cast<time_t>(now);
  tm parts{};
  if (gmtime_r(&stamp, &parts) != nullptr) {
    n.dateTime = static_cast<uint64_t>(parts.tm_year + 1900) * 10000000000ull +
                 static_cast<uint64_t>(parts.tm_mon + 1) * 100000000ull +
                 static_cast<uint64_t>(parts.tm_mday) * 1000000ull + static_cast<uint64_t>(parts.tm_hour) * 10000ull +
                 static_cast<uint64_t>(parts.tm_min) * 100ull + static_cast<uint64_t>(parts.tm_sec);
  }

  n.batteryPercent = powerManager.getBatteryPercentage();
#if defined(ESP_PLATFORM) && !defined(SIMULATOR)
  const BatteryMonitor battery;
  n.batteryMillivolts = battery.readMillivolts();
#endif
  return dice::entropy::seed(n);
}

}  // namespace entropy
