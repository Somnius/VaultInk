#pragma once

#include <cstddef>
#include <cstdint>

#include "Euc.h"

// The ride CSV written to the SD card under /VaultEUC/. Columns follow the DarknessBot-style names that eucviewer
// reads by header (eucviewer static/js/app.js:5087, parser-worker.js buildTrackFromRows) and that euc_soh_kotlin
// knows as DarknessBotColumns (Constants.kt): Date, Speed, Voltage, PWM, Current, Power, Battery level, Total
// mileage, Temperature, Pitch, Roll. No GPS columns (the reader has no GPS); a "Trip distance" column is added at the
// end, where importers ignore extras. Units: km/h, V, %, A, W, %, km, degrees C, degrees, km. Date is
// "YYYY-MM-DD HH:MM:SS.mmm" wall-clock (eucviewer parseDateParts, the naive ISO-like branch).
namespace euc {

struct DateTime {
  uint16_t year = 1970;
  uint8_t month = 1;  // 1..12
  uint8_t day = 1;    // 1..31
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint16_t millis = 0;
};

// Seconds since 1970-01-01 of a wall-clock DateTime (no time zone: both sides are the reader's clock).
int64_t toEpochSeconds(const DateTime& dt);
DateTime fromEpochSeconds(int64_t seconds);

namespace ridelog {

constexpr const char* kFolder = "/VaultEUC";
constexpr const char* kHeader =
    "Date,Speed,Voltage,PWM,Current,Power,Battery level,Total mileage,Temperature,Pitch,Roll,Trip distance";
constexpr size_t kMaxLine = 192;  // a formatted line never exceeds this (buffer size for callers)
constexpr size_t kMaxFileName = 64;

// Writes kHeader plus "\n". Returns characters wanted (snprintf style).
int formatHeader(char* buf, size_t cap);
// One CSV line with a trailing "\n". Fields the wheel did not report are left empty. The temperature column is the
// highest reported temperature (Alarms::maxTemp).
int formatLine(char* buf, size_t cap, const Telemetry& t, const DateTime& when);
// "<nickname>_<YYYY-MM-DD_HH-MM>.csv"; characters FAT does not allow in the nickname become '_', an empty nickname
// becomes "ride"; the nickname is cut so the whole name fits 8.3-free FAT limits (kMaxFileName).
int fileName(char* buf, size_t cap, const char* nickname, const DateTime& when);

}  // namespace ridelog

// What a ride CSV boils down to for the ride list.
struct RideSummary {
  bool valid = false;  // at least one good line
  DateTime start;
  DateTime end;
  uint32_t durationSec = 0;
  float distanceKm = 0.0f;  // from Total mileage when it moved, else Trip distance, else integrated speed
  float topSpeedKmh = 0.0f;
  float avgSpeedKmh = 0.0f;  // mean over lines with speed >= 2 km/h
  float batteryUsedPct = 0.0f;
  float minVoltage = 0.0f;
  float maxVoltage = 0.0f;
  uint32_t lines = 0;     // good data lines
  uint32_t badLines = 0;  // skipped: too long, wrong column count or unparsable date
};

// Streaming parser: feed() the file in any chunking, then finish(). Fixed buffers, no heap.
class RideLogParser {
 public:
  RideLogParser() = default;
  void feed(const char* data, size_t size);
  void finish();
  const RideSummary& summary() const { return summary_; }
  void reset();

 private:
  void line(const char* s, size_t n);
  void dataLine(const char* s, size_t n);

  char buf_[ridelog::kMaxLine + 1] = {};
  size_t len_ = 0;
  bool overflow_ = false;
  bool headerSeen_ = false;
  // column indexes found in the header, -1 when absent
  int colDate_ = -1;
  int colSpeed_ = -1;
  int colVoltage_ = -1;
  int colBattery_ = -1;
  int colTotal_ = -1;
  int colTrip_ = -1;
  RideSummary summary_;
  int64_t startEpoch_ = 0;
  int64_t lastEpoch_ = 0;
  uint16_t lastMillis_ = 0;
  float firstBattery_ = 0.0f;
  float lastBattery_ = 0.0f;
  bool batterySeen_ = false;
  float firstTotal_ = 0.0f;
  float lastTotal_ = 0.0f;
  bool totalSeen_ = false;
  float firstTrip_ = 0.0f;
  float lastTrip_ = 0.0f;
  bool tripSeen_ = false;
  float integratedKm_ = 0.0f;
  double speedSum_ = 0.0;
  uint32_t speedCount_ = 0;
};

}  // namespace euc
