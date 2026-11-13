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

TEST(ReadingPresets, ParseNeverKeepsAHalfCutUtf8Name) {
  // 16 Greek letters are 32 bytes; the name holds 24: the cut must fall between letters.
  std::string json = "{\"slots\":[{\"name\":\"";
  for (int i = 0; i < 16; ++i) json += "\xCE\xB1";
  json += "\",\"saved\":false}]}";
  PresetList list;
  ASSERT_TRUE(parse(json.c_str(), json.size(), list));
  const size_t len = std::strlen(list.slots[0].name);
  EXPECT_EQ(0u, len % 2);
  EXPECT_LE(len, 24u);
  EXPECT_GT(len, 0u);
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

// --- Gestures inside a preset ---

namespace {

Gestures sampleGestures() {
  Gestures g;
  for (int i = 0; i < kGestureSlots; ++i) g.slots[i] = static_cast<uint8_t>(i + 2);
  for (int i = 0; i < kQuickMenuSlots; ++i) g.quickMenu[i] = static_cast<uint8_t>(13 - i);
  g.slots[8] = 255;  // the module keeps the byte; the store checks it against the actions
  return g;
}

// One saved slot with `gesturesJson` as the value of its "gestures" key, then a second saved slot.
std::string withGesturesValue(const std::string& gesturesJson) {
  return "{\"v\":1,\"slots\":[{\"name\":\"A\",\"saved\":true,\"size\":16,\"gestures\":" + gesturesJson +
         ",\"margin\":9},{\"name\":\"B\",\"saved\":true,\"size\":12}]}";
}

}  // namespace

TEST(ReadingPresetsGestures, RoundTripWithGestures) {
  PresetList list;
  ASSERT_TRUE(save(list, 0, sampleLayout(), sampleGestures()));
  ASSERT_TRUE(save(list, 1, sampleLayout()));
  const std::string json = toJson(list);
  EXPECT_NE(std::string::npos, json.find("\"gestures\":{\"slots\":[2,3,4,5,6,7,8,9,255],\"quick\":[13,12,11,10,9,8]}"));

  PresetList back;
  ASSERT_TRUE(parse(json.c_str(), json.size(), back));
  EXPECT_TRUE(back.slots[0].hasGestures);
  EXPECT_EQ(sampleGestures(), back.slots[0].gestures);
  EXPECT_EQ(sampleLayout(), back.slots[0].layout);
  EXPECT_FALSE(back.slots[1].hasGestures);
  EXPECT_EQ(Gestures{}, back.slots[1].gestures);
  EXPECT_EQ(json, toJson(back));
}

TEST(ReadingPresetsGestures, FlagOffWritesNoBlockAndTheBytesAreAsBefore) {
  PresetList list;
  ASSERT_TRUE(save(list, 2, sampleLayout()));
  const std::string json = toJson(list);
  EXPECT_EQ(std::string::npos, json.find("gestures"));
  // What the module wrote for this slot before gestures existed (0.70.0), byte for byte.
  EXPECT_NE(std::string::npos,
            json.find("{\"name\":\"Preset 3\",\"saved\":true,\"sdFont\":\"Bookerly\",\"font\":0,\"size\":14,"
                      "\"spacing\":6,\"align\":1,\"hyphen\":1,\"focus\":0,\"gaps\":2,\"word\":125,\"char\":3,"
                      "\"margin\":12,\"indent\":0,\"lig\":1,\"kern\":0,\"widow\":1,\"headings\":0,\"hanging\":1}"));
}

TEST(ReadingPresetsGestures, OldFileParsesAsLayoutOnly) {
  // A file as 0.70.0 wrote it: no gestures key anywhere.
  const std::string json =
      "{\"v\":1,\"slots\":[{\"name\":\"Day\",\"saved\":true,\"sdFont\":\"Bookerly\",\"font\":0,\"size\":14,"
      "\"spacing\":6,\"align\":1,\"hyphen\":1,\"focus\":0,\"gaps\":2,\"word\":125,\"char\":3,\"margin\":12,"
      "\"indent\":0,\"lig\":1,\"kern\":0,\"widow\":1,\"headings\":0,\"hanging\":1},"
      "{\"name\":\"Preset 2\",\"saved\":false},{\"name\":\"Preset 3\",\"saved\":false},"
      "{\"name\":\"Preset 4\",\"saved\":false},{\"name\":\"Preset 5\",\"saved\":false},"
      "{\"name\":\"Preset 6\",\"saved\":false},{\"name\":\"Preset 7\",\"saved\":false},"
      "{\"name\":\"Preset 8\",\"saved\":false},{\"name\":\"Preset 9\",\"saved\":false},"
      "{\"name\":\"Preset 10\",\"saved\":false}]}";
  PresetList list;
  ASSERT_TRUE(parse(json.c_str(), json.size(), list));
  EXPECT_TRUE(list.slots[0].saved);
  EXPECT_STREQ("Day", list.slots[0].name);
  EXPECT_EQ(sampleLayout(), list.slots[0].layout);
  for (int i = 0; i < kSlots; ++i) {
    EXPECT_FALSE(list.slots[i].hasGestures) << i;
    EXPECT_EQ(Gestures{}, list.slots[i].gestures) << i;
  }
  EXPECT_EQ(json, toJson(list));  // and it is written back unchanged
}

TEST(ReadingPresetsGestures, CorruptBlockIsIgnoredAndTheRestStays) {
  const char* const bad[] = {
      "null",
      "true",
      "7",
      "\"swipe\"",
      "[1,2,3]",
      "{}",
      "{\"slots\":[1,2,3,4,5,6,7,8,9]}",                                  // no quick
      "{\"quick\":[1,2,3,4,5,6]}",                                        // no slots
      "{\"slots\":[1,2,3,4,5,6,7,8],\"quick\":[1,2,3,4,5,6]}",            // eight slots
      "{\"slots\":[1,2,3,4,5,6,7,8,9,10],\"quick\":[1,2,3,4,5,6]}",       // ten slots
      "{\"slots\":[1,2,3,4,5,6,7,8,9],\"quick\":[1,2,3,4,5]}",            // five quick entries
      "{\"slots\":[1,2,3,4,5,6,7,8,256],\"quick\":[1,2,3,4,5,6]}",        // over a byte
      "{\"slots\":[1,2,3,4,5,6,7,8,-1],\"quick\":[1,2,3,4,5,6]}",         // negative
      "{\"slots\":[1,2,3,4,5,6,7,8,1.5],\"quick\":[1,2,3,4,5,6]}",        // not whole
      "{\"slots\":[1,2,3,4,5,6,7,8,\"9\"],\"quick\":[1,2,3,4,5,6]}",      // a string
      "{\"slots\":[1,2,3,4,5,6,7,8,null],\"quick\":[1,2,3,4,5,6]}",       // null
      "{\"slots\":{\"a\":1},\"quick\":[1,2,3,4,5,6]}",                    // an object
      "{\"slots\":[1,2,3,4,5,6,7,8,9],\"slots\":[1,2,3,4,5,6,7,8,9]}",    // twice
      "{\"slots\":[1,2,3,4,5,6,7,8,9],\"quick\":[1,2,3,4,5,6],\"x\":1}",  // something extra
      "{\"slots\":[1,2,3,4,5,6,7,8,9],\"other\":[1,2,3,4,5,6]}",          // an unknown key
  };
  for (const char* value : bad) {
    const std::string json = withGesturesValue(value);
    PresetList list;
    ASSERT_TRUE(parse(json.c_str(), json.size(), list)) << value;
    EXPECT_FALSE(list.slots[0].hasGestures) << value;
    EXPECT_EQ(Gestures{}, list.slots[0].gestures) << value;
    // The layout around the block, and the next slot, are read as usual.
    EXPECT_TRUE(list.slots[0].saved) << value;
    EXPECT_EQ(16, list.slots[0].layout.fontPointSize) << value;
    EXPECT_EQ(9, list.slots[0].layout.screenMargin) << value;
    EXPECT_TRUE(list.slots[1].saved) << value;
    EXPECT_EQ(12, list.slots[1].layout.fontPointSize) << value;
  }
  // The good shape in the same place is taken, with blanks and either key order.
  const std::string good =
      withGesturesValue(" { \"quick\" : [ 1 , 2 , 3 , 4 , 5 , 6 ] , \"slots\" : [ 9,8,7,6,5,4,3,2,1 ] } ");
  PresetList list;
  ASSERT_TRUE(parse(good.c_str(), good.size(), list));
  EXPECT_TRUE(list.slots[0].hasGestures);
  EXPECT_EQ(9, list.slots[0].gestures.slots[0]);
  EXPECT_EQ(1, list.slots[0].gestures.slots[8]);
  EXPECT_EQ(6, list.slots[0].gestures.quickMenu[5]);
  EXPECT_EQ(9, list.slots[0].layout.screenMargin);
}

TEST(ReadingPresetsGestures, BlockOfAnUnsavedSlotIsDropped) {
  const std::string json =
      "{\"slots\":[{\"name\":\"A\",\"saved\":false,\"gestures\":{\"slots\":[1,2,3,4,5,6,7,8,9],"
      "\"quick\":[1,2,3,4,5,6]}}]}";
  PresetList list;
  ASSERT_TRUE(parse(json.c_str(), json.size(), list));
  EXPECT_FALSE(list.slots[0].hasGestures);
  EXPECT_EQ(Gestures{}, list.slots[0].gestures);
}

TEST(ReadingPresetsGestures, CutOffInsideTheBlockFailsTheFileAsBefore) {
  const std::string json = "{\"slots\":[{\"name\":\"A\",\"saved\":true,\"gestures\":{\"slots\":[1,2,3";
  PresetList list;
  EXPECT_FALSE(parse(json.c_str(), json.size(), list));
  EXPECT_FALSE(list.slots[0].saved);
}

TEST(ReadingPresetsGestures, SavingLayoutOnlyOrClearingDropsTheGestures) {
  PresetList list;
  ASSERT_TRUE(save(list, 3, sampleLayout(), sampleGestures()));
  EXPECT_TRUE(list.slots[3].hasGestures);
  ASSERT_TRUE(save(list, 3, sampleLayout()));
  EXPECT_FALSE(list.slots[3].hasGestures);
  EXPECT_EQ(Gestures{}, list.slots[3].gestures);
  ASSERT_TRUE(save(list, 3, sampleLayout(), sampleGestures()));
  ASSERT_TRUE(clear(list, 3));
  EXPECT_FALSE(list.slots[3].hasGestures);
  EXPECT_EQ(Gestures{}, list.slots[3].gestures);
  EXPECT_FALSE(save(list, kSlots, sampleLayout(), sampleGestures()));
  EXPECT_FALSE(save(list, -1, sampleLayout(), sampleGestures()));
}

TEST(ReadingPresetsGestures, FindComparesGesturesOnlyForPresetsThatCarryThem) {
  PresetList list;
  ASSERT_TRUE(save(list, 1, sampleLayout(), sampleGestures()));
  ASSERT_TRUE(save(list, 4, sampleLayout()));
  EXPECT_EQ(1, list.find(sampleLayout()));  // the layout-only search is as before
  EXPECT_EQ(1, list.find(sampleLayout(), sampleGestures()));
  EXPECT_EQ(4, list.find(sampleLayout(), Gestures{}));  // other gestures: the layout-only preset matches
  Layout other = sampleLayout();
  other.fontPointSize = 20;
  EXPECT_EQ(-1, list.find(other, sampleGestures()));
}

TEST(ReadingPresetsGestures, TenFullPresetsFitTheStoreBuffer) {
  PresetList list;
  Layout l;
  std::memset(l.sdFontFamilyName, 'W', sizeof(l.sdFontFamilyName) - 1);
  for (uint8_t Layout::* m :
       {&Layout::fontFamily, &Layout::fontPointSize, &Layout::lineSpacing, &Layout::paragraphAlignment,
        &Layout::hyphenationEnabled, &Layout::focusReadingEnabled, &Layout::evenWordGaps, &Layout::wordSpacing,
        &Layout::characterSpacing, &Layout::screenMargin, &Layout::paragraphIndentSpaces, &Layout::textLigatures,
        &Layout::textKerning, &Layout::textWidowOrphan, &Layout::textKeepHeadings, &Layout::textHanging}) {
    l.*m = 255;
  }
  Gestures g;
  std::memset(g.slots, 255, sizeof(g.slots));
  std::memset(g.quickMenu, 255, sizeof(g.quickMenu));
  for (int i = 0; i < kSlots; ++i) {
    ASSERT_TRUE(save(list, i, l, g));
    // The worst case for the writer: 24 name bytes that each take six when escaped (setName would clean them).
    std::memset(list.slots[i].name, 1, kNameCapacity - 1);
  }
  const size_t need = serialize(list, nullptr, 0);
  EXPECT_LT(need, 7424u) << need;  // ReadingPresetsStore::save's CAP
}

TEST(ReadingPresetsGestures, APresetWithTheCurrentGesturesBeatsALayoutOnlyOne) {
  PresetList list;
  Gestures other = sampleGestures();
  other.slots[0] = 5;                                             // different from the sample's first slot
  ASSERT_TRUE(save(list, 0, sampleLayout()));                    // layout only, above
  ASSERT_TRUE(save(list, 1, sampleLayout(), sampleGestures()));  // layout and gestures
  EXPECT_EQ(1, list.find(sampleLayout(), sampleGestures()));     // the closer match wins
  EXPECT_EQ(0, list.find(sampleLayout(), other));                // other gestures: the layout-only slot
}
