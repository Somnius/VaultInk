#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "ReadingPresets.h"

using namespace readingpresets;

namespace {

Layout sampleLayout() {
  Layout l;
  l.fontFamily = 0;
  std::strcpy(l.sdFontFamilyName, "Bookerly");
  l.fontPointSize = 14;
  l.lineSpacing = 6;
  l.paragraphAlignment = 1;
  l.hyphenationEnabled = 1;
  l.focusReadingEnabled = 0;
  l.evenWordGaps = 2;
  l.wordSpacing = 125;
  l.characterSpacing = 3;
  l.screenMargin = 12;
  l.paragraphIndentSpaces = 0;
  l.textLigatures = 1;
  l.textKerning = 0;
  l.textWidowOrphan = 1;
  l.textKeepHeadings = 0;
  l.textHanging = 1;
  return l;
}

std::string toJson(const PresetList& list) {
  char buf[4096];
  const size_t need = serialize(list, buf, sizeof(buf));
  EXPECT_LT(need, sizeof(buf));
  return std::string(buf, need < sizeof(buf) ? need : sizeof(buf) - 1);
}

}  // namespace

TEST(ReadingPresets, DefaultsAreEmptyWithNumberedNames) {
  PresetList list;
  for (int i = 0; i < kSlots; ++i) {
    EXPECT_FALSE(list.slots[i].saved);
    EXPECT_EQ(std::string("Preset ") + std::to_string(i + 1), list.slots[i].name);
  }
  EXPECT_STREQ("Preset 10", list.slots[9].name);
}

TEST(ReadingPresets, RoundTripKeepsEveryField) {
  PresetList list;
  ASSERT_TRUE(save(list, 2, sampleLayout()));
  ASSERT_TRUE(setName(list, 2, "Night \"q\" \\ Σκ"));
  ASSERT_TRUE(save(list, 9, Layout{}));

  const std::string json = toJson(list);
  PresetList back;
  ASSERT_TRUE(parse(json.c_str(), json.size(), back));

  EXPECT_TRUE(back.slots[2].saved);
  EXPECT_EQ(sampleLayout(), back.slots[2].layout);
  EXPECT_STREQ("Night \"q\" \\ Σκ", back.slots[2].name);
  EXPECT_TRUE(back.slots[9].saved);
  EXPECT_EQ(Layout{}, back.slots[9].layout);
  for (int i = 0; i < kSlots; ++i) {
    if (i == 2 || i == 9) continue;
    EXPECT_FALSE(back.slots[i].saved) << i;
    EXPECT_STREQ(list.slots[i].name, back.slots[i].name);
  }
  // Serialising the parsed list gives the same bytes.
  EXPECT_EQ(json, toJson(back));
}

TEST(ReadingPresets, SerializeReportsNeededLengthWhenCut) {
  PresetList list;
  save(list, 0, sampleLayout());
  char small[32];
  const size_t need = serialize(list, small, sizeof(small));
  EXPECT_GT(need, sizeof(small));
  EXPECT_EQ('\0', small[sizeof(small) - 1]);
  EXPECT_EQ(need, serialize(list, nullptr, 0));
}

TEST(ReadingPresets, NameIsTrimmedCleanedAndCutOnUtf8Boundary) {
  PresetList list;
  ASSERT_TRUE(setName(list, 0, "  My\tpreset \n"));
  EXPECT_STREQ("My preset", list.slots[0].name);

  // 13 two-byte Greek letters = 26 bytes; only 12 (24 bytes) fit.
  ASSERT_TRUE(setName(list, 1, "αβγδεζηθικλμν"));
  EXPECT_EQ(24u, std::strlen(list.slots[1].name));
  EXPECT_STREQ("αβγδεζηθικλμ", list.slots[1].name);

  // Blank goes back to the default name.
  ASSERT_TRUE(setName(list, 3, "   "));
  EXPECT_STREQ("Preset 4", list.slots[3].name);
  ASSERT_TRUE(setName(list, 4, nullptr));
  EXPECT_STREQ("Preset 5", list.slots[4].name);

  EXPECT_FALSE(setName(list, -1, "x"));
  EXPECT_FALSE(setName(list, kSlots, "x"));
}

TEST(ReadingPresets, ParseRejectsGarbageAndKeepsDefaults) {
  PresetList list;
  save(list, 0, sampleLayout());
  EXPECT_FALSE(parse("not json", 8, list));
  EXPECT_FALSE(list.slots[0].saved);
  EXPECT_STREQ("Preset 1", list.slots[0].name);
  EXPECT_FALSE(parse(nullptr, 0, list));
  EXPECT_FALSE(parse("{\"slots\":[{\"name\":\"x\"", 22, list));
  EXPECT_FALSE(parse("", 0, list));
  EXPECT_TRUE(parse("{}", 2, list));
}

TEST(ReadingPresets, ParseSkipsUnknownKeysAndExtraSlots) {
  const std::string json =
      "{\"v\":7,\"future\":{\"a\":[1,2,{\"b\":null}]},\"slots\":["
      "{\"name\":\"A\",\"saved\":true,\"size\":16,\"mystery\":\"\\u00e9\\ud83d\\ude00\",\"margin\":300,\"word\":-5},"
      "{\"saved\":false,\"size\":99},"
      "{},{},{},{},{},{},{},{},"
      "{\"name\":\"eleventh\",\"saved\":true}]}";
  PresetList list;
  ASSERT_TRUE(parse(json.c_str(), json.size(), list));
  EXPECT_STREQ("A", list.slots[0].name);
  EXPECT_TRUE(list.slots[0].saved);
  EXPECT_EQ(16, list.slots[0].layout.fontPointSize);
  EXPECT_EQ(255, list.slots[0].layout.screenMargin);  // clamped
  EXPECT_EQ(0, list.slots[0].layout.wordSpacing);     // clamped
  // Values of an unsaved slot are dropped; its default name is kept.
  EXPECT_FALSE(list.slots[1].saved);
  EXPECT_EQ(0, list.slots[1].layout.fontPointSize);
  EXPECT_STREQ("Preset 2", list.slots[1].name);
  EXPECT_STREQ("Preset 10", list.slots[9].name);
}

TEST(ReadingPresets, ParseDecodesEscapedNames) {
  const std::string json = "{\"slots\":[{\"name\":\"a\\\"b\\\\c\\u03b1\\n\",\"saved\":false}]}";
  PresetList list;
  ASSERT_TRUE(parse(json.c_str(), json.size(), list));
  EXPECT_STREQ("a\"b\\c\xce\xb1\n", list.slots[0].name);
}

TEST(ReadingPresets, FindMatchesSavedSlotsOnly) {
  PresetList list;
  EXPECT_EQ(-1, list.find(Layout{}));
  save(list, 4, sampleLayout());
  EXPECT_EQ(4, list.find(sampleLayout()));
  Layout other = sampleLayout();
  other.textHanging = 0;
  EXPECT_EQ(-1, list.find(other));
  std::strcpy(other.sdFontFamilyName, "Other");
  other.textHanging = 1;
  EXPECT_EQ(-1, list.find(other));
  ASSERT_TRUE(clear(list, 4));
  EXPECT_EQ(-1, list.find(sampleLayout()));
  EXPECT_STREQ("Preset 5", list.slots[4].name);
}
