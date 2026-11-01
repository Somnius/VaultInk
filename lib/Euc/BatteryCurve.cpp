#include "BatteryCurve.h"

#include <algorithm>
#include <cmath>

namespace euc {

namespace {

struct Curve {
  float full;      // above this: 100 %
  float kneeHigh;  // above this: (v - offsetHigh) / slopeHigh
  float offsetHigh;
  float slopeHigh;
  float kneeLow;  // above this: (v - kneeLow) / slopeLow, else 0
  float slopeLow;
};

// eucData.mc:94-104 (Begode 67.2 V)
constexpr Curve kCurve67{66.8f, 54.4f, kBatteryCurve67Offset, kBatteryCurve67Slope, 52.9f, 0.325f};
// eucData.mc:147-156 (Kingsong 84 V)
constexpr Curve kCurve84{83.5f, 68.0f, 66.5f, 0.17f, 64.0f, 0.45f};
// eucData.mc:107-118 (Veteran 100.8 V)
constexpr Curve kCurve100{100.2f, 81.6f, kBatteryCurve100Offset, kBatteryCurve100Slope, 79.35f, 0.4875f};
// eucData.mc:119-129 and 167-176 (Veteran Patton / Kingsong S20, S22: 126 V)
constexpr Curve kCurve126{125.25f, 102.0f, 99.75f, 0.255f, 96.0f, 0.675f};

constexpr bool near(const float a, const float b) { return std::fabs(a - b) < 0.6f; }

float evaluate(const Curve& c, const float v) {
  if (v > c.full) {
    return 100.0f;
  }
  if (v > c.kneeHigh) {
    return (v - c.offsetHigh) / c.slopeHigh;
  }
  if (v > c.kneeLow) {
    return (v - c.kneeLow) / c.slopeLow;
  }
  return 0.0f;
}

}  // namespace

float batteryPercentFromVoltage(const float volts, const float packNominalVolts) {
  float pct = 0.0f;
  if (near(packNominalVolts, 84.0f)) {
    pct = evaluate(kCurve84, volts);
  } else if (near(packNominalVolts, 100.8f)) {
    pct = evaluate(kCurve100, volts);
  } else if (near(packNominalVolts, 126.0f)) {
    pct = evaluate(kCurve126, volts);
  } else {
    // 67.2 V as is; every other pack (134.4 V Master and unknowns) scaled from it. UNVERIFIED for 134.4 V.
    const float scale = packNominalVolts > 1.0f ? packNominalVolts / 67.2f : 1.0f;
    pct = evaluate(kCurve67, volts / scale);
  }
  return std::clamp(pct, 0.0f, 100.0f);
}

}  // namespace euc
