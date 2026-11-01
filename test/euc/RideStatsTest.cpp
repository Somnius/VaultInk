#include <gtest/gtest.h>

#include "RideStats.h"

using namespace euc;

namespace {
Telemetry sample(const float speed, const float volts, const float amps, const float battery) {
  Telemetry t;
  t.speedKmh = speed;
  t.mark(F_SPEED);
  t.voltage = volts;
  t.mark(F_VOLTAGE);
  t.current = amps;
  t.mark(F_CURRENT);
  t.power = volts * amps;
  t.mark(F_POWER);
  t.batteryPct = battery;
  t.mark(F_BATTERY);
  t.tempMos = 35.0f;
  t.mark(F_TEMP_MOS);
  return t;
}
}  // namespace

TEST(RideStats, EmptyIsZero) {
  const RideStats s;
  EXPECT_EQ(s.updates(), 0u);
  EXPECT_EQ(s.durationSec(), 0u);
  EXPECT_FLOAT_EQ(s.avgMovingKmh(), 0.0f);
  EXPECT_FALSE(s.soh().available);
}

TEST(RideStats, DurationMovingAndExtremes) {
  RideStats s;
  uint32_t now = 0;
  // 10 updates stopped, 20 updates at 30 km/h, 10 at 10 km/h, one second apart
  for (int i = 0; i < 10; ++i, now += 1000) {
    s.update(sample(0.0f, 100.0f, 1.0f, 90.0f), now);
  }
  for (int i = 0; i < 20; ++i, now += 1000) {
    s.update(sample(30.0f, 96.0f, 20.0f, 85.0f), now);
  }
  for (int i = 0; i < 10; ++i, now += 1000) {
    s.update(sample(10.0f, 97.0f, -5.0f, 80.0f), now);
  }
  EXPECT_EQ(s.updates(), 40u);
  EXPECT_EQ(s.durationSec(), 39u);
  EXPECT_EQ(s.movingSec(), 30u);
  EXPECT_FLOAT_EQ(s.topSpeedKmh(), 30.0f);
  // 20 s at 30 + 10 s at 10 = 700/3600 km over 30 s -> 23.33 km/h
  EXPECT_NEAR(s.avgMovingKmh(), 23.333f, 0.01f);
  EXPECT_NEAR(s.distanceKm(), 700.0f / 3600.0f, 0.0001f);  // integrated: no trip odometer
  EXPECT_FALSE(s.distanceFromOdometer());
  EXPECT_FLOAT_EQ(s.maxCurrentA(), 20.0f);
  EXPECT_FLOAT_EQ(s.maxPowerW(), 96.0f * 20.0f);
  EXPECT_FLOAT_EQ(s.minVoltage(), 96.0f);
  EXPECT_FLOAT_EQ(s.maxVoltage(), 100.0f);
  EXPECT_FLOAT_EQ(s.startBatteryPct(), 90.0f);
  EXPECT_FLOAT_EQ(s.endBatteryPct(), 80.0f);
  EXPECT_FLOAT_EQ(s.minBatteryPct(), 80.0f);
  EXPECT_FLOAT_EQ(s.maxTempC(), 35.0f);
}

TEST(RideStats, DistanceFromTripOdometerAndReset) {
  RideStats s;
  Telemetry t = sample(20.0f, 80.0f, 5.0f, 50.0f);
  t.mark(F_TRIP);
  t.tripKm = 12.5f;
  s.update(t, 0);
  t.tripKm = 13.0f;
  s.update(t, 1000);
  EXPECT_TRUE(s.distanceFromOdometer());
  EXPECT_NEAR(s.distanceKm(), 0.5f, 0.0001f);
  t.tripKm = 0.1f;  // the rider reset the trip on the wheel
  s.update(t, 2000);
  t.tripKm = 0.4f;
  s.update(t, 3000);
  EXPECT_NEAR(s.distanceKm(), 0.8f, 0.0001f);
}

TEST(RideStats, LongGapIsNotIntegrated) {
  RideStats s;
  s.update(sample(30.0f, 80.0f, 5.0f, 50.0f), 0);
  s.update(sample(30.0f, 80.0f, 5.0f, 50.0f), 1000);
  s.update(sample(30.0f, 80.0f, 5.0f, 50.0f), 1000 + 10 * 60 * 1000);  // a 10 minute BLE drop
  EXPECT_EQ(s.durationSec(), 1u);
  EXPECT_NEAR(s.distanceKm(), 30.0f / 3600.0f, 0.0001f);
}

TEST(RideStats, MissingFieldsAreSkipped) {
  RideStats s;
  Telemetry t;  // nothing valid
  s.update(t, 0);
  s.update(t, 1000);
  EXPECT_EQ(s.durationSec(), 1u);
  EXPECT_FALSE(s.hasVoltage());
  EXPECT_FALSE(s.hasBattery());
  EXPECT_FALSE(s.hasTemp());
  EXPECT_FLOAT_EQ(s.distanceKm(), 0.0f);
}

TEST(RideStats, SeriesFromPackVolts) {
  EXPECT_EQ(RideStats::seriesFromPackVolts(67.2f), 16);
  EXPECT_EQ(RideStats::seriesFromPackVolts(84.0f), 20);
  EXPECT_EQ(RideStats::seriesFromPackVolts(100.8f), 24);
  EXPECT_EQ(RideStats::seriesFromPackVolts(126.0f), 30);
  EXPECT_EQ(RideStats::seriesFromPackVolts(134.4f), 32);
  EXPECT_EQ(RideStats::seriesFromPackVolts(151.2f), 36);  // not a known series: rounds to 36
  EXPECT_EQ(RideStats::seriesFromPackVolts(0.0f), 0);
  EXPECT_EQ(RideStats::seriesFromPackVolts(1000.0f), 0);  // out of NS_MIN..NS_MAX
}

TEST(RideStats, SohEstimateHealthyPack) {
  // A 24S pack (100.8 V) idling at 96 V, with 0.02 Ohm/cell sag (0.48 Ohm total)
  RideStats s;
  s.setPackSeries(24);
  uint32_t now = 0;
  s.update(sample(0.0f, 96.0f, 0.5f, 70.0f), now);  // idle sample sets V_idle
  for (int i = 0; i < 60; ++i) {
    now += 500;
    const float amps = 20.0f + static_cast<float>(i % 5);  // in the 12..150 A window for 24S
    s.update(sample(30.0f, 96.0f - 0.48f * amps, amps, 70.0f), now);
  }
  const SohEstimate soh = s.soh();
  ASSERT_TRUE(soh.available);
  EXPECT_EQ(soh.series, 24);
  EXPECT_EQ(soh.samples, 60);
  EXPECT_NEAR(soh.reqOhm, 0.48f, 0.0125f);  // one histogram bin wide
  // PackInference: 24 * 3.7 V = 88.8 V nominal -> 18 mOhm/cell -> 0.432 Ohm
  EXPECT_NEAR(soh.nominalOhm, 0.432f, 0.0001f);
  EXPECT_NEAR(soh.ratio, 1.11f, 0.05f);
  EXPECT_LT(soh.ratio, RideStats::kSohWornRatio);
  EXPECT_GT(soh.healthPct, 70.0f);
}

TEST(RideStats, SohNeedsSamplesAndSeries) {
  RideStats s;
  s.update(sample(0.0f, 96.0f, 0.5f, 70.0f), 0);
  for (int i = 0; i < 10; ++i) {
    s.update(sample(30.0f, 90.0f, 30.0f, 70.0f), 1000 * (i + 1));
  }
  EXPECT_FALSE(s.soh().available);  // fewer than 50 samples
  // Inferred from the highest voltage seen as upstream does: 96 / 4.2 = 22.86, ceil 23 is not a known series so the
  // rounded value (23) is kept. Only a full charge (100.8 V) or the Catalog gives the true 24.
  EXPECT_EQ(s.soh().series, 23);
}

TEST(RideStats, SohSkipsLowSpeedAndOutOfWindow) {
  RideStats s;
  s.setPackSeries(24);
  s.update(sample(0.0f, 96.0f, 0.5f, 70.0f), 0);
  for (int i = 0; i < 100; ++i) {
    s.update(sample(10.0f, 90.0f, 30.0f, 70.0f), 1000 * (i + 1));  // too slow for the estimator
  }
  EXPECT_EQ(s.soh().samples, 0);
  for (int i = 0; i < 100; ++i) {
    s.update(sample(30.0f, 90.0f, 5.0f, 70.0f), 200000 + 1000 * (i + 1));  // 5 A: under iMin for 24S
  }
  EXPECT_EQ(s.soh().samples, 0);
}

TEST(RideStats, ResetKeepsFixedSeries) {
  RideStats s;
  s.setPackSeries(20);
  s.update(sample(30.0f, 80.0f, 5.0f, 50.0f), 0);
  s.reset();
  EXPECT_EQ(s.updates(), 0u);
  EXPECT_EQ(s.soh().series, 20);
}
