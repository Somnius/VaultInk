#include <gtest/gtest.h>

#include "BatteryCurve.h"

// Expected values computed by hand from WheelDash source/eucData.mc:90-190 (WheelLog's curves).
namespace euc {

TEST(BatteryCurve, Pack67FollowsWheelDashBegodeCurve) {
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(67.2f, 67.2f), 100.0f);
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(66.81f, 67.2f), 100.0f);
  EXPECT_NEAR(batteryPercentFromVoltage(60.0f, 67.2f), (60.0f - 53.8f) / 0.13f, 0.01f);   // 47.7 %
  EXPECT_NEAR(batteryPercentFromVoltage(54.0f, 67.2f), (54.0f - 52.9f) / 0.325f, 0.01f);  // 3.4 %
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(52.9f, 67.2f), 0.0f);
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(40.0f, 67.2f), 0.0f);
}

TEST(BatteryCurve, Pack84FollowsKingsongCurve) {
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(84.0f, 84.0f), 100.0f);
  EXPECT_NEAR(batteryPercentFromVoltage(75.0f, 84.0f), (75.0f - 66.5f) / 0.17f, 0.01f);  // 50 %
  EXPECT_NEAR(batteryPercentFromVoltage(66.0f, 84.0f), (66.0f - 64.0f) / 0.45f, 0.01f);  // 4.4 %
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(63.0f, 84.0f), 0.0f);
}

TEST(BatteryCurve, Pack100FollowsVeteranCurve) {
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(100.8f, 100.8f), 100.0f);
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(100.3f, 100.8f), 100.0f);
  EXPECT_NEAR(batteryPercentFromVoltage(90.0f, 100.8f), (90.0f - 80.7f) / 0.195f, 0.01f);    // 47.7 %
  EXPECT_NEAR(batteryPercentFromVoltage(80.0f, 100.8f), (80.0f - 79.35f) / 0.4875f, 0.01f);  // 1.3 %
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(79.0f, 100.8f), 0.0f);
}

TEST(BatteryCurve, Pack126FollowsPattonCurve) {
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(126.0f, 126.0f), 100.0f);
  EXPECT_NEAR(batteryPercentFromVoltage(110.0f, 126.0f), (110.0f - 99.75f) / 0.255f, 0.01f);  // 40.2 %
  EXPECT_NEAR(batteryPercentFromVoltage(100.0f, 126.0f), (100.0f - 96.0f) / 0.675f, 0.01f);   // 5.9 %
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(95.0f, 126.0f), 0.0f);
}

TEST(BatteryCurve, Pack134ScalesThe67Curve) {
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(134.4f, 134.4f), 100.0f);
  EXPECT_NEAR(batteryPercentFromVoltage(120.0f, 134.4f), batteryPercentFromVoltage(60.0f, 67.2f), 0.05f);
  EXPECT_FLOAT_EQ(batteryPercentFromVoltage(105.0f, 134.4f), 0.0f);
}

TEST(BatteryCurve, ClampedAndMonotonic) {
  constexpr float kPacks[] = {67.2f, 84.0f, 100.8f, 126.0f, 134.4f};
  for (const float pack : kPacks) {
    float last = -1.0f;
    for (float v = 0.0f; v <= pack + 5.0f; v += 0.25f) {
      const float pct = batteryPercentFromVoltage(v, pack);
      EXPECT_GE(pct, 0.0f);
      EXPECT_LE(pct, 100.0f);
      EXPECT_GE(pct, last - 1e-3f) << "pack " << pack << " V at " << v;
      last = pct;
    }
  }
}

}  // namespace euc
