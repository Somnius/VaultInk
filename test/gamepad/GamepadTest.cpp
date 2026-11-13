#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "Gamepad.h"

using namespace gamepad;

namespace {

constexpr char kPad[] = "aa:bb:cc:dd:ee:01";
constexpr char kOther[] = "aa:bb:cc:dd:ee:02";

std::string text(const Map& map) {
  char out[kTextLen];
  format(map, out, sizeof(out));
  return out;
}

TEST(Gamepad, AnEmptyMapRoutesNothing) {
  const Map map;
  EXPECT_FALSE(routes(map, kPad));
  EXPECT_FALSE(routes(map, ""));
  EXPECT_FALSE(routes(map, nullptr));
  EXPECT_EQ(lookup(map, 0x030102), gestures::Default);
  EXPECT_EQ(text(map), "");
}

TEST(Gamepad, LearnedButtonsAreLookedUpForTheirPadOnly) {
  Map map;
  EXPECT_EQ(learn(map, kPad, 0x030102, gestures::NextPage), Learned::Saved);
  EXPECT_EQ(learn(map, kPad, 0x030104, gestures::ToggleNight), Learned::Saved);
  EXPECT_EQ(map.count, 2);
  EXPECT_TRUE(routes(map, kPad));
  EXPECT_FALSE(routes(map, kOther));
  EXPECT_EQ(lookup(map, 0x030102), gestures::NextPage);
  EXPECT_EQ(lookup(map, 0x030104), gestures::ToggleNight);
  EXPECT_EQ(lookup(map, 0x030108), gestures::Default);
  EXPECT_EQ(buttonFor(map, gestures::ToggleNight), 0x030104u);
  EXPECT_EQ(buttonFor(map, gestures::Bookmark), 0u);
}

TEST(Gamepad, AnActionHasOneButton) {
  Map map;
  learn(map, kPad, 0x000101, gestures::Bookmark);
  EXPECT_EQ(learn(map, kPad, 0x000102, gestures::Bookmark), Learned::Saved);
  EXPECT_EQ(map.count, 1);
  EXPECT_EQ(lookup(map, 0x000101), gestures::Default);
  EXPECT_EQ(lookup(map, 0x000102), gestures::Bookmark);
}

TEST(Gamepad, AButtonTakenByAnotherActionIsAConflictAndMoves) {
  Map map;
  learn(map, kPad, 0x000101, gestures::NextPage);
  learn(map, kPad, 0x000102, gestures::PrevPage);
  EXPECT_EQ(learn(map, kPad, 0x000101, gestures::GoHome), Learned::Moved);
  EXPECT_EQ(map.count, 2);
  EXPECT_EQ(lookup(map, 0x000101), gestures::GoHome);
  EXPECT_EQ(buttonFor(map, gestures::NextPage), 0u);
  EXPECT_EQ(lookup(map, 0x000102), gestures::PrevPage);
  // The same button for the same action again is not a conflict.
  EXPECT_EQ(learn(map, kPad, 0x000101, gestures::GoHome), Learned::Saved);
  EXPECT_EQ(map.count, 2);
}

TEST(Gamepad, AnotherPadStartsANewMap) {
  Map map;
  learn(map, kPad, 0x000101, gestures::NextPage);
  learn(map, kPad, 0x000102, gestures::PrevPage);
  EXPECT_EQ(learn(map, kOther, 0x000108, gestures::Refresh), Learned::NewPad);
  EXPECT_EQ(map.count, 1);
  EXPECT_STREQ(map.addr, kOther);
  EXPECT_FALSE(routes(map, kPad));
  EXPECT_EQ(lookup(map, 0x000101), gestures::Default);
}

TEST(Gamepad, RefusesWhatIsNotAButtonOrAnAction) {
  Map map;
  EXPECT_EQ(learn(map, kPad, 0, gestures::NextPage), Learned::Refused);
  EXPECT_EQ(learn(map, kPad, 0x01000000, gestures::NextPage), Learned::Refused);
  EXPECT_EQ(learn(map, kPad, 0x000101, gestures::Default), Learned::Refused);
  EXPECT_EQ(learn(map, kPad, 0x000101, gestures::None), Learned::Refused);
  EXPECT_EQ(learn(map, kPad, 0x000101, gestures::kActionCount), Learned::Refused);
  EXPECT_EQ(learn(map, kPad, 0x000101, 200), Learned::Refused);
  EXPECT_EQ(learn(map, "", 0x000101, gestures::NextPage), Learned::Refused);
  EXPECT_EQ(learn(map, nullptr, 0x000101, gestures::NextPage), Learned::Refused);
  EXPECT_EQ(learn(map, "aa:bb:cc:dd:ee:01:ff", 0x000101, gestures::NextPage), Learned::Refused);
  EXPECT_EQ(map.count, 0);
  EXPECT_STREQ(map.addr, "");
}

TEST(Gamepad, EveryLearnableActionFitsAtOnce) {
  Map map;
  for (int i = 0; i < kLearnableCount; ++i) {
    EXPECT_NE(learn(map, kPad, 0x000100u + static_cast<uint32_t>(i) + 1, kLearnable[i]), Learned::Refused) << i;
  }
  EXPECT_EQ(map.count, kLearnableCount);
  for (int i = 0; i < kLearnableCount; ++i) {
    EXPECT_EQ(lookup(map, 0x000100u + static_cast<uint32_t>(i) + 1), kLearnable[i]);
  }
  // A full map filled some other way refuses instead of writing past the end.
  Map full;
  snprintf(full.addr, sizeof(full.addr), "%s", kPad);
  full.count = kMaxButtons;
  for (int i = 0; i < kMaxButtons; ++i) {
    full.codes[i] = 0x100u + static_cast<uint32_t>(i);
    full.actions[i] = 100;  // nothing learn() would replace
  }
  EXPECT_EQ(learn(full, kPad, 0x999, gestures::Refresh), Learned::Refused);
  EXPECT_EQ(full.count, kMaxButtons);
}

TEST(Gamepad, ClearAndReset) {
  Map map;
  learn(map, kPad, 0x000101, gestures::NextPage);
  learn(map, kPad, 0x000102, gestures::PrevPage);
  EXPECT_FALSE(clear(map, gestures::Bookmark));
  EXPECT_TRUE(clear(map, gestures::NextPage));
  EXPECT_EQ(map.count, 1);
  EXPECT_EQ(lookup(map, 0x000102), gestures::PrevPage);
  EXPECT_TRUE(clear(map, gestures::PrevPage));
  EXPECT_EQ(map.count, 0);
  EXPECT_STREQ(map.addr, "");  // the last button gone: the map belongs to no pad
  learn(map, kPad, 0x000101, gestures::NextPage);
  reset(map);
  EXPECT_EQ(map.count, 0);
  EXPECT_FALSE(routes(map, kPad));
}

TEST(Gamepad, TheSavedTextRoundTrips) {
  Map map;
  learn(map, kPad, 0x030102, gestures::NextPage);
  learn(map, kPad, 0xFFFF43, gestures::Screenshot);
  learn(map, kPad, 0x000001, gestures::QuickMenu);
  EXPECT_EQ(text(map), "030102:2,FFFF43:14,000001:7");
  Map back;
  EXPECT_EQ(parse(kPad, text(map).c_str(), back), 3);
  EXPECT_STREQ(back.addr, kPad);
  EXPECT_EQ(lookup(back, 0x030102), gestures::NextPage);
  EXPECT_EQ(lookup(back, 0xFFFF43), gestures::Screenshot);
  EXPECT_EQ(lookup(back, 0x000001), gestures::QuickMenu);
  EXPECT_EQ(text(back), text(map));
}

TEST(Gamepad, AFullMapFitsItsTextBuffer) {
  Map map;
  snprintf(map.addr, sizeof(map.addr), "%s", kPad);
  map.count = kMaxButtons;
  for (int i = 0; i < kMaxButtons; ++i) {
    map.codes[i] = 0xFFFFF0u + static_cast<uint32_t>(i);
    map.actions[i] = gestures::Screenshot;
  }
  char out[kTextLen];
  const size_t wrote = format(map, out, sizeof(out));
  EXPECT_EQ(wrote, strlen(out));
  EXPECT_EQ(wrote, static_cast<size_t>(kMaxButtons) * 10 - 1);
  char small[12];
  EXPECT_EQ(format(map, small, sizeof(small)), 0u);
  EXPECT_STREQ(small, "");
}

TEST(Gamepad, ParseDropsWhatMeansNothing) {
  Map map;
  // Bad hex, a zero code, 7 hex digits, no action, an empty action, an unknown action, None and Default, a
  // doubled button and a doubled action; the two good entries are kept.
  EXPECT_EQ(
      parse(kPad, "zz:2,000000:2,1030102:2,0101,0102:,0103:99,0104:1,0105:0,000201:2,000201:3,000202:2,000203:11", map),
      2);
  EXPECT_EQ(lookup(map, 0x000201), gestures::NextPage);
  EXPECT_EQ(lookup(map, 0x000203), gestures::Bookmark);
  EXPECT_EQ(lookup(map, 0x000202), gestures::Default);
  EXPECT_EQ(lookup(map, 0x000104), gestures::Default);
}

TEST(Gamepad, ParseKeepsNothingWithoutAnAddressOrText) {
  Map map;
  learn(map, kPad, 0x000101, gestures::NextPage);
  EXPECT_EQ(parse("", "000101:2", map), 0);
  EXPECT_EQ(map.count, 0);
  EXPECT_EQ(parse(kPad, "", map), 0);
  EXPECT_STREQ(map.addr, "");
  EXPECT_EQ(parse(kPad, nullptr, map), 0);
  EXPECT_EQ(parse(nullptr, "000101:2", map), 0);
  EXPECT_EQ(parse("aa:bb:cc:dd:ee:01:ff", "000101:2", map), 0);
  EXPECT_EQ(parse(kPad, ",,,::,", map), 0);
}

TEST(Gamepad, ParseNeverKeepsMoreThanTheMapHolds) {
  // More entries than slots, each a different button; the actions repeat, so one per learnable action is kept.
  std::string many;
  for (int i = 0; i < 40; ++i) {
    char item[16];
    snprintf(item, sizeof(item), "%06X:%u,", 0x100 + i, static_cast<unsigned>(kLearnable[i % kLearnableCount]));
    many += item;
  }
  Map map;
  EXPECT_EQ(parse(kPad, many.c_str(), map), kLearnableCount);
  EXPECT_LE(map.count, kMaxButtons);
}

TEST(Gamepad, ListeningKeepsAButtonOnItsRelease) {
  Listener l;
  EXPECT_EQ(hear(l, 0x000101, true, false), 0u);
  EXPECT_EQ(hear(l, 0x000102, false, false), 0u);  // the release of some other button
  EXPECT_EQ(hear(l, 0x000101, false, false), 0x000101u);
  EXPECT_EQ(hear(l, 0x000101, false, false), 0u);  // a release with no press before it
}

TEST(Gamepad, ListeningDropsAPadIdlingOnANonZeroByte) {
  Listener l;
  hear(l, 0x000180, true, false);
  EXPECT_EQ(hear(l, 0x000180, false, true), 0u);
  EXPECT_EQ(heardAtTimeout(l), 0u);
}

TEST(Gamepad, ListeningTakesTheLastButtonPressed) {
  Listener l;
  hear(l, 0x000101, true, false);
  hear(l, 0x000102, true, false);
  EXPECT_EQ(hear(l, 0x000101, false, false), 0u);
  EXPECT_EQ(hear(l, 0x000102, false, false), 0x000102u);
}

TEST(Gamepad, AButtonWithNoReleaseIsKeptWhenTheWaitEnds) {
  Listener l;
  EXPECT_EQ(heardAtTimeout(l), 0u);
  hear(l, 0x000101, true, false);
  EXPECT_EQ(heardAtTimeout(l), 0x000101u);
  EXPECT_EQ(heardAtTimeout(l), 0u);
  hear(l, 0, true, false);  // not a button code
  EXPECT_EQ(heardAtTimeout(l), 0u);
}

TEST(Gamepad, ButtonNamesMatchThePageTurnerRows) {
  char out[24];
  formatCode(out, sizeof(out), 0x030102);
  EXPECT_STREQ(out, "3:1=02");
  formatCode(out, sizeof(out), 0xFFFF43);
  EXPECT_STREQ(out, "0x43");
}

TEST(Gamepad, LearnableActionsAreRealActions) {
  for (int i = 0; i < kLearnableCount; ++i) {
    EXPECT_GT(kLearnable[i], gestures::None);
    EXPECT_LT(kLearnable[i], gestures::kActionCount);
    EXPECT_TRUE(learnable(kLearnable[i]));
  }
  EXPECT_FALSE(learnable(gestures::Default));
  EXPECT_FALSE(learnable(gestures::None));
}

}  // namespace
