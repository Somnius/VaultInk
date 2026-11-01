#pragma once

#include <cstdint>

#include "Euc.h"

// Accumulates one ride from Telemetry updates: speeds, distance, extremes, battery and a simple battery
// state-of-health estimate. Platform-free, fixed size, no heap.
namespace euc {

// Battery state of health, estimated the way euc_soh_kotlin does (ReqStatsComputer.kt): the pack's equivalent
// resistance R_eq = (V_idle - V) / |I| over points under load, compared with a nominal pack resistance derived from
// the series cell count (PackInference.kt: 22 mOhm/cell below 80 V ... 10 mOhm/cell above 150 V). A ratio near 1 is
// a healthy pack; the upstream tool alarms above 1.8. This is a rough estimate from one ride, not a measurement.
struct SohEstimate {
  bool available = false;  // enough samples under load
  uint16_t samples = 0;
  uint8_t series = 0;       // cells in series (Ns), 0 unknown
  float reqOhm = 0.0f;      // median R_eq
  float nominalOhm = 0.0f;  // expected R for a new pack of this Ns
  float ratio = 0.0f;       // reqOhm / nominalOhm; 1.0 = like new, > 1.8 = worn (upstream's alarm factor)
  float healthPct = 0.0f;   // 100 * clamp(nominal / req), a one-number reading of the ratio; an estimate
};

class RideStats {
 public:
  static constexpr float kMovingKmh = 2.0f;       // the Session rule: below this the wheel is "stopped"
  static constexpr uint32_t kMaxGapMs = 60000;    // longer gaps between updates are not integrated
  static constexpr float kSohSpeedKmh = 20.0f;    // ReqStatsComputer: speedThreshold = 20
  static constexpr float kSohIdleAmps = 3.0f;     // README-core: |I| < 3 A defines V_idle
  static constexpr uint16_t kSohMinSamples = 50;  // Constants.MIN_POINTS
  static constexpr float kSohWornRatio = 1.8f;    // Constants.ABS_REQ_FACTOR

  RideStats() = default;

  // Tells the estimator the pack's series count (from the Catalog's pack voltage); otherwise it is inferred from
  // the highest voltage seen, as the upstream tool does (ceil(Vmax / 4.2) snapped to a known series count).
  void setPackSeries(uint8_t series);
  static uint8_t seriesFromPackVolts(float packMaxVolts);

  void update(const Telemetry& t, uint32_t nowMs);
  void reset();

  uint32_t updates() const { return updates_; }
  uint32_t durationSec() const { return durationMs_ / 1000; }
  uint32_t movingSec() const { return movingMs_ / 1000; }
  float topSpeedKmh() const { return topSpeedKmh_; }
  // Distance travelled while moving divided by moving time; 0 before the first movement.
  float avgMovingKmh() const;
  float distanceKm() const { return distanceKm_; }
  bool distanceFromOdometer() const { return odoSeen_; }
  float maxCurrentA() const { return maxCurrentA_; }
  float maxPowerW() const { return maxPowerW_; }
  float minVoltage() const { return minVoltage_; }
  float maxVoltage() const { return maxVoltage_; }
  float minBatteryPct() const { return minBatteryPct_; }
  float startBatteryPct() const { return startBatteryPct_; }
  float endBatteryPct() const { return endBatteryPct_; }
  bool hasBattery() const { return batterySeen_; }
  bool hasVoltage() const { return voltageSeen_; }
  float maxTempC() const { return maxTempC_; }
  bool hasTemp() const { return tempSeen_; }

  SohEstimate soh() const;

 private:
  void updateDistance(const Telemetry& t, float dtSec);
  void updateSoh(const Telemetry& t);

  static constexpr size_t kSohBins = 64;
  static constexpr float kSohMaxOhm = 0.8f;  // Constants.ABS_REQ_LIMIT; samples above land in the last bin

  uint32_t updates_ = 0;
  uint32_t lastMs_ = 0;
  uint32_t durationMs_ = 0;
  uint32_t movingMs_ = 0;
  float movingKm_ = 0.0f;  // integrated speed while moving (for the average)
  float topSpeedKmh_ = 0.0f;
  float distanceKm_ = 0.0f;
  bool odoSeen_ = false;
  float odoStartKm_ = 0.0f;
  float odoLastKm_ = 0.0f;
  float odoBankedKm_ = 0.0f;  // distance before an odometer reset (trip reset mid-ride)
  float maxCurrentA_ = 0.0f;
  float maxPowerW_ = 0.0f;
  bool voltageSeen_ = false;
  float minVoltage_ = 0.0f;
  float maxVoltage_ = 0.0f;
  bool batterySeen_ = false;
  float minBatteryPct_ = 0.0f;
  float startBatteryPct_ = 0.0f;
  float endBatteryPct_ = 0.0f;
  bool tempSeen_ = false;
  float maxTempC_ = 0.0f;

  uint8_t series_ = 0;
  bool seriesFixed_ = false;
  bool idleSeen_ = false;
  float idleVolts_ = 0.0f;
  uint16_t sohSamples_ = 0;
  uint16_t sohBins_[kSohBins] = {};
};

}  // namespace euc
