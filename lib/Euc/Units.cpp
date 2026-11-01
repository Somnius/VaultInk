#include "Units.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace euc {

float toSpeed(const float kmh, const SpeedUnit unit) { return unit == SpeedUnit::Mph ? kmh * kMilesPerKm : kmh; }

float toTemp(const float celsius, const TempUnit unit) {
  return unit == TempUnit::Fahrenheit ? celsius * 9.0f / 5.0f + 32.0f : celsius;
}

float toDistance(const float km, const DistanceUnit unit) { return unit == DistanceUnit::Mi ? km * kMilesPerKm : km; }

const char* speedUnitLabel(const SpeedUnit unit) { return unit == SpeedUnit::Mph ? "mph" : "km/h"; }
namespace {
constexpr const char* kDegC = "\xC2\xB0\x43";  // "°C" in UTF-8
constexpr const char* kDegF = "\xC2\xB0\x46";  // "°F"
}  // namespace

const char* tempUnitLabel(const TempUnit unit) { return unit == TempUnit::Fahrenheit ? kDegF : kDegC; }
const char* distanceUnitLabel(const DistanceUnit unit) { return unit == DistanceUnit::Mi ? "mi" : "km"; }

int formatSpeed(char* buf, const size_t cap, const float kmh, const SpeedUnit unit, const int decimals) {
  const int d = std::clamp(decimals, 0, 2);
  return snprintf(buf, cap, "%.*f %s", d, static_cast<double>(toSpeed(kmh, unit)), speedUnitLabel(unit));
}

int formatTemp(char* buf, const size_t cap, const float celsius, const TempUnit unit) {
  const int whole = static_cast<int>(std::lround(toTemp(celsius, unit)));
  return snprintf(buf, cap, "%d%s", whole, tempUnitLabel(unit));
}

int formatDistance(char* buf, const size_t cap, const float km, const DistanceUnit unit) {
  const float v = toDistance(km, unit);
  const int decimals = v < 100.0f ? 1 : 0;
  return snprintf(buf, cap, "%.*f %s", decimals, static_cast<double>(v), distanceUnitLabel(unit));
}

int formatVoltage(char* buf, const size_t cap, const float volts) {
  return snprintf(buf, cap, "%.1f V", static_cast<double>(volts));
}

int formatCurrent(char* buf, const size_t cap, const float amps) {
  return snprintf(buf, cap, "%.1f A", static_cast<double>(amps));
}

int formatPower(char* buf, const size_t cap, const float watts) {
  return snprintf(buf, cap, "%ld W", std::lround(watts));
}

int formatPercent(char* buf, const size_t cap, const float pct) {
  const long p = std::lround(std::clamp(pct, 0.0f, 100.0f));
  return snprintf(buf, cap, "%ld%%", p);
}

int formatDuration(char* buf, const size_t cap, const uint32_t seconds) {
  const uint32_t h = seconds / 3600;
  const uint32_t m = (seconds / 60) % 60;
  const uint32_t s = seconds % 60;
  if (h > 0) {
    return snprintf(buf, cap, "%u:%02u:%02u", h, m, s);
  }
  return snprintf(buf, cap, "%u:%02u", m, s);
}

}  // namespace euc
