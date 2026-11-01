#include <gtest/gtest.h>

#include <cstring>

#include "Units.h"

using namespace euc;

TEST(Units, Conversions) {
  EXPECT_FLOAT_EQ(toSpeed(100.0f, SpeedUnit::Kmh), 100.0f);
  EXPECT_NEAR(toSpeed(100.0f, SpeedUnit::Mph), 62.1371f, 0.001f);
  EXPECT_FLOAT_EQ(toTemp(0.0f, TempUnit::Fahrenheit), 32.0f);
  EXPECT_FLOAT_EQ(toTemp(100.0f, TempUnit::Fahrenheit), 212.0f);
  EXPECT_FLOAT_EQ(toTemp(37.0f, TempUnit::Celsius), 37.0f);
  EXPECT_NEAR(toDistance(10.0f, DistanceUnit::Mi), 6.21371f, 0.0001f);
}

TEST(Units, FormatSpeed) {
  char buf[32];
  formatSpeed(buf, sizeof(buf), 25.26f, SpeedUnit::Kmh);
  EXPECT_STREQ(buf, "25 km/h");
  formatSpeed(buf, sizeof(buf), 25.26f, SpeedUnit::Kmh, 1);
  EXPECT_STREQ(buf, "25.3 km/h");
  formatSpeed(buf, sizeof(buf), 32.1869f, SpeedUnit::Mph);
  EXPECT_STREQ(buf, "20 mph");
  formatSpeed(buf, sizeof(buf), 1.0f, SpeedUnit::Kmh, 7);  // decimals clamp to 2
  EXPECT_STREQ(buf, "1.00 km/h");
}

TEST(Units, FormatTempAndDistance) {
  char buf[32];
  formatTemp(buf, sizeof(buf), 30.6f, TempUnit::Celsius);
  EXPECT_STREQ(buf, "31\xC2\xB0\x43");
  formatTemp(buf, sizeof(buf), 30.0f, TempUnit::Fahrenheit);
  EXPECT_STREQ(buf, "86\xC2\xB0\x46");
  formatDistance(buf, sizeof(buf), 12.34f, DistanceUnit::Km);
  EXPECT_STREQ(buf, "12.3 km");
  formatDistance(buf, sizeof(buf), 161.0f, DistanceUnit::Mi);  // 100.04 mi: no decimal from 100 up
  EXPECT_STREQ(buf, "100 mi");
  formatDistance(buf, sizeof(buf), 1234.6f, DistanceUnit::Km);
  EXPECT_STREQ(buf, "1235 km");
}

TEST(Units, FormatElectrical) {
  char buf[32];
  formatVoltage(buf, sizeof(buf), 84.123f);
  EXPECT_STREQ(buf, "84.1 V");
  formatCurrent(buf, sizeof(buf), -12.55f);
  EXPECT_STREQ(buf, "-12.6 A");
  formatPower(buf, sizeof(buf), 1249.6f);
  EXPECT_STREQ(buf, "1250 W");
  formatPercent(buf, sizeof(buf), 78.4f);
  EXPECT_STREQ(buf, "78%");
  formatPercent(buf, sizeof(buf), 140.0f);
  EXPECT_STREQ(buf, "100%");
  formatPercent(buf, sizeof(buf), -3.0f);
  EXPECT_STREQ(buf, "0%");
}

TEST(Units, FormatDuration) {
  char buf[32];
  formatDuration(buf, sizeof(buf), 0);
  EXPECT_STREQ(buf, "0:00");
  formatDuration(buf, sizeof(buf), 723);
  EXPECT_STREQ(buf, "12:03");
  formatDuration(buf, sizeof(buf), 3723);
  EXPECT_STREQ(buf, "1:02:03");
  formatDuration(buf, sizeof(buf), 36 * 3600 + 59);
  EXPECT_STREQ(buf, "36:00:59");
}

TEST(Units, SmallBufferIsTruncatedAndTerminated) {
  char buf[5];
  std::memset(buf, 'x', sizeof(buf));
  const int wanted = formatSpeed(buf, sizeof(buf), 123.4f, SpeedUnit::Kmh, 1);
  EXPECT_GT(wanted, 4);
  EXPECT_EQ(buf[4], '\0');
  EXPECT_EQ(std::strlen(buf), 4u);
  char one[1];
  formatDuration(one, sizeof(one), 59);
  EXPECT_EQ(one[0], '\0');
}
