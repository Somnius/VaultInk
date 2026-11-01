#pragma once

#include <cstddef>
#include <cstdint>

// VaultEUC unit settings and formatting. Telemetry is always metric (km/h, degrees C, km); these helpers convert
// for the screen and format into caller-owned char buffers (snprintf only, never past `cap`). Every format
// function returns the number of characters it wanted to write (like snprintf) and always NUL-terminates when
// cap > 0.
namespace euc {

enum class SpeedUnit : uint8_t { Kmh, Mph };
enum class TempUnit : uint8_t { Celsius, Fahrenheit };
enum class DistanceUnit : uint8_t { Km, Mi };

struct UnitSettings {
  SpeedUnit speed = SpeedUnit::Kmh;
  TempUnit temp = TempUnit::Celsius;
  DistanceUnit distance = DistanceUnit::Km;
};

constexpr float kMilesPerKm = 0.621371f;

float toSpeed(float kmh, SpeedUnit unit);
float toTemp(float celsius, TempUnit unit);
float toDistance(float km, DistanceUnit unit);

// "km/h" / "mph", "°C" / "°F" (UTF-8), "km" / "mi".
const char* speedUnitLabel(SpeedUnit unit);
const char* tempUnitLabel(TempUnit unit);
const char* distanceUnitLabel(DistanceUnit unit);

// "25 km/h", "25.3 km/h" with `decimals` digits (0..2).
int formatSpeed(char* buf, size_t cap, float kmh, SpeedUnit unit, int decimals = 0);
// "31°C" / "88°F", rounded to whole degrees.
int formatTemp(char* buf, size_t cap, float celsius, TempUnit unit);
// "12.3 km" / "7.6 mi", one decimal below 100, none above.
int formatDistance(char* buf, size_t cap, float km, DistanceUnit unit);
// "84.1 V".
int formatVoltage(char* buf, size_t cap, float volts);
// "12.5 A" (negative kept: regeneration).
int formatCurrent(char* buf, size_t cap, float amps);
// "1250 W".
int formatPower(char* buf, size_t cap, float watts);
// "78%" clamped to 0..100.
int formatPercent(char* buf, size_t cap, float pct);
// "12:03" below an hour, "1:02:03" above.
int formatDuration(char* buf, size_t cap, uint32_t seconds);

}  // namespace euc
