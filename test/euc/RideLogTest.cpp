#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "RideLog.h"

using namespace euc;

namespace {
Telemetry full() {
  Telemetry t;
  t.speedKmh = 25.5f;
  t.voltage = 98.76f;
  t.pwm = 42.0f;
  t.current = -3.25f;
  t.power = -321.0f;
  t.batteryPct = 77.0f;
  t.totalKm = 1234.567f;
  t.tempMos = 41.0f;
  t.tempMotor = 55.0f;
  t.pitch = 1.5f;
  t.roll = -0.5f;
  t.tripKm = 12.345f;
  t.valid = F_SPEED | F_VOLTAGE | F_PWM | F_CURRENT | F_POWER | F_BATTERY | F_TOTAL | F_TEMP_MOS | F_TEMP_MOTOR |
            F_PITCH | F_ROLL | F_TRIP;
  return t;
}

DateTime at(const int h, const int m, const int s, const int ms = 0) {
  DateTime d;
  d.year = 2026;
  d.month = 11;
  d.day = 1;
  d.hour = static_cast<uint8_t>(h);
  d.minute = static_cast<uint8_t>(m);
  d.second = static_cast<uint8_t>(s);
  d.millis = static_cast<uint16_t>(ms);
  return d;
}

// Builds a ride: `n` lines one second apart, speed 30 km/h, voltage sagging, total mileage advancing.
std::string ride(const int n) {
  char buf[ridelog::kMaxLine];
  std::string out;
  ridelog::formatHeader(buf, sizeof(buf));
  out += buf;
  for (int i = 0; i < n; ++i) {
    Telemetry t = full();
    t.speedKmh = i == 0 ? 0.0f : 30.0f;
    t.voltage = 100.0f - 0.1f * static_cast<float>(i);
    t.batteryPct = 90.0f - static_cast<float>(i) * 0.5f;
    t.totalKm = 1000.0f + 30.0f / 3600.0f * static_cast<float>(i);
    t.tripKm = 30.0f / 3600.0f * static_cast<float>(i);
    ridelog::formatLine(buf, sizeof(buf), t, fromEpochSeconds(toEpochSeconds(at(10, 0, 0)) + i));
    out += buf;
  }
  return out;
}
}  // namespace

TEST(RideLog, EpochRoundTrip) {
  const DateTime d = at(13, 45, 7);
  const int64_t e = toEpochSeconds(d);
  EXPECT_EQ(e, 1793540707LL);  // 2026-11-01T13:45:07Z
  const DateTime back = fromEpochSeconds(e);
  EXPECT_EQ(back.year, 2026);
  EXPECT_EQ(back.month, 11);
  EXPECT_EQ(back.day, 1);
  EXPECT_EQ(back.hour, 13);
  EXPECT_EQ(back.minute, 45);
  EXPECT_EQ(back.second, 7);
  const DateTime epoch = fromEpochSeconds(0);
  EXPECT_EQ(epoch.year, 1970);
  EXPECT_EQ(epoch.month, 1);
  EXPECT_EQ(epoch.day, 1);
  // leap day
  DateTime leap;
  leap.year = 2024;
  leap.month = 2;
  leap.day = 29;
  EXPECT_EQ(toEpochSeconds(leap), 1709164800LL);
}

TEST(RideLog, HeaderMatchesEucviewerNames) {
  char buf[ridelog::kMaxLine];
  ridelog::formatHeader(buf, sizeof(buf));
  EXPECT_STREQ(buf,
               "Date,Speed,Voltage,PWM,Current,Power,Battery level,Total mileage,Temperature,Pitch,Roll,Trip "
               "distance\n");
  EXPECT_EQ(std::strstr(buf, "Latitude"), nullptr);
}

TEST(RideLog, FormatLineFull) {
  char buf[ridelog::kMaxLine];
  const int n = ridelog::formatLine(buf, sizeof(buf), full(), at(9, 5, 3, 120));
  EXPECT_STREQ(buf, "2026-11-01 09:05:03.120,25.5,98.76,42.0,-3.25,-321.0,77.0,1234.567,55.0,1.5,-0.5,12.345\n");
  EXPECT_EQ(n, static_cast<int>(std::strlen(buf)));
  EXPECT_LT(n, static_cast<int>(ridelog::kMaxLine));
}

TEST(RideLog, FormatLineLeavesMissingFieldsEmpty) {
  char buf[ridelog::kMaxLine];
  Telemetry t;
  t.voltage = 84.0f;
  t.mark(F_VOLTAGE);
  ridelog::formatLine(buf, sizeof(buf), t, at(0, 0, 0));
  EXPECT_STREQ(buf, "2026-11-01 00:00:00.000,,84.00,,,,,,,,,\n");
}

TEST(RideLog, FormatLineSmallBuffer) {
  char buf[16];
  const int n = ridelog::formatLine(buf, sizeof(buf), full(), at(0, 0, 0));
  EXPECT_GE(n, static_cast<int>(sizeof(buf)));
  EXPECT_EQ(buf[15], '\0');
}

TEST(RideLog, FileNameSanitised) {
  char buf[ridelog::kMaxFileName];
  ridelog::fileName(buf, sizeof(buf), "Sherman S", at(7, 30, 0));
  EXPECT_STREQ(buf, "Sherman S_2026-11-01_07-30.csv");
  ridelog::fileName(buf, sizeof(buf), "my:wheel/\"v12\"?*|<>\\", at(7, 30, 0));
  EXPECT_STREQ(buf, "my_wheel__v12_2026-11-01_07-30.csv");
  ridelog::fileName(buf, sizeof(buf), "", at(7, 30, 0));
  EXPECT_STREQ(buf, "ride_2026-11-01_07-30.csv");
  ridelog::fileName(buf, sizeof(buf), nullptr, at(7, 30, 0));
  EXPECT_STREQ(buf, "ride_2026-11-01_07-30.csv");
  ridelog::fileName(buf, sizeof(buf), "Lynx\xCE\xB1\xCE\xB2...", at(7, 30, 0));  // non-ASCII, trailing dots
  EXPECT_STREQ(buf, "Lynx_2026-11-01_07-30.csv");
  // a very long nickname is cut so the name stays bounded
  ridelog::fileName(buf, sizeof(buf), "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", at(7, 30, 0));
  EXPECT_EQ(std::strlen(buf), 31u + 21u);
}

TEST(RideLog, ParseRoundTripWholeFile) {
  const std::string text = ride(61);  // 60 s ride
  RideLogParser p;
  p.feed(text.data(), text.size());
  p.finish();
  const RideSummary& s = p.summary();
  ASSERT_TRUE(s.valid);
  EXPECT_EQ(s.lines, 61u);
  EXPECT_EQ(s.badLines, 0u);
  EXPECT_EQ(s.start.hour, 10);
  EXPECT_EQ(s.start.second, 0);
  EXPECT_EQ(s.end.minute, 1);
  EXPECT_EQ(s.end.second, 0);
  EXPECT_EQ(s.durationSec, 60u);
  EXPECT_NEAR(s.distanceKm, 0.5f, 0.002f);  // Total mileage: 60 s at 30 km/h (3 decimals in the file)
  EXPECT_FLOAT_EQ(s.topSpeedKmh, 30.0f);
  EXPECT_FLOAT_EQ(s.avgSpeedKmh, 30.0f);
  EXPECT_NEAR(s.batteryUsedPct, 30.0f, 0.01f);
  EXPECT_NEAR(s.minVoltage, 94.0f, 0.01f);
  EXPECT_NEAR(s.maxVoltage, 100.0f, 0.01f);
}

TEST(RideLog, ParseInChunksOfEverySize) {
  const std::string text = ride(20);
  RideLogParser whole;
  whole.feed(text.data(), text.size());
  whole.finish();
  for (size_t chunk = 1; chunk <= 64; ++chunk) {
    RideLogParser p;
    for (size_t pos = 0; pos < text.size(); pos += chunk) {
      p.feed(text.data() + pos, std::min(chunk, text.size() - pos));
    }
    p.finish();
    EXPECT_EQ(p.summary().lines, whole.summary().lines) << "chunk " << chunk;
    EXPECT_EQ(p.summary().durationSec, whole.summary().durationSec) << "chunk " << chunk;
    EXPECT_FLOAT_EQ(p.summary().distanceKm, whole.summary().distanceKm) << "chunk " << chunk;
    EXPECT_FLOAT_EQ(p.summary().minVoltage, whole.summary().minVoltage) << "chunk " << chunk;
  }
}

TEST(RideLog, DamagedLinesAreSkipped) {
  std::string text = ride(5);
  text += "garbage without commas\n";
  text += "2026-11-01 10:00:07.000,abc,99.0,,,,,,,,,\n";  // bad speed: skipped
  text += "2026-11-01 10:00:08.000,999,99.0,,,,,,,,,\n";  // impossible speed: skipped
  text += "not-a-date,30.0,99.0,,,,,,,,,\n";
  text += std::string(ridelog::kMaxLine + 50, 'x') + "\n";  // too long
  text += "2026-11-01 10:00:09.000,30.0,93.0,40,10,900,87.5,1000.075,41,0,0,0.075\n";
  text += "2026-11-01 10:00:10.000,30.0";  // truncated last line, no newline: date ok, rest short
  RideLogParser p;
  p.feed(text.data(), text.size());
  p.finish();
  const RideSummary& s = p.summary();
  EXPECT_TRUE(s.valid);
  EXPECT_EQ(s.badLines, 5u);
  EXPECT_EQ(s.lines, 7u);
  EXPECT_EQ(s.durationSec, 10u);
  EXPECT_NEAR(s.minVoltage, 93.0f, 0.01f);
  EXPECT_FLOAT_EQ(s.topSpeedKmh, 30.0f);
}

TEST(RideLog, BinaryGarbageNeverCrashes) {
  RideLogParser p;
  unsigned char junk[4096];
  for (size_t i = 0; i < sizeof(junk); ++i) {
    junk[i] = static_cast<unsigned char>((i * 7919u + 13u) & 0xFF);
  }
  for (int round = 0; round < 8; ++round) {
    p.feed(reinterpret_cast<const char*>(junk), sizeof(junk));
  }
  p.finish();
  EXPECT_FALSE(p.summary().valid);
  EXPECT_EQ(p.summary().lines, 0u);
}

TEST(RideLog, EmptyAndHeaderOnly) {
  RideLogParser p;
  p.finish();
  EXPECT_FALSE(p.summary().valid);
  char buf[ridelog::kMaxLine];
  ridelog::formatHeader(buf, sizeof(buf));
  p.feed(buf, std::strlen(buf));
  p.finish();
  EXPECT_FALSE(p.summary().valid);
  EXPECT_EQ(p.summary().badLines, 0u);
}

TEST(RideLog, ForeignColumnOrderAndCrLf) {
  // A file from another app with the columns in a different order, Windows line ends and extra columns.
  const char* text =
      "Speed,Voltage,Date,Battery level,Extra\r\n"
      "0.0,84.0,2026-01-02 08:00:00,100,x\r\n"
      "20.0,83.0,2026-01-02 08:00:10,99,y\r\n"
      "20.0,82.5,2026-01-02 08:00:20,98,z\r\n";
  RideLogParser p;
  p.feed(text, std::strlen(text));
  p.finish();
  const RideSummary& s = p.summary();
  ASSERT_TRUE(s.valid);
  EXPECT_EQ(s.lines, 3u);
  EXPECT_EQ(s.durationSec, 20u);
  EXPECT_NEAR(s.distanceKm, 20.0f * 20.0f / 3600.0f, 0.0001f);  // no mileage columns: integrated speed
  EXPECT_FLOAT_EQ(s.batteryUsedPct, 2.0f);
  EXPECT_FLOAT_EQ(s.minVoltage, 82.5f);
}

TEST(RideLog, TripDistanceFallbackWhenTotalMissing) {
  char buf[ridelog::kMaxLine];
  std::string text;
  ridelog::formatHeader(buf, sizeof(buf));
  text += buf;
  for (int i = 0; i < 3; ++i) {
    Telemetry t;
    t.tripKm = 5.0f + static_cast<float>(i);
    t.mark(F_TRIP);
    ridelog::formatLine(buf, sizeof(buf), t, at(1, 0, i));
    text += buf;
  }
  RideLogParser p;
  p.feed(text.data(), text.size());
  p.finish();
  EXPECT_NEAR(p.summary().distanceKm, 2.0f, 0.0001f);
}
