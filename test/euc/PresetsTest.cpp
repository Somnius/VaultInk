#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "Presets.h"

using namespace euc;

namespace {
WheelPreset sherman() {
  WheelPreset p;
  snprintf(p.nickname, sizeof(p.nickname), "%s", "My Sherman");
  snprintf(p.maker, sizeof(p.maker), "%s", "Veteran");
  snprintf(p.model, sizeof(p.model), "%s", "Sherman S");
  p.brand = Brand::Veteran;
  snprintf(p.address, sizeof(p.address), "%s", "AA:BB:CC:DD:EE:FF");
  p.addressType = 1;
  p.speedUnit = 1;
  p.tempUnit = -1;
  p.distanceUnit = 0;
  p.alarms.pwmPct = 75.0f;
  p.alarms.speedKmh = 50.0f;
  p.alarms.tempC = 70.0f;
  p.alarms.currentA = 80.0f;
  p.alarms.lowBatteryPct = 15.0f;
  p.alarms.wheelAlarms = false;
  p.refreshSeconds = 15;
  p.lastConnected = 1793540707LL;
  return p;
}

void expectSame(const WheelPreset& a, const WheelPreset& b) {
  EXPECT_STREQ(a.nickname, b.nickname);
  EXPECT_STREQ(a.maker, b.maker);
  EXPECT_STREQ(a.model, b.model);
  EXPECT_EQ(a.brand, b.brand);
  EXPECT_STREQ(a.address, b.address);
  EXPECT_EQ(a.addressType, b.addressType);
  EXPECT_EQ(a.speedUnit, b.speedUnit);
  EXPECT_EQ(a.tempUnit, b.tempUnit);
  EXPECT_EQ(a.distanceUnit, b.distanceUnit);
  EXPECT_FLOAT_EQ(a.alarms.pwmPct, b.alarms.pwmPct);
  EXPECT_FLOAT_EQ(a.alarms.speedKmh, b.alarms.speedKmh);
  EXPECT_FLOAT_EQ(a.alarms.tempC, b.alarms.tempC);
  EXPECT_FLOAT_EQ(a.alarms.currentA, b.alarms.currentA);
  EXPECT_FLOAT_EQ(a.alarms.lowBatteryPct, b.alarms.lowBatteryPct);
  EXPECT_EQ(a.alarms.wheelAlarms, b.alarms.wheelAlarms);
  EXPECT_EQ(a.refreshSeconds, b.refreshSeconds);
  EXPECT_EQ(a.lastConnected, b.lastConnected);
}
}  // namespace

TEST(Presets, ValidAddress) {
  EXPECT_TRUE(validAddress("AA:BB:CC:DD:EE:FF"));
  EXPECT_TRUE(validAddress("aa:bb:cc:dd:ee:ff"));
  EXPECT_TRUE(validAddress("01:23:45:67:89:ab"));
  EXPECT_FALSE(validAddress("AA:BB:CC:DD:EE:F"));
  EXPECT_FALSE(validAddress("AA:BB:CC:DD:EE:FF:"));
  EXPECT_FALSE(validAddress("AA-BB-CC-DD-EE-FF"));
  EXPECT_FALSE(validAddress("GG:BB:CC:DD:EE:FF"));
  EXPECT_FALSE(validAddress(""));
  EXPECT_FALSE(validAddress(nullptr));
}

TEST(Presets, RefreshChoices) {
  EXPECT_TRUE(validRefresh(5));
  EXPECT_TRUE(validRefresh(10));
  EXPECT_TRUE(validRefresh(15));
  EXPECT_TRUE(validRefresh(30));
  EXPECT_FALSE(validRefresh(0));
  EXPECT_FALSE(validRefresh(1));
  EXPECT_FALSE(validRefresh(20));
  EXPECT_FALSE(validRefresh(60));
}

TEST(Presets, UnitOverrides) {
  UnitSettings global;
  global.speed = SpeedUnit::Kmh;
  global.temp = TempUnit::Celsius;
  global.distance = DistanceUnit::Km;
  const WheelPreset p = sherman();  // speed mph, temp none, distance km
  const UnitSettings u = p.units(global);
  EXPECT_EQ(u.speed, SpeedUnit::Mph);
  EXPECT_EQ(u.temp, TempUnit::Celsius);
  EXPECT_EQ(u.distance, DistanceUnit::Km);
}

TEST(Presets, RoundTripOneWheel) {
  PresetFile f;
  ASSERT_TRUE(f.upsert(sherman()));
  char buf[kPresetFileCapacity];
  const int n = serializePresets(f, buf, sizeof(buf));
  ASSERT_GT(n, 0);
  ASSERT_LT(static_cast<size_t>(n), sizeof(buf));
  EXPECT_NE(std::strstr(buf, "version=1\n"), nullptr);
  EXPECT_NE(std::strstr(buf, "[wheel]\nnickname=My Sherman\n"), nullptr);
  PresetFile back;
  ASSERT_TRUE(parsePresets(buf, static_cast<size_t>(n), back));
  ASSERT_EQ(back.count, 1);
  expectSame(back.wheels[0], sherman());
}

TEST(Presets, RoundTripSixteenWheelsFitsCapacity) {
  PresetFile f;
  for (int i = 0; i < 16; ++i) {
    WheelPreset p = sherman();
    snprintf(p.nickname, sizeof(p.nickname), "Wheel number %02d xyz", i);  // 20 chars: near the field cap
    snprintf(p.address, sizeof(p.address), "AA:BB:CC:DD:EE:%02X", i);
    p.lastConnected = 9223372036854775807LL;  // widest value
    p.alarms.speedKmh = 199.9f;
    p.alarms.currentA = 499.9f;
    ASSERT_TRUE(f.upsert(p));
  }
  WheelPreset extra = sherman();
  snprintf(extra.address, sizeof(extra.address), "%s", "00:00:00:00:00:01");
  EXPECT_FALSE(f.upsert(extra));  // full
  char buf[kPresetFileCapacity];
  const int n = serializePresets(f, buf, sizeof(buf));
  ASSERT_LT(static_cast<size_t>(n), sizeof(buf));
  PresetFile back;
  ASSERT_TRUE(parsePresets(buf, static_cast<size_t>(n), back));
  ASSERT_EQ(back.count, 16);
  for (int i = 0; i < 16; ++i) {
    expectSame(back.wheels[i], f.wheels[i]);
  }
}

TEST(Presets, UpsertReplacesByAddressCaseInsensitive) {
  PresetFile f;
  f.upsert(sherman());
  WheelPreset p = sherman();
  snprintf(p.address, sizeof(p.address), "%s", "aa:bb:cc:dd:ee:ff");
  snprintf(p.nickname, sizeof(p.nickname), "%s", "Renamed");
  EXPECT_TRUE(f.upsert(p));
  EXPECT_EQ(f.count, 1);
  EXPECT_STREQ(f.wheels[0].nickname, "Renamed");
  ASSERT_NE(f.findByAddress("AA:BB:CC:DD:EE:FF"), nullptr);
  EXPECT_EQ(f.findByAddress("11:22:33:44:55:66"), nullptr);
}

TEST(Presets, Remove) {
  PresetFile f;
  for (int i = 0; i < 3; ++i) {
    WheelPreset p = sherman();
    snprintf(p.address, sizeof(p.address), "AA:BB:CC:DD:EE:%02X", i);
    snprintf(p.nickname, sizeof(p.nickname), "w%d", i);
    f.upsert(p);
  }
  EXPECT_TRUE(f.remove(1));
  EXPECT_EQ(f.count, 2);
  EXPECT_STREQ(f.wheels[0].nickname, "w0");
  EXPECT_STREQ(f.wheels[1].nickname, "w2");
  EXPECT_FALSE(f.remove(2));
  EXPECT_FALSE(f.remove(99));
}

TEST(Presets, SerializeTruncatesSafely) {
  PresetFile f;
  f.upsert(sherman());
  char small[40];
  const int n = serializePresets(f, small, sizeof(small));
  EXPECT_GT(static_cast<size_t>(n), sizeof(small));
  EXPECT_EQ(small[sizeof(small) - 1], '\0');
  EXPECT_LT(std::strlen(small), sizeof(small));
  char none[1];
  serializePresets(f, none, 0);  // must not write
}

TEST(Presets, ParseTolerantOfUnknownKeysCommentsAndSpaces) {
  const char* text =
      "# VaultEUC presets\n"
      "version=7\n"
      "futureGlobal=abc\n"
      "\n"
      "[wheel]\r\n"
      "  nickname = Spacey \r\n"
      "address=11:22:33:44:55:66\n"
      "brand=2\n"
      "colour=red\n"
      "refresh=30\n"
      "alarmPwm=66.5\n"
      "[wheel]\n"
      "nickname=Second\n"
      "address=aa:bb:cc:dd:ee:00\n";
  PresetFile f;
  ASSERT_TRUE(parsePresets(text, std::strlen(text), f));
  ASSERT_EQ(f.count, 2);
  EXPECT_STREQ(f.wheels[0].nickname, "Spacey");  // "nickname = Spacey " -> key trimmed... value keeps leading space?
  EXPECT_EQ(f.wheels[0].brand, Brand::Kingsong);
  EXPECT_EQ(f.wheels[0].refreshSeconds, 30);
  EXPECT_FLOAT_EQ(f.wheels[0].alarms.pwmPct, 66.5f);
  EXPECT_EQ(f.wheels[0].speedUnit, -1);
  EXPECT_STREQ(f.wheels[1].nickname, "Second");
  EXPECT_EQ(f.wheels[1].refreshSeconds, 10);  // default
}

TEST(Presets, ParseDropsInvalidWheelsAndValues) {
  const char* text =
      "version=1\n"
      "[wheel]\n"
      "nickname=NoAddress\n"
      "[wheel]\n"
      "nickname=BadAddress\n"
      "address=ZZ:BB:CC:DD:EE:FF\n"
      "[wheel]\n"
      "address=AA:BB:CC:DD:EE:01\n"  // no nickname
      "[wheel]\n"
      "nickname=Good\n"
      "address=AA:BB:CC:DD:EE:02\n"
      "brand=99\n"           // out of range: default kept
      "addressType=7\n"      // out of range
      "speedUnit=5\n"        // out of range
      "alarmPwm=500\n"       // out of range
      "alarmTemp=-3\n"       // out of range
      "alarmSpeed=abc\n"     // not a number
      "refresh=7\n"          // not a choice
      "lastConnected=-5\n"   // negative
      "alarmWheel=maybe\n";  // not 0/1
  PresetFile f;
  ASSERT_TRUE(parsePresets(text, std::strlen(text), f));
  ASSERT_EQ(f.count, 1);
  const WheelPreset d;
  EXPECT_STREQ(f.wheels[0].nickname, "Good");
  EXPECT_EQ(f.wheels[0].brand, d.brand);
  EXPECT_EQ(f.wheels[0].addressType, d.addressType);
  EXPECT_EQ(f.wheels[0].speedUnit, -1);
  EXPECT_FLOAT_EQ(f.wheels[0].alarms.pwmPct, d.alarms.pwmPct);
  EXPECT_FLOAT_EQ(f.wheels[0].alarms.tempC, d.alarms.tempC);
  EXPECT_FLOAT_EQ(f.wheels[0].alarms.speedKmh, d.alarms.speedKmh);
  EXPECT_EQ(f.wheels[0].refreshSeconds, d.refreshSeconds);
  EXPECT_EQ(f.wheels[0].lastConnected, 0);
  EXPECT_TRUE(f.wheels[0].alarms.wheelAlarms);
}

TEST(Presets, ParseLongValuesAreBounded) {
  std::string text = "version=1\n[wheel]\nnickname=";
  text += std::string(500, 'N');
  text += "\nmodel=";
  text += std::string(500, 'M');
  text += "\naddress=AA:BB:CC:DD:EE:FF\n";
  text += std::string(3000, 'k') + "=" + std::string(3000, 'v') + "\n";
  PresetFile f;
  ASSERT_TRUE(parsePresets(text.data(), text.size(), f));
  ASSERT_EQ(f.count, 1);
  EXPECT_EQ(std::strlen(f.wheels[0].nickname), sizeof(WheelPreset::nickname) - 1);
  EXPECT_EQ(std::strlen(f.wheels[0].model), sizeof(WheelPreset::model) - 1);
}

TEST(Presets, ParseMoreThanSixteenWheelsKeepsFirstSixteen) {
  std::string text = "version=1\n";
  for (int i = 0; i < 20; ++i) {
    char block[128];
    snprintf(block, sizeof(block), "[wheel]\nnickname=w%d\naddress=AA:BB:CC:DD:EE:%02X\n", i, i);
    text += block;
  }
  PresetFile f;
  ASSERT_TRUE(parsePresets(text.data(), text.size(), f));
  EXPECT_EQ(f.count, 16);
  EXPECT_STREQ(f.wheels[15].nickname, "w15");
}

TEST(Presets, ParseGarbage) {
  PresetFile f;
  EXPECT_FALSE(parsePresets("", 0, f));
  EXPECT_FALSE(parsePresets(nullptr, 0, f));
  const char* noVersion = "[wheel]\nnickname=x\naddress=AA:BB:CC:DD:EE:FF\n";
  EXPECT_FALSE(parsePresets(noVersion, std::strlen(noVersion), f));
  EXPECT_EQ(f.count, 1);  // the wheel itself was readable; the caller decides on a missing version
  unsigned char junk[2048];
  for (size_t i = 0; i < sizeof(junk); ++i) {
    junk[i] = static_cast<unsigned char>((i * 2654435761u) >> 3);
  }
  EXPECT_FALSE(parsePresets(reinterpret_cast<const char*>(junk), sizeof(junk), f));
  EXPECT_EQ(f.count, 0);
  // embedded NUL stops the parse
  const char withNul[] = "version=1\n[wheel]\nnickname=a\naddress=AA:BB:CC:DD:EE:FF\n\0[wheel]\nnickname=b\n";
  EXPECT_TRUE(parsePresets(withNul, sizeof(withNul) - 1, f));
  EXPECT_EQ(f.count, 1);
}

TEST(Presets, SerializeSanitisesLineBreaksInText) {
  PresetFile f;
  WheelPreset p = sherman();
  snprintf(p.nickname, sizeof(p.nickname), "%s", "bad\nname=x");
  f.upsert(p);
  char buf[kPresetFileCapacity];
  const int n = serializePresets(f, buf, sizeof(buf));
  PresetFile back;
  ASSERT_TRUE(parsePresets(buf, static_cast<size_t>(n), back));
  ASSERT_EQ(back.count, 1);
  EXPECT_STREQ(back.wheels[0].nickname, "bad name x");
}
