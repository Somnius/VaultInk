#include <gtest/gtest.h>

#include <string>

#include "GamepadJson.h"

using namespace gamepad;

namespace {

constexpr char kPad[] = "aa:bb:cc:dd:ee:01";

TEST(GamepadJson, AnEmptyMapWritesNoKeys) {
  JsonDocument doc;
  doc["other"] = 1;
  writeJson(Map{}, doc);
  EXPECT_TRUE(doc["gamepadAddr"].isNull());
  EXPECT_TRUE(doc["gamepadButtons"].isNull());
  EXPECT_EQ(doc.size(), 1u);
}

TEST(GamepadJson, AFileWithoutTheKeysLoadsAsAnEmptyMap) {
  JsonDocument doc;
  doc["blePageTurnerEnabled"] = 1;
  Map map;
  learn(map, kPad, 0x000101, gestures::NextPage);
  readJson(map, doc);
  EXPECT_EQ(map.count, 0);
  EXPECT_STREQ(map.addr, "");
}

TEST(GamepadJson, RoundTripsThroughTheSettingsText) {
  Map map;
  learn(map, kPad, 0x030102, gestures::NextPage);
  learn(map, kPad, 0xFFFF43, gestures::ToggleNight);
  JsonDocument doc;
  writeJson(map, doc);
  std::string text;
  serializeJson(doc, text);
  EXPECT_EQ(text, R"({"gamepadAddr":"aa:bb:cc:dd:ee:01","gamepadButtons":"030102:2,FFFF43:8"})");

  JsonDocument read;
  ASSERT_FALSE(deserializeJson(read, text));
  Map back;
  readJson(back, read);
  EXPECT_TRUE(routes(back, kPad));
  EXPECT_EQ(back.count, 2);
  EXPECT_EQ(lookup(back, 0x030102), gestures::NextPage);
  EXPECT_EQ(lookup(back, 0xFFFF43), gestures::ToggleNight);
}

TEST(GamepadJson, GarbageInTheFileIsDropped) {
  Map map;
  JsonDocument doc;
  // Buttons without a pad, a pad without buttons, an address that is too long, the wrong types.
  ASSERT_FALSE(deserializeJson(doc, R"({"gamepadButtons":"030102:2"})"));
  readJson(map, doc);
  EXPECT_EQ(map.count, 0);
  ASSERT_FALSE(deserializeJson(doc, R"({"gamepadAddr":"aa:bb:cc:dd:ee:01"})"));
  readJson(map, doc);
  EXPECT_EQ(map.count, 0);
  EXPECT_STREQ(map.addr, "");
  ASSERT_FALSE(deserializeJson(doc, R"({"gamepadAddr":"aa:bb:cc:dd:ee:01:02:03","gamepadButtons":"030102:2"})"));
  readJson(map, doc);
  EXPECT_EQ(map.count, 0);
  ASSERT_FALSE(deserializeJson(doc, R"({"gamepadAddr":17,"gamepadButtons":[1,2]})"));
  readJson(map, doc);
  EXPECT_EQ(map.count, 0);
  ASSERT_FALSE(
      deserializeJson(doc, R"({"gamepadAddr":"aa:bb:cc:dd:ee:01","gamepadButtons":"junk,030102:200,000104:9"})"));
  readJson(map, doc);
  EXPECT_EQ(map.count, 1);
  EXPECT_EQ(lookup(map, 0x000104), gestures::ToggleFrontlight);
}

}  // namespace
