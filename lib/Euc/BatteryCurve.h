#pragma once

// Battery percent from pack voltage for the wheels that do not report a percent themselves (Begode, Veteran,
// Kingsong). WheelLog's "better percents" piecewise curves as copied by WheelDash
// (.work/euc-repos/WheelDash/source/eucData.mc:90-190). One curve per pack nominal voltage:
//   67.2 V  Begode 16s / Kingsong 14D-16S     (eucData.mc:94-104, the Begode variant; see note below)
//   84 V    Kingsong 18L/16X/S18, Begode 20s   (eucData.mc:147-156)
//   100.8 V Veteran Sherman/Abrams, KS S19     (eucData.mc:107-118, the Veteran variant; see note below)
//   126 V   Veteran Patton, KS S20/S22         (eucData.mc:119-129 and 167-176, identical)
//   134.4 V Begode Master                      UNVERIFIED: no source has a 134 V curve, the 67.2 V one is scaled x2
// Any other pack scales the 67.2 V curve by pack / 67.2 (WheelLog's Begode rule for its voltage multiplier).
namespace euc {

// UNVERIFIED: WheelDash has two 67.2 V curves, Begode (voltage - 53.8) / 0.13 and Kingsong (voltage - 53.2) / 0.136,
// and two 100.8 V curves, Veteran (voltage - 80.7) / 0.195 and Kingsong (voltage - 79.8) / 0.204. This file uses the
// one that reads lower (Begode for 67.2 V, Veteran for 100.8 V) so a low-battery alarm comes earlier, not later.
constexpr float kBatteryCurve67Offset = 53.8f;
constexpr float kBatteryCurve67Slope = 0.13f;
constexpr float kBatteryCurve100Offset = 80.7f;
constexpr float kBatteryCurve100Slope = 0.195f;

// `volts` is the measured pack voltage, `packNominalVolts` one of 67.2, 84, 100.8, 126, 134.4 (anything else is
// scaled from 67.2). Returns 0..100.
float batteryPercentFromVoltage(float volts, float packNominalVolts);

}  // namespace euc
