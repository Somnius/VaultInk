#include "RideStats.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include "Alarms.h"

namespace euc {

namespace {
// Constants.KNOWN_SERIES in euc_soh_kotlin.
constexpr std::array<uint8_t, 12> kKnownSeries = {16, 20, 24, 28, 30, 32, 40, 48, 52, 60, 64, 72};
constexpr float kMaxCellV = 4.2f;      // Constants.MAXIIMAL_CELL_V
constexpr float kNominalCellV = 3.7f;  // Constants.NOMINAL_CELL_V

// PackInference.estimateCellResistanceMohm.
float cellResistanceMohm(const float vNominal) {
  if (vNominal < 80.0f) {
    return 22.0f;
  }
  if (vNominal < 100.0f) {
    return 18.0f;
  }
  if (vNominal < 130.0f) {
    return 15.0f;
  }
  if (vNominal < 150.0f) {
    return 12.0f;
  }
  return 10.0f;
}

// PackInference.chooseBatteryCurrentWindow.
void currentWindow(const uint8_t ns, float& iMin, float& iMax) {
  if (ns <= 16) {
    iMin = 6.0f;
    iMax = 90.0f;
  } else if (ns <= 24) {
    iMin = 12.0f;
    iMax = 150.0f;
  } else if (ns <= 36) {
    iMin = 10.0f;
    iMax = 180.0f;
  } else if (ns <= 56) {
    iMin = 11.0f;
    iMax = 200.0f;
  } else {
    iMin = 3.0f;
    iMax = 200.0f;
  }
}

uint8_t snapSeries(const int ceiled, const int rounded) {
  const bool ceilKnown =
      std::any_of(kKnownSeries.begin(), kKnownSeries.end(), [&](const uint8_t s) { return s == ceiled; });
  const int pick = ceilKnown ? ceiled : rounded;
  if (pick < 8 || pick > 80) {  // Constants.NS_MIN..NS_MAX
    return 0;
  }
  return static_cast<uint8_t>(pick);
}
}  // namespace

uint8_t RideStats::seriesFromPackVolts(const float packMaxVolts) {
  if (packMaxVolts <= 0.0f) {
    return 0;
  }
  return snapSeries(static_cast<int>(std::ceil(packMaxVolts / kMaxCellV)),
                    static_cast<int>(std::lround(packMaxVolts / kMaxCellV)));
}

void RideStats::setPackSeries(const uint8_t series) {
  series_ = series;
  seriesFixed_ = series != 0;
}

void RideStats::reset() {
  const uint8_t series = seriesFixed_ ? series_ : 0;
  const bool fixed = seriesFixed_;
  *this = RideStats();
  series_ = series;
  seriesFixed_ = fixed;
}

float RideStats::avgMovingKmh() const {
  if (movingMs_ == 0) {
    return 0.0f;
  }
  return movingKm_ / (static_cast<float>(movingMs_) / 3600000.0f);
}

void RideStats::updateDistance(const Telemetry& t, const float dtSec) {
  if (t.has(F_TRIP)) {
    if (!odoSeen_) {
      odoSeen_ = true;
      odoStartKm_ = t.tripKm;
    } else if (t.tripKm + 0.0005f < odoLastKm_) {
      // The trip counter went backwards: a reset on the wheel. Bank what we had and start again.
      odoBankedKm_ += odoLastKm_ - odoStartKm_;
      odoStartKm_ = t.tripKm;
    }
    odoLastKm_ = t.tripKm;
    distanceKm_ = odoBankedKm_ + (odoLastKm_ - odoStartKm_);
    return;
  }
  if (odoSeen_) {
    return;  // the wheel reported a trip before; do not mix in an integration
  }
  if (t.has(F_SPEED) && t.speedKmh >= kMovingKmh && dtSec > 0.0f) {
    distanceKm_ += t.speedKmh * dtSec / 3600.0f;
  }
}

void RideStats::updateSoh(const Telemetry& t) {
  if (!t.has(F_VOLTAGE) || !t.has(F_CURRENT)) {
    return;
  }
  if (!seriesFixed_) {
    series_ = seriesFromPackVolts(maxVoltage_);
  }
  const float amps = std::fabs(t.current);
  if (amps < kSohIdleAmps) {
    idleSeen_ = true;
    idleVolts_ = t.voltage;
    return;
  }
  if (!idleSeen_ || series_ == 0 || !t.has(F_SPEED) || t.speedKmh <= kSohSpeedKmh) {
    return;
  }
  float iMin = 0.0f;
  float iMax = 0.0f;
  currentWindow(series_, iMin, iMax);
  if (amps < iMin || amps > iMax) {
    return;
  }
  const float sag = idleVolts_ - t.voltage;
  if (sag <= 0.0f) {
    return;
  }
  const float req = sag / amps;
  const size_t bin = std::min(kSohBins - 1, static_cast<size_t>(req / kSohMaxOhm * static_cast<float>(kSohBins)));
  if (sohBins_[bin] < 0xFFFF && sohSamples_ < 0xFFFF) {
    ++sohBins_[bin];
    ++sohSamples_;
  }
}

SohEstimate RideStats::soh() const {
  SohEstimate s;
  s.samples = sohSamples_;
  s.series = series_;
  if (series_ == 0 || sohSamples_ < kSohMinSamples) {
    return s;
  }
  // Median from the histogram: the bin where the running count passes half.
  const uint32_t half = sohSamples_ / 2;
  uint32_t seen = 0;
  size_t medianBin = kSohBins - 1;
  for (size_t i = 0; i < kSohBins; ++i) {
    seen += sohBins_[i];
    if (seen > half) {
      medianBin = i;
      break;
    }
  }
  s.reqOhm = (static_cast<float>(medianBin) + 0.5f) * kSohMaxOhm / static_cast<float>(kSohBins);
  const float vNominal = static_cast<float>(series_) * kNominalCellV;
  s.nominalOhm = static_cast<float>(series_) * cellResistanceMohm(vNominal) / 1000.0f;
  s.ratio = s.reqOhm / s.nominalOhm;
  s.healthPct = std::clamp(100.0f * s.nominalOhm / s.reqOhm, 0.0f, 100.0f);
  s.available = true;
  return s;
}

void RideStats::update(const Telemetry& t, const uint32_t nowMs) {
  float dtSec = 0.0f;
  if (updates_ > 0) {
    const uint32_t dtMs = nowMs - lastMs_;
    if (dtMs <= kMaxGapMs) {
      durationMs_ += dtMs;
      dtSec = static_cast<float>(dtMs) / 1000.0f;
      if (t.has(F_SPEED) && t.speedKmh >= kMovingKmh) {
        movingMs_ += dtMs;
        movingKm_ += t.speedKmh * dtSec / 3600.0f;
      }
    }
  }
  lastMs_ = nowMs;
  ++updates_;

  if (t.has(F_SPEED)) {
    topSpeedKmh_ = std::max(topSpeedKmh_, t.speedKmh);
  }
  updateDistance(t, dtSec);

  if (t.has(F_CURRENT)) {
    maxCurrentA_ = std::max(maxCurrentA_, std::fabs(t.current));
  }
  if (t.has(F_POWER)) {
    maxPowerW_ = std::max(maxPowerW_, std::fabs(t.power));
  }
  if (t.has(F_VOLTAGE)) {
    if (!voltageSeen_) {
      voltageSeen_ = true;
      minVoltage_ = t.voltage;
      maxVoltage_ = t.voltage;
    }
    minVoltage_ = std::min(minVoltage_, t.voltage);
    maxVoltage_ = std::max(maxVoltage_, t.voltage);
  }
  if (t.has(F_BATTERY)) {
    if (!batterySeen_) {
      batterySeen_ = true;
      startBatteryPct_ = t.batteryPct;
      minBatteryPct_ = t.batteryPct;
    }
    minBatteryPct_ = std::min(minBatteryPct_, t.batteryPct);
    endBatteryPct_ = t.batteryPct;
  }
  float temp = 0.0f;
  if (Alarms::maxTemp(t, temp)) {
    maxTempC_ = tempSeen_ ? std::max(maxTempC_, temp) : temp;
    tempSeen_ = true;
  }
  updateSoh(t);
}

}  // namespace euc
