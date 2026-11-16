#include <Epub/ParsedText.h>
#include <Epub/ReaderRenderSpec.h>
#include <Epub/TypographyEngine.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using typoengine::Config;

// The fixture font of the next layout() calls: the width of its space and the side bearings (left, right) of the
// codepoints that have metrics. Engine puts both back.
int fixtureSpace = 4;
std::map<uint32_t, std::pair<int, int>> fixtureBearings;
// How often the last layout() asked the font for a space: a measure of the line breaker's work.
size_t lastSpaceAdvanceCalls = 0;

// Sets the engine for one test and switches it off again after it.
struct Engine {
  explicit Engine(const Config& config) { typoengine::set(typoengine::pack(config)); }
  ~Engine() {
    typoengine::set(0);
    Hyphenator::setPreferredLanguage("");
    fixtureSpace = 4;
    fixtureBearings.clear();
  }
};

Config condense(const uint8_t percent) {
  Config c;
  c.condensePercent = percent;
  return c;
}

struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> xpos;
  int8_t tracking = 0;
  // Where the last word ends, measured as the reader draws it (the line's own tracking).
  int right = 0;
};

// Fixture metrics (stub renderer): every glyph is 8 px wide, a space is 4 px, tracking goes between two glyphs.
std::vector<Line> layout(const std::vector<const char*>& words, const uint16_t width, const bool hyphenation = false,
                         const CssTextAlign alignment = CssTextAlign::Justify) {
  GfxRenderer renderer;
  renderer.spaceAdvance = fixtureSpace;
  renderer.sideBearings = fixtureBearings;
  BlockStyle style;
  style.alignment = alignment;
  style.textIndentDefined = true;
  ParsedText text(hyphenation, false, style, 0);
  for (const char* word : words) text.addWord(word, EpdFontFamily::REGULAR);
  std::vector<Line> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::unique_ptr<TextBlock> block, auto) {
    auto& line = lines.emplace_back();
    line.tracking = block->getBlockStyle().characterSpacing;
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      line.words.emplace_back(block->wordText(i));
      line.xpos.push_back(block->wordXpos(i));
    }
    if (!line.words.empty()) {
      line.right = line.xpos.back() +
                   renderer.getTextAdvanceX(0, line.words.back().c_str(), EpdFontFamily::REGULAR, line.tracking);
    }
  });
  lastSpaceAdvanceCalls = renderer.spaceAdvanceCalls;
  return lines;
}

std::vector<std::vector<std::string>> wordsOf(const std::vector<Line>& lines) {
  std::vector<std::vector<std::string>> out;
  for (const auto& line : lines) out.push_back(line.words);
  return out;
}

std::vector<std::vector<int16_t>> xposOf(const std::vector<Line>& lines) {
  std::vector<std::vector<int16_t>> out;
  for (const auto& line : lines) out.push_back(line.xpos);
  return out;
}

// "aaaa bbbb cccc" is 32 + 4 + 32 + 4 + 32 = 104 px; the last word is 48 px.
const std::vector<const char*> kThreeAndOne{"aaaa", "bbbb", "cccc", "dddddd"};

}  // namespace

// --- The packed setting ---

TEST(TypographyEngineConfig, DefaultsPackToZero) {
  EXPECT_EQ(typoengine::pack(Config{}), 0u);
  const Config c = typoengine::unpack(0);
  EXPECT_EQ(c.condensePercent, 100);
  EXPECT_EQ(c.letterSpacing, 0);
  EXPECT_FALSE(c.lockedSpace);
  EXPECT_EQ(c.hyphenMode, typoengine::HYPHEN_PATTERNS);
  EXPECT_EQ(c.minLeft, 0);
  EXPECT_EQ(c.minRight, 0);
  EXPECT_EQ(c.language, 0);
  EXPECT_EQ(c.punctSpace, typoengine::PUNCT_SPACE_NORMAL);
  EXPECT_FALSE(c.opticalSpacing);
}

TEST(TypographyEngineConfig, SpaceAfterPunctuationAndOpticalSpacingHaveTheirOwnBits) {
  for (uint8_t punct = 0; punct < typoengine::PUNCT_SPACE_COUNT; ++punct) {
    for (const bool optical : {false, true}) {
      // The other settings at their largest values: nothing runs into the new bits, and the new bits into nothing.
      Config c = condense(50);
      c.letterSpacing = typoengine::LETTER_SPACING_LEVELS - 1;
      c.lockedSpace = true;
      c.hyphenMode = typoengine::HYPHEN_OFF;
      c.minLeft = c.minRight = typoengine::MIN_LETTERS_MAX;
      c.language = typoengine::LANGUAGE_COUNT;
      const uint32_t others = typoengine::pack(c);
      c.punctSpace = punct;
      c.opticalSpacing = optical;
      const uint32_t packed = typoengine::pack(c);
      // Bits 18-20 are theirs; bit 21 is the third bit of letter spacing and 28-30 the layout revision.
      const uint32_t theirs = 7U << 18;
      EXPECT_EQ(packed & ~theirs, others & ~theirs);
      EXPECT_EQ(others & theirs, 0U);
      EXPECT_EQ((packed & theirs) >> 18, static_cast<uint32_t>(punct) | (optical ? 4U : 0U));
      const Config back = typoengine::unpack(packed);
      EXPECT_EQ(back.punctSpace, punct);
      EXPECT_EQ(back.opticalSpacing, optical);
      EXPECT_EQ(back.condensePercent, 50);
      EXPECT_EQ(back.language, typoengine::LANGUAGE_COUNT);
      EXPECT_EQ(back.minRight, typoengine::MIN_LETTERS_MAX);

      // Each one alone is an engine in use.
      Config alone;
      alone.punctSpace = punct;
      alone.opticalSpacing = optical;
      EXPECT_EQ(typoengine::pack(alone) != 0, punct != 0 || optical);
    }
  }
  // A choice that does not exist is Normal.
  Config odd;
  odd.punctSpace = 3;
  EXPECT_EQ(typoengine::sanitized(odd).punctSpace, typoengine::PUNCT_SPACE_NORMAL);
  EXPECT_EQ(typoengine::unpack(3U << 18).punctSpace, typoengine::PUNCT_SPACE_NORMAL);
}

TEST(TypographyEngineConfig, EveryValueSurvivesPacking) {
  for (const int percent : {100, 90, 80, 70, 60, 50}) {
    for (uint8_t letters = 0; letters < typoengine::LETTER_SPACING_LEVELS; ++letters) {
      for (uint8_t mode = 0; mode < typoengine::HYPHEN_MODE_COUNT; ++mode) {
        for (uint8_t minimum = 0; minimum <= typoengine::MIN_LETTERS_MAX; ++minimum) {
          for (uint8_t language = 0; language <= typoengine::LANGUAGE_COUNT; ++language) {
            Config c;
            c.condensePercent = static_cast<uint8_t>(percent);
            c.letterSpacing = letters;
            c.lockedSpace = (minimum % 2) != 0;
            c.hyphenMode = mode;
            c.minLeft = minimum;
            c.minRight = static_cast<uint8_t>(typoengine::MIN_LETTERS_MAX - minimum);
            c.language = language;
            const Config back = typoengine::unpack(typoengine::pack(c));
            ASSERT_EQ(back.condensePercent, c.condensePercent);
            ASSERT_EQ(back.letterSpacing, c.letterSpacing);
            ASSERT_EQ(back.lockedSpace, c.lockedSpace);
            ASSERT_EQ(back.hyphenMode, c.hyphenMode);
            ASSERT_EQ(back.minLeft, c.minLeft);
            ASSERT_EQ(back.minRight, c.minRight);
            ASSERT_EQ(back.language, c.language);
          }
        }
      }
    }
  }
}

TEST(TypographyEngineConfig, ValuesOutsideTheChoicesAreBroughtBack) {
  Config c;
  c.condensePercent = 7;
  c.letterSpacing = 9;
  c.hyphenMode = 9;
  c.minLeft = 200;
  c.minRight = 6;
  c.language = 14;
  const Config s = typoengine::sanitized(c);
  EXPECT_EQ(s.condensePercent, 100);  // below 50 is not a choice: off, not the strongest
  EXPECT_EQ(s.letterSpacing, 0);      // not a choice: off, not the strongest
  EXPECT_EQ(s.hyphenMode, typoengine::HYPHEN_PATTERNS);
  EXPECT_EQ(s.minLeft, 5);
  EXPECT_EQ(s.minRight, 5);
  EXPECT_EQ(s.language, 0);
  EXPECT_EQ(typoengine::sanitizeCondensePercent(85), 80);
  EXPECT_EQ(typoengine::sanitizeCondensePercent(255), 100);
  // Every bit pattern unpacks to values inside the choices.
  for (uint32_t packed = 0; packed < (1u << 18); packed += 7) {
    const Config u = typoengine::unpack(packed);
    ASSERT_GE(u.condensePercent, 50);
    ASSERT_LE(u.condensePercent, 100);
    ASSERT_LT(u.letterSpacing, typoengine::LETTER_SPACING_LEVELS);
    ASSERT_LT(u.hyphenMode, typoengine::HYPHEN_MODE_COUNT);
    ASSERT_LE(u.minLeft, typoengine::MIN_LETTERS_MAX);
    ASSERT_LE(u.minRight, typoengine::MIN_LETTERS_MAX);
    ASSERT_LE(u.language, typoengine::LANGUAGE_COUNT);
  }
}

TEST(TypographyEngineConfig, LetterSpacingHasFiveChoicesAndTheOldOnesKeepTheirBits) {
  EXPECT_EQ(typoengine::LETTER_SPACING_LEVELS, 5);
  const uint32_t revision = typoengine::LAYOUT_REVISION << 28;
  for (uint8_t level = 0; level < typoengine::LETTER_SPACING_LEVELS; ++level) {
    Config c;
    c.letterSpacing = level;
    const uint32_t packed = typoengine::pack(c);
    EXPECT_EQ(typoengine::unpack(packed).letterSpacing, level);
    // Off, Small and Medium sit in bits 3-4 as in 0.76.0; Large and Max use bit 21 with them.
    const uint32_t expected = (static_cast<uint32_t>(level & 3) << 3) | (static_cast<uint32_t>(level >> 2) << 21);
    EXPECT_EQ(packed, level == 0 ? 0U : expected | revision);
  }
  // A word written by 0.76.0 or 0.77.0 (no revision bits) still reads as the same choices.
  EXPECT_EQ(typoengine::unpack(1U << 3).letterSpacing, 1);
  EXPECT_EQ(typoengine::unpack(2U << 3).letterSpacing, 2);
  // and is not the word the same settings pack to now, so a section cached by the older layout is built again.
  EXPECT_NE(typoengine::pack(typoengine::unpack(2U << 3)), 2U << 3);
  EXPECT_EQ(typoengine::pack(Config{}), 0U);
}

TEST(TypographyEngineConfig, ACondensePercentageOutsideFiftyToAHundredIsOff) {
  for (int percent = 0; percent <= 255; ++percent) {
    const uint8_t snapped = typoengine::sanitizeCondensePercent(static_cast<uint8_t>(percent));
    if (percent < 50 || percent > 100) {
      EXPECT_EQ(snapped, 100) << percent;
    } else {
      EXPECT_EQ(snapped, percent - percent % 10) << percent;
    }
  }
  // 0, as in a settings file or a preset edited by hand, packs like the engine off.
  Config c;
  c.condensePercent = 0;
  EXPECT_EQ(typoengine::sanitized(c).condensePercent, 100);
  EXPECT_EQ(typoengine::pack(c), 0u);
  c.condensePercent = 49;
  EXPECT_EQ(typoengine::pack(c), 0u);
  c.condensePercent = 50;
  EXPECT_EQ(typoengine::unpack(typoengine::pack(c)).condensePercent, 50);
}

TEST(TypographyEngineConfig, HyphenationIsOnOnlyWithTheSwitchOnAndTheModeNotOff) {
  EXPECT_TRUE(typoengine::hyphenationOn(1, typoengine::HYPHEN_PATTERNS));
  EXPECT_TRUE(typoengine::hyphenationOn(1, typoengine::HYPHEN_SOFT_ONLY));
  EXPECT_FALSE(typoengine::hyphenationOn(1, typoengine::HYPHEN_OFF));
  EXPECT_FALSE(typoengine::hyphenationOn(0, typoengine::HYPHEN_PATTERNS));
  EXPECT_FALSE(typoengine::hyphenationOn(0, typoengine::HYPHEN_OFF));
  // A mode outside the choices is Patterns, as sanitized() makes it.
  EXPECT_TRUE(typoengine::hyphenationOn(1, 9));
}

TEST(TypographyEngineConfig, TheHyphenationCheckboxAlwaysChangesWhatItShows) {
  for (uint8_t sw : {uint8_t{0}, uint8_t{1}}) {
    for (uint8_t mode = 0; mode < typoengine::HYPHEN_MODE_COUNT; ++mode) {
      uint8_t s = sw, m = mode;
      const bool before = typoengine::hyphenationOn(s, m);
      typoengine::toggleHyphenation(s, m);
      EXPECT_NE(before, typoengine::hyphenationOn(s, m)) << int(sw) << " " << int(mode);
    }
  }
  // Switching off keeps the mode; switching on from mode Off goes back to Patterns; Soft hyphens only is kept.
  uint8_t s = 1, m = typoengine::HYPHEN_SOFT_ONLY;
  typoengine::toggleHyphenation(s, m);
  EXPECT_EQ(s, 0);
  EXPECT_EQ(m, typoengine::HYPHEN_SOFT_ONLY);
  typoengine::toggleHyphenation(s, m);
  EXPECT_EQ(s, 1);
  EXPECT_EQ(m, typoengine::HYPHEN_SOFT_ONLY);
  s = 1;
  m = typoengine::HYPHEN_OFF;
  typoengine::toggleHyphenation(s, m);
  EXPECT_EQ(s, 1);
  EXPECT_EQ(m, typoengine::HYPHEN_PATTERNS);
}

TEST(TypographyEngineConfig, SettingHyphenationOnOrOffAlwaysShowsAsAsked) {
  // What the Settings and web switch does: after it, hyphenationOn() is what was asked, from every state.
  for (uint8_t sw = 0; sw <= 1; ++sw) {
    for (uint8_t mode = 0; mode < typoengine::HYPHEN_MODE_COUNT; ++mode) {
      for (const bool on : {false, true}) {
        uint8_t s = sw, m = mode;
        typoengine::setHyphenation(s, m, on);
        EXPECT_EQ(on, typoengine::hyphenationOn(s, m)) << int(sw) << " " << int(mode) << " " << on;
        EXPECT_EQ(s, on ? 1 : 0);
        // The mode only changes when on is asked while it is Off: then it is Patterns.
        const uint8_t expectedMode = on && mode == typoengine::HYPHEN_OFF ? typoengine::HYPHEN_PATTERNS : mode;
        EXPECT_EQ(m, expectedMode) << int(sw) << " " << int(mode) << " " << on;
      }
    }
  }
  // Setting the state it already shows changes nothing.
  uint8_t s = 0, m = typoengine::HYPHEN_OFF;
  typoengine::setHyphenation(s, m, false);
  EXPECT_EQ(s, 0);
  EXPECT_EQ(m, typoengine::HYPHEN_OFF);
  s = 1;
  m = typoengine::HYPHEN_SOFT_ONLY;
  typoengine::setHyphenation(s, m, true);
  EXPECT_EQ(s, 1);
  EXPECT_EQ(m, typoengine::HYPHEN_SOFT_ONLY);
  // A switch value that is not 0 or 1 (a hand-edited file) still ends as 0 or 1.
  s = 7;
  m = typoengine::HYPHEN_OFF;
  typoengine::setHyphenation(s, m, true);
  EXPECT_EQ(s, 1);
  EXPECT_EQ(m, typoengine::HYPHEN_PATTERNS);
}

TEST(TypographyEngineConfig, EveryPatternSetHasItsOwnName) {
  EXPECT_STREQ(typoengine::languageName(0), "");
  EXPECT_STREQ(typoengine::languageName(14), "");
  EXPECT_STREQ(typoengine::languageName(-1), "");
  const std::vector<std::pair<const char*, const char*>> expected = {{"en", "English"},
                                                                     {"fr", "Français"},
                                                                     {"de", "Deutsch"},
                                                                     {"ru", "Русский"},
                                                                     {"es", "Español"},
                                                                     {"it", "Italiano"},
                                                                     {"pl", "Polski"},
                                                                     {"sv", "Svenska"},
                                                                     {"uk", "Українська"},
                                                                     {"fi", "Suomi"},
                                                                     {"pt", "Português"},
                                                                     {"el", "Ελληνικά"},
                                                                     {"grc", "Ελληνικά (πολυτονικά)"}};
  ASSERT_EQ(expected.size(), static_cast<size_t>(typoengine::LANGUAGE_COUNT));
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_STREQ(typoengine::LANGUAGE_TAGS[i], expected[i].first);
    EXPECT_STREQ(typoengine::languageName(static_cast<int>(i) + 1), expected[i].second);
    for (size_t k = 0; k < i; ++k) EXPECT_STRNE(typoengine::LANGUAGE_NAMES[i], typoengine::LANGUAGE_NAMES[k]);
  }
}

TEST(TypographyEngineConfig, ScopedOffHidesTheBooksEngineAndGivesItBack) {
  Config c;
  c.condensePercent = 70;
  c.language = 12;
  c.hyphenMode = typoengine::HYPHEN_SOFT_ONLY;
  const uint32_t book = typoengine::pack(c);
  typoengine::set(book);
  {
    const typoengine::ScopedOff off;
    EXPECT_EQ(typoengine::storage().load(), 0u);
    EXPECT_EQ(typoengine::current().condensePercent, 100);
    EXPECT_EQ(typoengine::current().language, 0);
    {
      const typoengine::ScopedOff inner;  // a second one inside changes nothing
      EXPECT_EQ(typoengine::storage().load(), 0u);
    }
    EXPECT_EQ(typoengine::storage().load(), 0u);
  }
  EXPECT_EQ(typoengine::storage().load(), book);
  typoengine::set(0);
}

TEST(TypographyEngineConfig, OnlyAnEngineInUseChangesTheLayoutSignature) {
  ReaderRenderSpec base;
  ReaderRenderSpec same = base;
  same.typographyEngine = 0;
  EXPECT_EQ(readerRenderSpecSignature(base), readerRenderSpecSignature(same));
  ReaderRenderSpec changed = base;
  changed.typographyEngine = typoengine::pack(condense(80));
  EXPECT_NE(readerRenderSpecSignature(base), readerRenderSpecSignature(changed));
}

// --- Space condensing: the arithmetic ---

TEST(SpaceCondensing, AGapShrinksToItsShareRoundedUp) {
  EXPECT_EQ(typoengine::condensedGap(10, 90), 9);
  EXPECT_EQ(typoengine::condensedGap(10, 50), 5);
  EXPECT_EQ(typoengine::condensedGap(4, 50), 2);
  EXPECT_EQ(typoengine::condensedGap(4, 70), 3);
  EXPECT_EQ(typoengine::condensedGap(4, 90), 4);  // 3.6 px rounds up: nothing to give
  EXPECT_EQ(typoengine::condensedGap(5, 50), 3);
  EXPECT_EQ(typoengine::gapShrink(10, 60), 4);
  EXPECT_EQ(typoengine::gapShrink(4, 90), 0);
}

TEST(SpaceCondensing, OffAndEmptyGapsGiveNothing) {
  EXPECT_EQ(typoengine::condensedGap(10, 100), 10);
  EXPECT_EQ(typoengine::gapShrink(10, 100), 0);
  EXPECT_EQ(typoengine::condensedGap(0, 50), 0);
  EXPECT_EQ(typoengine::gapShrink(0, 50), 0);
  EXPECT_EQ(typoengine::condensedGap(-2, 50), -2);  // a negative kerning gap is left as it is
  EXPECT_EQ(typoengine::condensedGap(1, 50), 1);    // a gap never closes
  for (int gap = 0; gap <= 40; ++gap) {
    for (const int percent : {50, 60, 70, 80, 90, 100}) {
      ASSERT_GE(typoengine::gapShrink(gap, percent), 0);
      ASSERT_GE(typoengine::condensedGap(gap, percent) * 100, gap * percent);
    }
  }
}

TEST(SpaceCondensing, TheOverrunIsTakenOffTheGapsRoundedUp) {
  EXPECT_EQ(typoengine::shrinkPerGap(4, 2), 2);
  EXPECT_EQ(typoengine::shrinkPerGap(5, 2), 3);
  EXPECT_EQ(typoengine::shrinkPerGap(1, 7), 1);
  EXPECT_EQ(typoengine::shrinkPerGap(0, 3), 0);
  EXPECT_EQ(typoengine::shrinkPerGap(-4, 3), 0);
  EXPECT_EQ(typoengine::shrinkPerGap(4, 0), 0);
  for (int deficit = 1; deficit <= 60; ++deficit) {
    for (size_t gaps = 1; gaps <= 12; ++gaps) {
      const int each = typoengine::shrinkPerGap(deficit, gaps);
      ASSERT_GE(each * static_cast<int>(gaps), deficit);        // the line comes back inside the page
      ASSERT_LT((each - 1) * static_cast<int>(gaps), deficit);  // and by no more than it has to
    }
  }
}

// --- Space condensing: the line breaker and the positions ---

TEST(SpaceCondensing, OffKeepsTheLayout) {
  for (const bool hyphenation : {false, true}) {
    const auto lines = layout(kThreeAndOne, 100, hyphenation);
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ(lines[0].words, (std::vector<std::string>{"aaaa", "bbbb"}));
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 68}));
  }
}

TEST(SpaceCondensing, OneMoreWordFitsWhenTheGapsMayShrink) {
  for (const bool hyphenation : {false, true}) {
    SCOPED_TRACE(hyphenation);
    Engine engine(condense(50));
    const auto lines = layout(kThreeAndOne, 102, hyphenation);
    ASSERT_EQ(lines.size(), 2u);
    // 104 px on a 102 px line: each of the two gaps gives 1 px (4 -> 3, the floor of 60% of a space, and not the 2
    // that 50% alone would allow), and the line ends at the edge.
    EXPECT_EQ(lines[0].words, (std::vector<std::string>{"aaaa", "bbbb", "cccc"}));
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 35, 70}));
    EXPECT_EQ(lines[0].right, 102);
    EXPECT_EQ(lines[1].xpos, (std::vector<int16_t>{0}));
    // One pixel narrower and the third word no longer fits: no gap goes under the floor.
    EXPECT_EQ(layout(kThreeAndOne, 101, hyphenation)[0].words, (std::vector<std::string>{"aaaa", "bbbb"}));
  }
}

TEST(SpaceCondensing, AnOddOverrunRoundsToTheInsideOfThePage) {
  for (const bool hyphenation : {false, true}) {
    Engine engine(condense(50));
    const auto lines = layout(kThreeAndOne, 103, hyphenation);
    ASSERT_EQ(lines.size(), 2u);
    // 1 px over, two gaps: 1 px off each, so the line ends 1 px before the edge, not after it.
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 35, 70}));
    EXPECT_EQ(lines[0].right, 102);
  }
}

TEST(SpaceCondensing, TheGapsGiveOnlyTheChosenShare) {
  for (const bool hyphenation : {false, true}) {
    SCOPED_TRACE(hyphenation);
    {
      // 70 %: a 4 px gap may go to 3. Two gaps give 2 px: a 102 px line takes the third word, a 101 px line does not.
      Engine engine(condense(70));
      const auto fits = layout(kThreeAndOne, 102, hyphenation);
      EXPECT_EQ(fits[0].words, (std::vector<std::string>{"aaaa", "bbbb", "cccc"}));
      EXPECT_EQ(fits[0].xpos, (std::vector<int16_t>{0, 35, 70}));
      EXPECT_EQ(fits[0].right, 102);
      const auto tooNarrow = layout(kThreeAndOne, 101, hyphenation);
      EXPECT_EQ(tooNarrow[0].words, (std::vector<std::string>{"aaaa", "bbbb"}));
    }
    {
      // 90 % of a 4 px gap is 3.6 px, which rounds up to 4: nothing to give.
      Engine engine(condense(90));
      const auto lines = layout(kThreeAndOne, 103, hyphenation);
      EXPECT_EQ(lines[0].words, (std::vector<std::string>{"aaaa", "bbbb"}));
    }
  }
}

TEST(SpaceCondensing, TheLastLineOfAParagraphIsNotCondensed) {
  for (const bool hyphenation : {false, true}) {
    SCOPED_TRACE(hyphenation);
    Engine engine(condense(50));
    // The third word is the paragraph's last: its line is not justified, so it must fit as it is.
    const auto lines = layout({"aaaa", "bbbb", "cccc"}, 100, hyphenation);
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].words, (std::vector<std::string>{"aaaa", "bbbb"}));
    EXPECT_EQ(lines[1].words, (std::vector<std::string>{"cccc"}));
    EXPECT_EQ(lines[1].xpos, (std::vector<int16_t>{0}));
  }
}

TEST(SpaceCondensing, ALineOfOneWordHasNoGapToShrink) {
  Engine engine(condense(50));
  // 104 px word on a 100 px line: no gap, so nothing changes (the word is split by the usual fallback).
  const std::vector<const char*> words{"aaaaaaaaaaaaa", "bb"};
  std::vector<Line> off;
  {
    typoengine::set(0);
    off = layout(words, 100);
    typoengine::set(typoengine::pack(condense(50)));
  }
  const auto on = layout(words, 100);
  EXPECT_EQ(wordsOf(on), wordsOf(off));
  EXPECT_EQ(xposOf(on), xposOf(off));
}

TEST(SpaceCondensing, OnlyJustifiedTextIsCondensed) {
  Engine engine(condense(50));
  for (const CssTextAlign alignment : {CssTextAlign::Left, CssTextAlign::Center, CssTextAlign::Right}) {
    const auto lines = layout(kThreeAndOne, 100, false, alignment);
    ASSERT_GE(lines.size(), 2u);
    EXPECT_EQ(lines[0].words, (std::vector<std::string>{"aaaa", "bbbb"}));
  }
}

TEST(SpaceCondensing, CjkAndRtlParagraphsAreUntouched) {
  const std::vector<std::vector<const char*>> paragraphs{
      {"漢字漢字漢字", "abc", "def", "漢字漢字", "ghij", "漢字"},        // CJK: zero-width break gaps
      {"שלום", "עולם", "שלום", "עולם", "שלום", "עולם", "שלום"},          // Hebrew: right to left
      {"aaaa", "שלום", "bbbb", "cccc", "dddd", "עולם", "eeee", "ffff"},  // mixed direction
  };
  for (const auto& words : paragraphs) {
    for (const uint16_t width : {uint16_t{60}, uint16_t{100}, uint16_t{101}, uint16_t{140}}) {
      for (const bool hyphenation : {false, true}) {
        typoengine::set(0);
        const auto off = layout(words, width, hyphenation);
        Config all = condense(50);
        all.letterSpacing = 2;
        all.lockedSpace = true;
        all.punctSpace = typoengine::PUNCT_SPACE_NONE;
        all.opticalSpacing = true;
        Engine engine(all);
        const auto on = layout(words, width, hyphenation);
        ASSERT_EQ(wordsOf(on), wordsOf(off));
        ASSERT_EQ(xposOf(on), xposOf(off));
        for (const auto& line : on) ASSERT_EQ(line.tracking, 0);
      }
    }
  }
}

// --- Letter spacing on loose lines: the arithmetic ---

TEST(LooseLineSplit, ALineWithinTheLimitKeepsItsLetters) {
  // 6 px over three gaps is 2 px each: not over a limit of 2.
  const auto split = typoengine::splitLooseLine(6, 3, 20, 2, 2);
  EXPECT_EQ(split.perLetter, 0);
  EXPECT_EQ(split.perGap, 2);
}

TEST(LooseLineSplit, TheLettersTakeTheirCapAndTheGapsTheRest) {
  // 32 px, one gap, six letter slots, limit 2: 30 px is over the limit, 5 px a letter, capped.
  auto split = typoengine::splitLooseLine(32, 1, 6, 2, 1);
  EXPECT_EQ(split.perLetter, 1);
  EXPECT_EQ(split.perGap, 26);
  split = typoengine::splitLooseLine(32, 1, 6, 2, 2);
  EXPECT_EQ(split.perLetter, 2);
  EXPECT_EQ(split.perGap, 20);
  // Many letters, little over the limit: less than a pixel each, so the letters stay.
  split = typoengine::splitLooseLine(10, 2, 40, 2, 2);
  EXPECT_EQ(split.perLetter, 0);
  EXPECT_EQ(split.perGap, 5);
}

TEST(LooseLineSplit, NothingToShareOrNowhereToPutIt) {
  EXPECT_EQ(typoengine::splitLooseLine(0, 3, 10, 2, 2).perLetter, 0);
  EXPECT_EQ(typoengine::splitLooseLine(0, 3, 10, 2, 2).perGap, 0);
  EXPECT_EQ(typoengine::splitLooseLine(-5, 3, 10, 2, 2).perGap, 0);
  EXPECT_EQ(typoengine::splitLooseLine(50, 0, 10, 2, 2).perLetter, 0);  // no gap: the line is not justified
  EXPECT_EQ(typoengine::splitLooseLine(50, 0, 10, 2, 2).perGap, 0);
  EXPECT_EQ(typoengine::splitLooseLine(50, 2, 0, 2, 2).perLetter, 0);  // one-letter words only
  EXPECT_EQ(typoengine::splitLooseLine(50, 2, 0, 2, 2).perGap, 25);
  EXPECT_EQ(typoengine::splitLooseLine(50, 2, 10, 2, 0).perLetter, 0);  // off
  EXPECT_EQ(typoengine::splitLooseLine(50, 2, 10, 2, 0).perGap, 25);
}

TEST(LooseLineSplit, TheSharesNeverAddUpToMoreThanTheSpareSpace) {
  for (int spare = 0; spare <= 120; ++spare) {
    for (size_t gaps = 0; gaps <= 9; ++gaps) {
      for (const size_t slots : {size_t{0}, size_t{1}, size_t{5}, size_t{23}, size_t{60}}) {
        for (int limit = 0; limit <= 6; ++limit) {
          for (int cap = 0; cap <= 4; ++cap) {
            const auto split = typoengine::splitLooseLine(spare, gaps, slots, limit, cap);
            ASSERT_GE(split.perLetter, 0);
            ASSERT_LE(split.perLetter, cap);
            ASSERT_GE(split.perGap, 0);
            ASSERT_LE(split.perLetter * static_cast<int>(slots) + split.perGap * static_cast<int>(gaps), spare);
            if (gaps > 0 && spare / static_cast<int>(gaps) <= limit) {
              ASSERT_EQ(split.perLetter, 0);
            }
          }
        }
      }
    }
  }
}

TEST(LooseLineSplit, TheLettersComeFirstSoTheGapsStayNearNormal) {
  // The line of the report: three gaps that would each grow by 28 px (40 px gaps of a 12 px space), about 40 places
  // between letters. Up to 0.77.0 only the part over half a space a gap went to the letters ((84 - 18) / 40 = 1 px),
  // so Medium did what Small does and the gaps were still 12 + 14 px. Now Medium's 2 px fit and a gap grows by 1 px.
  auto split = typoengine::splitLooseLine(84, 3, 40, 6, 2);
  EXPECT_EQ(split.perLetter, 2);
  EXPECT_EQ(split.perGap, 1);
  split = typoengine::splitLooseLine(84, 3, 40, 6, 1);
  EXPECT_EQ(split.perLetter, 1);
  EXPECT_EQ(split.perGap, 14);
  // A looser line with fewer letters: every step up to Max takes more from the gaps.
  int previousGap = 60;
  for (int cap = 1; cap <= 4; ++cap) {
    split = typoengine::splitLooseLine(120, 2, 28, 6, cap);
    EXPECT_EQ(split.perLetter, cap);
    EXPECT_LT(split.perGap, previousGap);
    previousGap = split.perGap;
  }
  EXPECT_EQ(previousGap, 4);
}

TEST(LooseLineSplit, TheCapIsTheChoiceBoundedByTheFontsSpace) {
  // A 12 px space (Fira Sans 16 pt on the X4 Pro): 1, 2, 3 and 4 px.
  for (uint8_t level = 0; level < typoengine::LETTER_SPACING_LEVELS; ++level) {
    EXPECT_EQ(typoengine::letterSpacingCap(level, 12), level);
  }
  EXPECT_EQ(typoengine::letterSpacingCap(4, 9), 3);  // a third of the space
  EXPECT_EQ(typoengine::letterSpacingCap(3, 6), 2);
  EXPECT_EQ(typoengine::letterSpacingCap(4, 4), 2);  // Medium's 2 px in any font, as before
  EXPECT_EQ(typoengine::letterSpacingCap(2, 0), 2);
  for (int space = 0; space <= 60; ++space) {
    for (uint8_t level = 0; level < typoengine::LETTER_SPACING_LEVELS; ++level) {
      const int cap = typoengine::letterSpacingCap(level, space);
      ASSERT_LE(cap, level);
      ASSERT_GE(cap, level < 2 ? level : 2);
      // The room between two letters stays under the narrowest word gap.
      if (space >= 6) ASSERT_LT(cap, typoengine::gapFloor(space));
    }
  }
}

// --- Letter spacing on loose lines: the positions ---

TEST(LooseLineLetterSpacing, LargeAndMaxSpreadTheLettersFurtherAndKeepTheGapNearNormal) {
  // A 12 px space. "aaaa bbbb" is 32 + 12 + 32 = 76 px on a 110 px line: the one gap would grow by 34 px, to 46.
  const std::vector<const char*> words{"aaaa", "bbbb", "xxxxxxxxxxxxx"};
  int previousGap = 46;
  for (uint8_t level = 1; level < typoengine::LETTER_SPACING_LEVELS; ++level) {
    SCOPED_TRACE(int(level));
    Config c;
    c.letterSpacing = level;
    Engine engine(c);
    fixtureSpace = 12;
    const auto lines = layout(words, 110);
    ASSERT_GE(lines.size(), 2u);
    ASSERT_EQ(lines[0].words, (std::vector<std::string>{"aaaa", "bbbb"}));
    EXPECT_EQ(lines[0].tracking, level);
    EXPECT_EQ(lines[0].right, 110);
    const int wordWidth = 32 + 3 * level;
    const int gap = lines[0].xpos[1] - wordWidth;
    EXPECT_EQ(gap, 110 - 2 * wordWidth);
    EXPECT_LT(gap, previousGap);  // each step leaves the gap nearer to the 12 px space
    previousGap = gap;
  }
  EXPECT_EQ(previousGap, 22);
  {
    // The 4 px space of the usual fixture font: Max stops at 2 px a letter.
    Config c;
    c.letterSpacing = 4;
    Engine engine(c);
    const auto lines = layout({"aaaa", "bbbb", "xxxxxxxxxxxx"}, 100);
    EXPECT_EQ(lines[0].tracking, 2);
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 62}));
  }
}

TEST(LooseLineLetterSpacing, ALooseLineSpreadsItsLetters) {
  // "aaaa bbbb" is 68 px on a 100 px line: the one gap would grow by 32 px.
  const std::vector<const char*> words{"aaaa", "bbbb", "xxxxxxxxxxxx"};
  {
    const auto off = layout(words, 100);
    EXPECT_EQ(off[0].xpos, (std::vector<int16_t>{0, 68}));
    EXPECT_EQ(off[0].tracking, 0);
  }
  {
    Config small;
    small.letterSpacing = 1;
    Engine engine(small);
    const auto lines = layout(words, 100);
    ASSERT_EQ(lines.size(), 2u);
    // 1 px between letters: each word is 35 px, and the second still ends at the edge.
    EXPECT_EQ(lines[0].tracking, 1);
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 65}));
    EXPECT_EQ(lines[0].right, 100);
    // The last line of the paragraph keeps its letters.
    EXPECT_EQ(lines[1].tracking, 0);
  }
  {
    Config medium;
    medium.letterSpacing = 2;
    Engine engine(medium);
    const auto lines = layout(words, 100);
    EXPECT_EQ(lines[0].tracking, 2);
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 62}));
    EXPECT_EQ(lines[0].right, 100);
  }
}

TEST(LooseLineLetterSpacing, ATightLineAndOtherAlignmentsKeepTheirLetters) {
  Config medium;
  medium.letterSpacing = 2;
  Engine engine(medium);
  // 104 px of text on a 108 px line: the two gaps grow by 2 px each, which is within half a space.
  const auto tight = layout(kThreeAndOne, 108);
  EXPECT_EQ(tight[0].words, (std::vector<std::string>{"aaaa", "bbbb", "cccc"}));
  EXPECT_EQ(tight[0].tracking, 0);
  EXPECT_EQ(tight[0].xpos, (std::vector<int16_t>{0, 38, 76}));
  for (const CssTextAlign alignment : {CssTextAlign::Left, CssTextAlign::Center, CssTextAlign::Right}) {
    for (const auto& line : layout({"aaaa", "bbbb", "xxxxxxxxxxxx"}, 100, false, alignment)) {
      EXPECT_EQ(line.tracking, 0);
    }
  }
}

TEST(LooseLineLetterSpacing, EvenWordGapsStillLeavesAVeryLooseLineRagged) {
  Config medium;
  medium.letterSpacing = 2;
  Engine engine(medium);
  ParsedText::evenWordGaps = 1;  // a gap may grow by 2 px at most
  const auto lines = layout({"aaaa", "bbbb", "xxxxxxxxxxxx"}, 100);
  ParsedText::evenWordGaps = 0;
  // Even with 2 px between letters the gap would grow by 20 px: the line stays ragged, with its letters as they were.
  EXPECT_EQ(lines[0].tracking, 0);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 36}));
}

// --- Locked space ---

TEST(LockedSpace, WordsOfOpeningQuotesAndDashes) {
  for (const char* word : {"—", "–", "―", "-", "«", "“", "„", "‘", "‹", "«—", "——"}) {
    EXPECT_TRUE(typoengine::isLockedSpaceWord(word)) << word;
  }
  for (const char* word : {"", "word", "word—", "—word", "»", "”", "\"", "'", ".", " ", "\xE2\x80", "\xFF"}) {
    EXPECT_FALSE(typoengine::isLockedSpaceWord(word)) << word;
  }
}

TEST(LockedSpace, TheGapAfterADashIsNotStretched) {
  // "— Hello world" is 8 + 4 + 40 + 4 + 40 = 96 px on a 100 px line.
  const std::vector<const char*> words{"—", "Hello", "world", "xxxxxxxxxxxx"};
  {
    const auto off = layout(words, 100);
    EXPECT_EQ(off[0].xpos, (std::vector<int16_t>{0, 14, 60}));  // both gaps grow by 2 px
  }
  Config locked;
  locked.lockedSpace = true;
  Engine engine(locked);
  const auto lines = layout(words, 100);
  EXPECT_EQ(lines[0].words, (std::vector<std::string>{"—", "Hello", "world"}));
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 12, 60}));  // the first stays 4 px, the second takes all 4
  EXPECT_EQ(lines[0].right, 100);
}

TEST(LockedSpace, TheGapAfterAnOpeningQuoteIsNotStretched) {
  // "« Γεια σου" is 8 + 4 + 32 + 4 + 24 = 72 px.
  const std::vector<const char*> words{"«", "Γεια", "σου", "xxxxxxxxxxxx"};
  Config locked;
  locked.lockedSpace = true;
  Engine engine(locked);
  const auto lines = layout(words, 100);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 12, 76}));
  EXPECT_EQ(lines[0].right, 100);
}

TEST(LockedSpace, ALineWhoseOnlyGapIsLockedStaysAsItIs) {
  Config locked;
  locked.lockedSpace = true;
  Engine engine(locked);
  // "— Hello" has one gap and it is locked: nothing to stretch, the line stays at its natural width.
  const auto lines = layout({"—", "Hello", "xxxxxxxxxxxx"}, 100);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 12}));
}

TEST(LockedSpace, ALockedGapIsNotCondensedEither) {
  // "— aaaa bbbb cccc" is 8 + 4 + 32 + 4 + 32 + 4 + 32 = 116 px.
  const std::vector<const char*> words{"—", "aaaa", "bbbb", "cccc", "dddddd"};
  for (const bool hyphenation : {false, true}) {
    SCOPED_TRACE(hyphenation);
    Config c = condense(50);
    {
      // Unlocked: three gaps give 3 px (4 -> 3 each), so the line fits 113 px.
      Engine engine(c);
      const auto lines = layout(words, 113, hyphenation);
      EXPECT_EQ(lines[0].words, (std::vector<std::string>{"—", "aaaa", "bbbb", "cccc"}));
      EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 11, 46, 81}));
      EXPECT_EQ(lines[0].right, 113);
    }
    c.lockedSpace = true;
    Engine engine(c);
    // Locked: only two gaps give, 2 px. 113 px is too narrow for the fourth word, 114 px is enough.
    EXPECT_EQ(layout(words, 113, hyphenation)[0].words, (std::vector<std::string>{"—", "aaaa", "bbbb"}));
    const auto lines = layout(words, 114, hyphenation);
    EXPECT_EQ(lines[0].words, (std::vector<std::string>{"—", "aaaa", "bbbb", "cccc"}));
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 12, 47, 82}));
    EXPECT_EQ(lines[0].right, 114);
  }
}

// --- Space after punctuation ---

namespace {

Config punctuation(const uint8_t mode) {
  Config c;
  c.punctSpace = mode;
  return c;
}

}  // namespace

TEST(SpaceAfterPunctuation, TheMarks) {
  for (const uint32_t cp : {uint32_t{','}, uint32_t{'.'}, uint32_t{';'}, uint32_t{':'}, uint32_t{'!'}, uint32_t{'?'},
                            uint32_t{')'}, uint32_t{']'}, uint32_t{'}'}, uint32_t{0x00B7}, uint32_t{0x0387},
                            uint32_t{0x037E}, uint32_t{0x00BB}, uint32_t{0x201D}, uint32_t{0x203A}}) {
    EXPECT_TRUE(typoengine::isSpacedPunctuation(cp)) << cp;
  }
  for (const uint32_t cp : {uint32_t{'a'}, uint32_t{0x03B1}, uint32_t{'('}, uint32_t{0x00AB}, uint32_t{0x2014},
                            uint32_t{'-'}, uint32_t{' '}, uint32_t{0}, uint32_t{'\''}, uint32_t{0x2019}}) {
    EXPECT_FALSE(typoengine::isSpacedPunctuation(cp)) << cp;
  }
}

TEST(SpaceAfterPunctuation, MinimalIsHalfASpaceAndNoneIsNoSpace) {
  using namespace typoengine;
  EXPECT_EQ(punctuationGap(12, PUNCT_SPACE_NORMAL), 12);
  EXPECT_EQ(punctuationGap(12, PUNCT_SPACE_MINIMAL), 6);
  EXPECT_EQ(punctuationGap(5, PUNCT_SPACE_MINIMAL), 3);  // rounded up
  EXPECT_EQ(punctuationGap(1, PUNCT_SPACE_MINIMAL), 1);  // a gap that exists stays a pixel wide
  EXPECT_EQ(punctuationGap(12, PUNCT_SPACE_NONE), 0);
  EXPECT_EQ(punctuationGap(0, PUNCT_SPACE_MINIMAL), 0);
}

TEST(SpaceAfterPunctuation, OnALeftAlignedLine) {
  // "aa, bb cc": 24 + 4 + 16 + 4 + 16 px.
  const std::vector<const char*> words{"aa,", "bb", "cc"};
  EXPECT_EQ(layout(words, 200, false, CssTextAlign::Left)[0].xpos, (std::vector<int16_t>{0, 28, 48}));
  for (const bool hyphenation : {false, true}) {
    {
      Engine engine(punctuation(typoengine::PUNCT_SPACE_MINIMAL));
      EXPECT_EQ(layout(words, 200, hyphenation, CssTextAlign::Left)[0].xpos, (std::vector<int16_t>{0, 26, 46}));
    }
    Engine engine(punctuation(typoengine::PUNCT_SPACE_NONE));
    EXPECT_EQ(layout(words, 200, hyphenation, CssTextAlign::Left)[0].xpos, (std::vector<int16_t>{0, 24, 44}));
  }
}

TEST(SpaceAfterPunctuation, EveryListedMarkNarrowsItsSpace) {
  for (const char* first :
       {"a,", "a.", "a;", "a:", "a!", "a?", "a)", "a]", "a·", "a\xCE\x87", "a\xCD\xBE", "a»", "a”"}) {
    Engine engine(punctuation(typoengine::PUNCT_SPACE_NONE));
    EXPECT_EQ(layout({first, "bb"}, 200, false, CssTextAlign::Left)[0].xpos, (std::vector<int16_t>{0, 16})) << first;
  }
  // Not after a letter, an opening quote, an opening bracket or a dash.
  for (const char* first : {"ab", "a«", "a(", "a—"}) {
    Engine engine(punctuation(typoengine::PUNCT_SPACE_NONE));
    EXPECT_EQ(layout({first, "bb"}, 200, false, CssTextAlign::Left)[0].xpos, (std::vector<int16_t>{0, 20})) << first;
  }
}

TEST(SpaceAfterPunctuation, JustificationDoesNotStretchTheGap) {
  // "aa, bb cc" is 64 px on a 100 px line: off, the 36 spare pixels go to both gaps.
  const std::vector<const char*> words{"aa,", "bb", "cc", "xxxxxxxxxxxx"};
  EXPECT_EQ(layout(words, 100)[0].xpos, (std::vector<int16_t>{0, 46, 84}));
  for (const bool hyphenation : {false, true}) {
    SCOPED_TRACE(hyphenation);
    {
      Engine engine(punctuation(typoengine::PUNCT_SPACE_MINIMAL));
      const auto lines = layout(words, 100, hyphenation);
      // The gap after the comma stays 2 px; the other gap takes all the spare room.
      EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 26, 84}));
      EXPECT_EQ(lines[0].right, 100);
    }
    Engine engine(punctuation(typoengine::PUNCT_SPACE_NONE));
    const auto lines = layout(words, 100, hyphenation);
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 24, 84}));
    EXPECT_EQ(lines[0].right, 100);
  }
}

TEST(SpaceAfterPunctuation, ALineWhoseOnlyGapFollowsAMarkStaysAsItIs) {
  Engine engine(punctuation(typoengine::PUNCT_SPACE_MINIMAL));
  EXPECT_EQ(layout({"aa,", "bb", "xxxxxxxxxxxx"}, 100)[0].xpos, (std::vector<int16_t>{0, 26}));
}

TEST(SpaceAfterPunctuation, NoneStillBreaksTheLineAfterTheMark) {
  // "aaaa,bbbb,cccc,dddd" with no spaces is 152 px: the line must still break after a comma.
  const std::vector<const char*> words{"aaaa,", "bbbb,", "cccc,", "dddd"};
  for (const bool hyphenation : {false, true}) {
    for (const CssTextAlign alignment : {CssTextAlign::Left, CssTextAlign::Justify}) {
      Engine engine(punctuation(typoengine::PUNCT_SPACE_NONE));
      const auto lines = layout(words, 90, hyphenation, alignment);
      ASSERT_EQ(wordsOf(lines), (std::vector<std::vector<std::string>>{{"aaaa,", "bbbb,"}, {"cccc,", "dddd"}}));
      EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 40}));
      EXPECT_EQ(lines[1].xpos, (std::vector<int16_t>{0, 40}));
    }
  }
}

TEST(SpaceAfterPunctuation, TheGapIsNotCondensedEither) {
  // "aa, bbbb cccc" with Minimal is 24 + 2 + 32 + 4 + 32 = 94 px. At 50% the ordinary gap gives 1 px (down to its
  // floor) and the gap after the comma nothing: the line fits 93 px and not 92.
  const std::vector<const char*> words{"aa,", "bbbb", "cccc", "dddddd"};
  for (const bool hyphenation : {false, true}) {
    SCOPED_TRACE(hyphenation);
    Config c = condense(50);
    c.punctSpace = typoengine::PUNCT_SPACE_MINIMAL;
    Engine engine(c);
    const auto lines = layout(words, 93, hyphenation);
    EXPECT_EQ(lines[0].words, (std::vector<std::string>{"aa,", "bbbb", "cccc"}));
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 26, 61}));
    EXPECT_EQ(lines[0].right, 93);
    EXPECT_EQ(layout(words, 92, hyphenation)[0].words, (std::vector<std::string>{"aa,", "bbbb"}));
  }
}

// --- Optical spacing ---

namespace {

Config optical() {
  Config c;
  c.opticalSpacing = true;
  return c;
}

// A fixture font with a 20 px space where 'a' has 1 px of empty room on each side and 'o' has 4 px.
void useBearingFont() {
  fixtureSpace = 20;
  fixtureBearings = {{'a', {1, 1}}, {'o', {4, 4}}, {'x', {0, 0}}, {'O', {12, 12}}};
}

// What the eye sees between two words of a line: the gap plus the empty room of the two glyphs around it.
std::vector<int> visibleGaps(const Line& line) {
  std::vector<int> out;
  for (size_t i = 1; i < line.words.size(); ++i) {
    const std::string& before = line.words[i - 1];
    const int gap = line.xpos[i] - (line.xpos[i - 1] + 8 * static_cast<int>(before.size()));
    out.push_back(gap + fixtureBearings.at(static_cast<uint32_t>(before.back())).second +
                  fixtureBearings.at(static_cast<uint32_t>(line.words[i].front())).first);
  }
  return out;
}

}  // namespace

TEST(OpticalSpacing, TheArithmetic) {
  using typoengine::opticalGap;
  EXPECT_EQ(opticalGap(20, 5, 5), 20);    // the average side room: the gap as it is
  EXPECT_EQ(opticalGap(20, 8, 5), 17);    // more room beside the glyphs: a narrower gap
  EXPECT_EQ(opticalGap(20, 2, 5), 23);    // less: a wider one
  EXPECT_EQ(opticalGap(20, 40, 0), 12);   // at most 40% of a space
  EXPECT_EQ(opticalGap(20, -40, 0), 28);  // either way
  EXPECT_EQ(opticalGap(0, 9, 0), 0);
  for (int space = 1; space <= 40; ++space) {
    for (int room = -60; room <= 60; ++room) {
      const int gap = opticalGap(space, room, 0);
      ASSERT_GE(gap * 100, space * typoengine::OPTICAL_MIN_PERCENT) << space << " " << room;
      ASSERT_GE(gap, 1);
      ASSERT_LE((gap > space ? gap - space : space - gap) * 100, space * typoengine::OPTICAL_MAX_PERCENT);
    }
  }
}

TEST(OpticalSpacing, TheGapsLookTheSameWidth) {
  // Side room of the gaps: a-a 2, a-o 5, o-o 8, o-a 5; the average is 5.
  const std::vector<const char*> words{"aa", "aa", "oo", "oo", "aa"};
  for (const bool hyphenation : {false, true}) {
    SCOPED_TRACE(hyphenation);
    {
      Engine engine{Config{}};
      useBearingFont();
      const auto off = layout(words, 400, hyphenation, CssTextAlign::Left);
      EXPECT_EQ(off[0].xpos, (std::vector<int16_t>{0, 36, 72, 108, 144}));
      EXPECT_EQ(visibleGaps(off[0]), (std::vector<int>{22, 25, 28, 25}));
    }
    Engine engine(optical());
    useBearingFont();
    const auto on = layout(words, 400, hyphenation, CssTextAlign::Left);
    EXPECT_EQ(on[0].xpos, (std::vector<int16_t>{0, 39, 75, 108, 144}));  // gaps of 23, 20, 17 and 20 px
    EXPECT_EQ(visibleGaps(on[0]), (std::vector<int>{25, 25, 25, 25}));
  }
}

TEST(OpticalSpacing, AJustifiedLineSharesTheRestEvenly) {
  // Five words of 16 px on a 180 px line leave 100 px for the four gaps; with their 20 px of side room each gap
  // looks 30 px wide.
  const std::vector<const char*> words{"aa", "aa", "oo", "oo", "aa", "xxxxxxxxxxxxxxxxxxxxxx"};
  for (const bool hyphenation : {false, true}) {
    SCOPED_TRACE(hyphenation);
    Engine engine(optical());
    useBearingFont();
    const auto lines = layout(words, 180, hyphenation);
    ASSERT_EQ(lines[0].words.size(), 5u);
    EXPECT_EQ(visibleGaps(lines[0]), (std::vector<int>{30, 30, 30, 30}));
    EXPECT_EQ(lines[0].right, 180);
  }
}

TEST(OpticalSpacing, TheCorrectionIsLimited) {
  // Side room: x-x 0 three times, x-O 12, O-O 24; the average is 36 / 5 = 7. The O-O gap would lose 17 px and
  // loses 8 (40% of the space).
  Engine engine(optical());
  useBearingFont();
  const auto lines = layout({"xx", "xx", "xx", "xx", "OO", "OO"}, 400, false, CssTextAlign::Left);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 43, 86, 129, 160, 188}));  // gaps of 27, 27, 27, 15 and 12 px
}

TEST(OpticalSpacing, AGlyphWithoutMetricsKeepsItsGap) {
  const std::vector<const char*> words{"aa", "bb", "oo", "bb", "aa", "oo"};
  Engine engine(optical());
  useBearingFont();
  // Only a-o has metrics on both sides, so it is the average itself: nothing moves.
  EXPECT_EQ(layout(words, 400, false, CssTextAlign::Left)[0].xpos, (std::vector<int16_t>{0, 36, 72, 108, 144, 180}));
  // And a font with no metrics at all lays out as with the setting off.
  fixtureBearings.clear();
  EXPECT_EQ(layout(words, 400, false, CssTextAlign::Left)[0].xpos, (std::vector<int16_t>{0, 36, 72, 108, 144, 180}));
}

TEST(OpticalSpacing, NotAfterANarrowedPunctuationGap) {
  Config c = optical();
  c.punctSpace = typoengine::PUNCT_SPACE_MINIMAL;
  Engine engine(c);
  useBearingFont();
  fixtureBearings[','] = {0, 9};
  // "aa," then "oo": half a space (10 px), whatever the side room. The other gaps: o-o 8 and o-a 5, average 7.
  const auto lines = layout({"aa,", "oo", "oo", "aa"}, 400, false, CssTextAlign::Left);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 34, 69, 107}));  // gaps of 10, 19 and 22 px
}

// --- Everything on: no line passes the right edge ---

TEST(TypographyEngineLayout, NoLinePassesThePageWidth) {
  const std::vector<const char*> words{"—",   "It",     "was",   "the",    "best",   "of", "times,", "it",
                                       "was", "the",    "worst", "of",     "times,", "«",  "it",     "was",
                                       "the", "age",    "of",    "wisdom", "»",      "a",  "b",      "foolishness",
                                       "and", "belief", "in",    "an",     "epoch",  "of", "i",      "incredulity"};
  Hyphenator::setPreferredLanguage("en");
  for (const int percent : {100, 90, 70, 50}) {
    for (uint8_t letters = 0; letters < typoengine::LETTER_SPACING_LEVELS; ++letters) {
      for (const bool locked : {false, true}) {
        Config c = condense(static_cast<uint8_t>(percent));
        c.letterSpacing = letters;
        c.lockedSpace = locked;
        // Space after punctuation and Optical spacing take turns with the rest.
        c.punctSpace =
            static_cast<uint8_t>((percent / 10 + letters + (locked ? 1 : 0)) % typoengine::PUNCT_SPACE_COUNT);
        c.opticalSpacing = (letters + (locked ? 1 : 0)) % 2 == 0;
        Engine engine(c);
        fixtureBearings = {{'t', {0, 3}}, {'s', {1, 1}}, {'f', {0, -2}}, {'o', {2, 2}},
                           {'i', {1, 1}}, {'w', {0, 0}}, {'a', {1, 2}},  {'e', {3, 1}}};
        Hyphenator::setPreferredLanguage("en");
        for (uint16_t width = 96; width <= 260; ++width) {
          for (const bool hyphenation : {false, true}) {
            const auto lines = layout(words, width, hyphenation);
            size_t wordCount = 0;
            for (size_t i = 0; i < lines.size(); ++i) {
              ASSERT_LE(lines[i].right, width)
                  << "line " << i << ", width " << width << ", " << percent << "%, letters " << int(letters)
                  << ", locked " << locked << ", hyphenation " << hyphenation;
              ASSERT_GE(lines[i].xpos.front(), 0);
              for (size_t w = 1; w < lines[i].xpos.size(); ++w) {
                // Words stay in order and never overlap: a condensed gap is still a gap.
                ASSERT_GT(lines[i].xpos[w], lines[i].xpos[w - 1]);
                // and with None the next word starts where the mark ends, never before it
                ASSERT_GE(lines[i].xpos[w], lines[i].xpos[w - 1] + 8);
              }
              wordCount += lines[i].words.size();
            }
            ASSERT_GE(wordCount, words.size());  // nothing is lost (hyphenation may add parts)
          }
        }
      }
    }
  }
}

// --- The floor under a word gap (0.77.1) ---

TEST(GapFloor, TheArithmetic) {
  EXPECT_EQ(typoengine::GAP_FLOOR_PERCENT, 60);
  EXPECT_EQ(typoengine::gapFloor(12), 8);  // 7.2 px rounds up
  EXPECT_EQ(typoengine::gapFloor(10), 6);
  EXPECT_EQ(typoengine::gapFloor(4), 3);
  // A 12 px space at 50%: the share alone would give 6 px, the floor leaves 4.
  EXPECT_EQ(typoengine::gapAllowance(12, 12, 50), 4);
  EXPECT_EQ(typoengine::gapAllowance(12, 12, 80), 2);  // 80% asks for less than the floor allows
  EXPECT_EQ(typoengine::gapAllowance(12, 12, 100), 0);
  // The same space after Optical spacing made it 9 px: 50% of that would be 5 px (glued words), now 8.
  EXPECT_EQ(typoengine::gapAllowance(9, 12, 50), 1);
  EXPECT_EQ(typoengine::gapAllowance(8, 12, 50), 0);
  EXPECT_EQ(typoengine::gapAllowance(7, 12, 50), 0);   // already under it: nothing more
  EXPECT_EQ(typoengine::gapAllowance(16, 12, 50), 8);  // a gap Optical spacing widened gives its whole share
  for (int space = 0; space <= 40; ++space) {
    // Optical spacing alone never goes under the floor.
    for (int room = -40; room <= 40; ++room)
      ASSERT_GE(typoengine::opticalGap(space, room, 0), typoengine::gapFloor(space));
    for (int gap = 0; gap <= space * 2; ++gap) {
      for (const int percent : {50, 60, 70, 80, 90, 100}) {
        const int allowance = typoengine::gapAllowance(gap, space, percent);
        ASSERT_GE(allowance, 0);
        ASSERT_LE(allowance, typoengine::gapShrink(gap, percent));
        ASSERT_GE(gap - allowance, std::min(gap, typoengine::gapFloor(space)));
      }
    }
  }
}

TEST(GapFloor, ALineIsCondensedOnlyDownToTheFloor) {
  for (const bool hyphenation : {false, true}) {
    SCOPED_TRACE(hyphenation);
    Engine engine(condense(50));
    fixtureSpace = 20;
    // "aaaa bbbb cccc" is 32 + 20 + 32 + 20 + 32 = 136 px. A 20 px gap may go to 12, not to 10: the line fits
    // 120 px and not 119.
    const auto lines = layout(kThreeAndOne, 120, hyphenation);
    EXPECT_EQ(lines[0].words, (std::vector<std::string>{"aaaa", "bbbb", "cccc"}));
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 44, 88}));
    EXPECT_EQ(lines[0].right, 120);
    EXPECT_EQ(layout(kThreeAndOne, 119, hyphenation)[0].words, (std::vector<std::string>{"aaaa", "bbbb"}));
  }
}

TEST(GapFloor, CondensingOpticalSpacingAndMinimalPunctuationTogetherLeaveNoGapUnderIt) {
  // The settings of the report (Space condensing 50%, Optical spacing, Space after punctuation Minimal) on a font
  // with a 20 px space, so the floor is 12 px. Up to 0.77.0 Optical spacing took a gap to 12 px and condensing took
  // half of that again.
  const std::vector<const char*> words{"oooo", "oao", "aoa,", "oooo", "ooo", "aaaa", "oo",  "ooooo", "aoao.",
                                       "oo",   "ooo", "aaa",  "oooo", "oa",  "ao;",  "ooo", "oooo",  "aaaaa",
                                       "ooo",  "oo",  "oooo", "aoo",  "ooa", "oooo", "ooo", "oo",    "ooooo"};
  Config c = condense(50);
  c.opticalSpacing = true;
  c.punctSpace = typoengine::PUNCT_SPACE_MINIMAL;
  Engine engine(c);
  size_t condensedLines = 0;
  for (uint16_t width = 150; width <= 420; ++width) {
    for (const bool hyphenation : {false, true}) {
      useBearingFont();
      const auto lines = layout(words, width, hyphenation);
      for (size_t l = 0; l < lines.size(); ++l) {
        const auto& line = lines[l];
        ASSERT_LE(line.right, width) << "width " << width << ", line " << l;
        bool condensed = false;
        for (size_t w = 1; w < line.words.size(); ++w) {
          const std::string& before = line.words[w - 1];
          const int gap = line.xpos[w] - (line.xpos[w - 1] + 8 * static_cast<int>(before.size()));
          const char last = before.back();
          if (last == ',' || last == '.' || last == ';') {
            ASSERT_EQ(gap, 10) << "width " << width << ", line " << l;  // Minimal: half a space, as designed
          } else {
            ASSERT_GE(gap, typoengine::gapFloor(20)) << "width " << width << ", line " << l << ", word " << w;
            condensed = condensed || gap < 20;
          }
        }
        if (condensed) ++condensedLines;
      }
    }
  }
  EXPECT_GT(condensedLines, 100u);  // the floor was really reached for, not just never needed
}

// --- Loose line balancing (0.77.1) ---

namespace {

// Lays the words out with the balancing off, then on.
struct Balanced {
  std::vector<Line> off;
  std::vector<Line> on;
  size_t offCalls = 0;
  size_t onCalls = 0;
};

Balanced balanced(const std::vector<const char*>& words, const uint16_t width, const bool hyphenation = true) {
  Balanced out;
  // The balancing belongs to the line-by-line breaker, which now lays out what the total-fit breaker leaves to it.
  ParsedText::totalFitHyphenation = false;
  struct Restore {
    ~Restore() { ParsedText::totalFitHyphenation = true; }
  } restore;
  ParsedText::balanceLooseLines = false;
  out.off = layout(words, width, hyphenation);
  out.offCalls = lastSpaceAdvanceCalls;
  ParsedText::balanceLooseLines = true;
  out.on = layout(words, width, hyphenation);
  out.onCalls = lastSpaceAdvanceCalls;
  return out;
}

// What each gap of a justified line grew by.
int stretchOf(const Line& line, const int space = 4) {
  int widest = 0;
  for (size_t w = 1; w < line.words.size(); ++w) {
    const int gap = line.xpos[w] - (line.xpos[w - 1] + 8 * static_cast<int>(line.words[w - 1].size()));
    widest = std::max(widest, gap - space);
  }
  return widest;
}

// A word no pattern can split: it does not fit after "ffff gggg" on a 200 px line and leaves that line loose.
const std::vector<const char*> kLooseSecondLine{
    "aaaa", "bbbb", "cccc", "dddd", "eeee", "ffff", "gggg", "xxxxxxxxxxxxxxxxxxxxxx", "hhhh", "iiii", "jjjj"};

}  // namespace

TEST(LooseLineBalancing, TheLineAboveGivesItsLastWordToALooseLine) {
  // 90% of a 4 px space rounds to 4 px: condensing is on and changes nothing itself.
  Engine engine(condense(90));
  const auto result = balanced(kLooseSecondLine, 200);
  // Filled as far as it goes: five words (176 px), then "ffff gggg" with a gap that grows by 132 px.
  ASSERT_GE(result.off.size(), 3u);
  EXPECT_EQ(result.off[0].words, (std::vector<std::string>{"aaaa", "bbbb", "cccc", "dddd", "eeee"}));
  EXPECT_EQ(result.off[1].words, (std::vector<std::string>{"ffff", "gggg"}));
  EXPECT_EQ(stretchOf(result.off[1]), 132);
  // Balanced: "eeee" moves down. The gaps grow by 20 px above and 48 px below. A second word would only swap the
  // two lines (48 and 20), so it stays.
  ASSERT_GE(result.on.size(), 3u);
  EXPECT_EQ(result.on[0].words, (std::vector<std::string>{"aaaa", "bbbb", "cccc", "dddd"}));
  EXPECT_EQ(result.on[1].words, (std::vector<std::string>{"eeee", "ffff", "gggg"}));
  EXPECT_EQ(stretchOf(result.on[0]), 20);
  EXPECT_EQ(stretchOf(result.on[1]), 48);
  EXPECT_EQ(result.on[0].right, 200);
  EXPECT_EQ(result.on[1].right, 200);
  // The lines after them are the same.
  ASSERT_EQ(result.on.size(), result.off.size());
  for (size_t l = 2; l < result.on.size(); ++l) EXPECT_EQ(result.on[l].words, result.off[l].words);
}

TEST(LooseLineBalancing, OnlyWithCondensingOrLetterSpacingOnAndOnlyInJustifiedText) {
  {
    // The engine off, and on with a setting that is not about filling lines: the lines break as before.
    const auto off = balanced(kLooseSecondLine, 200);
    EXPECT_EQ(wordsOf(off.on), wordsOf(off.off));
    EXPECT_EQ(off.on[0].words.size(), 5u);
    Config locked;
    locked.lockedSpace = true;
    Engine engine(locked);
    const auto other = balanced(kLooseSecondLine, 200);
    EXPECT_EQ(wordsOf(other.on), wordsOf(other.off));
  }
  {
    Config small;
    small.letterSpacing = 1;
    Engine engine(small);
    const auto justified = balanced(kLooseSecondLine, 200);
    EXPECT_EQ(justified.on[0].words.size(), 4u);
    ParsedText::balanceLooseLines = true;
    for (const CssTextAlign alignment : {CssTextAlign::Left, CssTextAlign::Center, CssTextAlign::Right}) {
      EXPECT_EQ(layout(kLooseSecondLine, 200, true, alignment)[0].words.size(), 5u);
    }
    // Without hyphenation the other breaker lays the paragraph out; it already weighs every line's spare room.
    const auto whole = balanced(kLooseSecondLine, 200, false);
    EXPECT_EQ(wordsOf(whole.on), wordsOf(whole.off));
  }
}

TEST(LooseLineBalancing, ALineThatIsNotLooseAndTheLastLineStayAsTheyAre) {
  Engine engine(condense(90));
  // "ffff gggg hhhh iiii" (140 px) is the second line: its gaps grow by 20 px, which is over a space, but moving
  // "eeee" down does not fit (176 px + 36 px), so nothing moves.
  const auto full = balanced(
      {"aaaa", "bbbb", "cccc", "dddd", "eeee", "ffff", "gggg", "hhhh", "iiii", "xxxxxxxxxxxxxxxxxxxxxx", "jjjj"}, 200);
  EXPECT_EQ(wordsOf(full.on), wordsOf(full.off));
  // The loose line is the paragraph's last: it is not justified, so it is left alone.
  const auto last = balanced({"aaaa", "bbbb", "cccc", "dddd", "eeee", "ffff", "gggg"}, 200);
  EXPECT_EQ(wordsOf(last.on), wordsOf(last.off));
  EXPECT_EQ(last.on[0].words.size(), 5u);
}

TEST(LooseLineBalancing, NoWordIsLostNoPartOfAHyphenatedWordMovesAndTheLooserLineNeverGetsLooser) {
  const std::vector<const char*> words{"It",          "was",
                                       "the",         "best",
                                       "of",          "times",
                                       "it",          "was",
                                       "the",         "worst",
                                       "of",          "times",
                                       "it",          "was",
                                       "the",         "age",
                                       "of",          "wisdom",
                                       "and",         "foolishness",
                                       "and",         "belief",
                                       "in",          "an",
                                       "epoch",       "of",
                                       "incredulity", "xxxxxxxxxxxxxxxx",
                                       "season",      "of",
                                       "light",       "and",
                                       "darkness",    "everything",
                                       "before",      "us",
                                       "nothing",     "xxxxxxxxxxxxxxxxxxxx",
                                       "we",          "were",
                                       "all",         "going",
                                       "direct",      "to"};
  size_t changed = 0;
  for (const int percent : {100, 90, 50}) {
    for (const uint8_t letters : {uint8_t{0}, uint8_t{2}, uint8_t{4}}) {
      if (percent == 100 && letters == 0) continue;
      Config c = condense(static_cast<uint8_t>(percent));
      c.letterSpacing = letters;
      Engine engine(c);
      for (uint16_t width = 96; width <= 300; ++width) {
        Hyphenator::setPreferredLanguage("en");
        const auto result = balanced(words, width);
        // The same words in the same order, with the same hyphenated parts: only where lines end may differ.
        std::vector<std::string> offWords;
        std::vector<std::string> onWords;
        for (const auto& line : result.off) offWords.insert(offWords.end(), line.words.begin(), line.words.end());
        for (const auto& line : result.on) onWords.insert(onWords.end(), line.words.begin(), line.words.end());
        ASSERT_EQ(onWords, offWords) << "width " << width;
        ASSERT_EQ(result.on.size(), result.off.size()) << "width " << width;
        for (size_t l = 0; l < result.on.size(); ++l) {
          const auto& line = result.on[l];
          ASSERT_LE(line.right, width) << "width " << width << ", line " << l;
          // A part of a hyphenated word still ends its line.
          for (size_t w = 0; w + 1 < line.words.size(); ++w) {
            ASSERT_NE(line.words[w].back(), '-') << "width " << width << ", line " << l;
          }
        }
        if (wordsOf(result.on) != wordsOf(result.off)) ++changed;
      }
    }
  }
  EXPECT_GT(changed, 20u);  // the balancing did act on these widths
}

TEST(LooseLineBalancing, CostsLittleMoreThanFillingTheLines) {
  // A chapter-long paragraph of long words (a Greek sentence in Latin letters, which no pattern set splits, and one
  // word that never fits beside another): about 2,400 words.
  const std::vector<const char*> sentence{
      "einai",     "simantiko",  "giati",      "katalavainoume",           "perissotera", "apotelesmata",
      "otan",      "diavazoume", "prosektika", "xxxxxxxxxxxxxxxxxxxxxxxx", "kai",         "anthropistikes",
      "epistimes", "xoris",      "viasyni",    "parakolouthoume"};
  std::vector<const char*> words;
  for (int i = 0; i < 150; ++i) words.insert(words.end(), sentence.begin(), sentence.end());
  Config c = condense(50);
  c.letterSpacing = 2;
  Engine engine(c);
  fixtureSpace = 12;
  const auto result = balanced(words, 440);
  EXPECT_TRUE(wordsOf(result.on) != wordsOf(result.off));  // there were loose lines to balance
  // Work, counted as questions to the font about a space (one for every gap looked at): at most 15% more, in a
  // paragraph made to have a loose line every few lines (10% when this was written: 7,776 against 7,051).
  EXPECT_LE(result.onCalls * 100, result.offCalls * 115) << result.onCalls << " against " << result.offCalls;
  // Time, for the record only (a computer is not the reader): the whole layout, 20 times each way.
  const auto timeOf = [&](const bool on) {
    ParsedText::balanceLooseLines = on;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) layout(words, 440, true);
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  };
  const double offMs = timeOf(false);
  const double onMs = timeOf(true);
  ParsedText::balanceLooseLines = true;
  RecordProperty("space_questions_off", static_cast<int>(result.offCalls));
  RecordProperty("space_questions_on", static_cast<int>(result.onCalls));
  std::printf("loose line balancing: %zu against %zu space questions, %.1f ms against %.1f ms for 20 layouts\n",
              result.onCalls, result.offCalls, onMs, offMs);
}

// --- Hyphenation: mode, minimum letters, language ---

namespace {

std::vector<size_t> offsets(const std::string& word, const bool fallback = false) {
  std::vector<size_t> out;
  for (const auto& info : Hyphenator::breakOffsets(word, fallback)) out.push_back(info.byteOffset);
  return out;
}

bool isSubset(const std::vector<size_t>& part, const std::vector<size_t>& whole) {
  for (const size_t value : part) {
    if (std::find(whole.begin(), whole.end(), value) == whole.end()) return false;
  }
  return true;
}

}  // namespace

TEST(HyphenationMode, SoftHyphensOnlyDropsThePatternBreaks) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  ASSERT_FALSE(offsets("hyphenation").empty());  // patterns, as before

  Config soft;
  soft.hyphenMode = typoengine::HYPHEN_SOFT_ONLY;
  typoengine::set(typoengine::pack(soft));
  EXPECT_TRUE(offsets("hyphenation").empty());
  // A soft hyphen in the word is still a break, and the only one: after "hy" + U+00AD (4 bytes), with a hyphen drawn.
  const auto breaks = Hyphenator::breakOffsets("hy\xC2\xADphenation", false);
  ASSERT_EQ(breaks.size(), 1u);
  EXPECT_EQ(breaks[0].byteOffset, 4u);
  EXPECT_TRUE(breaks[0].requiresInsertedHyphen);
  // A visible hyphen too, with nothing drawn.
  const auto visible = Hyphenator::breakOffsets("well-meaning", false);
  ASSERT_EQ(visible.size(), 1u);
  EXPECT_EQ(visible[0].byteOffset, 5u);
  EXPECT_FALSE(visible[0].requiresInsertedHyphen);
  // No apostrophe break either.
  EXPECT_TRUE(offsets("all'improvviso").empty());
  // A word wider than the line can still be cut, or it would run off the page.
  EXPECT_FALSE(offsets("hyphenation", true).empty());
}

TEST(HyphenationMode, SoftHyphensOnlyInALaidOutParagraph) {
  Hyphenator::setPreferredLanguage("en");
  // "aaaa" + a gap leaves 64 px: with patterns "hyphenation" (88 px) is split there; with soft hyphens only it
  // moves to the next line whole.
  const std::vector<const char*> words{"aaaa", "hyphenation", "bbbb"};
  typoengine::set(0);
  const auto patterns = layout(words, 100, true);
  ASSERT_FALSE(patterns.empty());
  ASSERT_EQ(patterns[0].words.size(), 2u);
  EXPECT_EQ(patterns[0].words[1].back(), '-');

  Config soft;
  soft.hyphenMode = typoengine::HYPHEN_SOFT_ONLY;
  Engine engine(soft);
  Hyphenator::setPreferredLanguage("en");
  const auto softOnly = layout(words, 100, true);
  EXPECT_EQ(softOnly[0].words, (std::vector<std::string>{"aaaa"}));
  EXPECT_EQ(softOnly[1].words.front(), "hyphenation");
  // With a soft hyphen in the word, that is where it breaks.
  const auto marked = layout({"aaaa",
                              "hyphen\xC2\xAD"
                              "ation",
                              "bbbb"},
                             100, true);
  EXPECT_EQ(marked[0].words, (std::vector<std::string>{"aaaa", "hyphen-"}));
  EXPECT_EQ(marked[1].words.front(), "ation");
}

TEST(HyphenationMinimumLetters, DefaultIsTheLanguagesOwn) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  const auto base = offsets("international");
  Config zero;
  zero.minLeft = 0;
  zero.minRight = 0;
  typoengine::set(typoengine::pack(zero));
  EXPECT_EQ(offsets("international"), base);
}

TEST(HyphenationMinimumLetters, LeftAndRightMinimumsCutTheBreaks) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  const std::string word = "international";  // ASCII: a byte offset is a letter count
  const auto base = offsets(word);
  ASSERT_FALSE(base.empty());

  for (uint8_t minimum = 1; minimum <= typoengine::MIN_LETTERS_MAX; ++minimum) {
    Config left;
    left.minLeft = minimum;
    typoengine::set(typoengine::pack(left));
    for (const size_t offset : offsets(word)) EXPECT_GE(offset, minimum) << int(minimum);

    Config right;
    right.minRight = minimum;
    typoengine::set(typoengine::pack(right));
    for (const size_t offset : offsets(word)) EXPECT_GE(word.size() - offset, minimum) << int(minimum);
  }

  // A larger minimum only removes breaks: what is left is exactly the usual breaks that keep to it.
  for (const char* sample : {"international", "hyphenation", "understanding", "responsibility"}) {
    const std::string text = sample;
    typoengine::set(0);
    const auto usual = offsets(text);
    Config wide;
    wide.minLeft = 5;
    wide.minRight = 5;
    typoengine::set(typoengine::pack(wide));
    std::vector<size_t> kept;
    for (const size_t offset : usual) {
      if (offset >= 5 && text.size() - offset >= 5) kept.push_back(offset);
    }
    EXPECT_EQ(offsets(text), kept) << sample;
  }
  // A smaller one only adds them.
  Config narrow;
  narrow.minLeft = 1;
  narrow.minRight = 1;
  typoengine::set(typoengine::pack(narrow));
  EXPECT_TRUE(isSubset(base, offsets(word)));
}

TEST(HyphenationMinimumLetters, AWordTooShortForTheMinimumsIsNotBroken) {
  Config wide;
  wide.minLeft = 5;
  wide.minRight = 5;
  Engine engine(wide);
  Hyphenator::setPreferredLanguage("en");
  EXPECT_TRUE(offsets("national").empty());  // 8 letters: no place with 5 on each side
  // The fallback for a word wider than the line keeps to the minimums as well.
  for (const size_t offset : offsets("zzzzzzzzzzzz", true)) {
    EXPECT_GE(offset, 5u);
    EXPECT_GE(12u - offset, 5u);
  }
}

TEST(HyphenationLanguage, BookLanguageKeepsGreekWordsOnGreekPatterns) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  EXPECT_FALSE(offsets("καλημέρα").empty());  // a Greek word in a book tagged English, as before
}

TEST(HyphenationLanguage, AChosenLanguageIsUsedForEveryWord) {
  Engine engine(Config{});
  // What German patterns give for a German word.
  Hyphenator::setPreferredLanguage("de");
  const auto german = offsets("Quadratkilometer");
  ASSERT_FALSE(german.empty());
  Hyphenator::setPreferredLanguage("en");
  const auto english = offsets("Quadratkilometer");

  Config c;
  for (uint8_t i = 0; i < typoengine::LANGUAGE_COUNT; ++i) {
    if (std::string(typoengine::LANGUAGE_TAGS[i]) == "de") c.language = static_cast<uint8_t>(i + 1);
  }
  ASSERT_NE(c.language, 0);
  typoengine::set(typoengine::pack(c));
  EXPECT_EQ(offsets("Quadratkilometer"), german);  // the book says English, the setting says German
  if (english != german) {
    EXPECT_NE(offsets("Quadratkilometer"), english);
  }

  // English chosen: a Greek word no longer switches to the Greek patterns, and English ones do not break it.
  for (uint8_t i = 0; i < typoengine::LANGUAGE_COUNT; ++i) {
    if (std::string(typoengine::LANGUAGE_TAGS[i]) == "en") c.language = static_cast<uint8_t>(i + 1);
  }
  typoengine::set(typoengine::pack(c));
  EXPECT_TRUE(offsets("καλημέρα").empty());

  // Greek chosen: the Greek word breaks whatever the book says.
  for (uint8_t i = 0; i < typoengine::LANGUAGE_COUNT; ++i) {
    if (std::string(typoengine::LANGUAGE_TAGS[i]) == "el") c.language = static_cast<uint8_t>(i + 1);
  }
  typoengine::set(typoengine::pack(c));
  Hyphenator::setPreferredLanguage("fr");
  EXPECT_FALSE(offsets("καλημέρα").empty());
}

TEST(HyphenationLanguage, EveryChoiceHasAPatternSet) {
  // Each of the 13 tags must name a set the firmware has: a missing one would silently hyphenate nothing.
  const std::vector<std::pair<const char*, const char*>> samples{
      {"en", "international"}, {"fr", "internationale"}, {"de", "Quadratkilometer"}, {"ru", "международный"},
      {"es", "internacional"}, {"it", "internazionale"}, {"pl", "międzynarodowy"},   {"sv", "internationell"},
      {"uk", "міжнародний"},   {"fi", "kansainvälinen"}, {"pt", "internacional"},    {"el", "καλημέρα"},
      {"grc", "ἄνθρωπος"}};
  ASSERT_EQ(samples.size(), static_cast<size_t>(typoengine::LANGUAGE_COUNT));
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("");
  for (uint8_t i = 0; i < typoengine::LANGUAGE_COUNT; ++i) {
    EXPECT_STREQ(typoengine::LANGUAGE_TAGS[i], samples[i].first);
    Config c;
    c.language = static_cast<uint8_t>(i + 1);
    typoengine::set(typoengine::pack(c));
    EXPECT_FALSE(offsets(samples[i].second).empty()) << samples[i].first;
  }
}

// --- Total-fit line breaking (justified text with hyphenation on) ---

namespace {

// Lays the words out with the line-by-line breaker, then with the total-fit one.
struct Fitted {
  std::vector<Line> greedy;
  std::vector<Line> total;
  bool usedTotalFit = false;
};

Fitted fitted(const std::vector<const char*>& words, const uint16_t width,
              const CssTextAlign alignment = CssTextAlign::Justify) {
  Fitted out;
  ParsedText::totalFitHyphenation = false;
  out.greedy = layout(words, width, true, alignment);
  ParsedText::totalFitHyphenation = true;
  const uint32_t before = ParsedText::totalFitParagraphs;
  out.total = layout(words, width, true, alignment);
  out.usedTotalFit = ParsedText::totalFitParagraphs != before;
  return out;
}

bool endsInHyphen(const Line& line) { return !line.words.empty() && line.words.back().back() == '-'; }

int hyphenatedLines(const std::vector<Line>& lines) {
  int count = 0;
  for (const auto& line : lines) count += endsInHyphen(line) ? 1 : 0;
  return count;
}

// True when the lines hold the source words in order, whole or split with a '-' at a line end (the source words of
// these tests have no hyphen of their own).
bool keepsTheText(const std::vector<Line>& lines, const std::vector<const char*>& source) {
  std::string all;
  for (const auto& line : lines) {
    for (const auto& word : line.words) all += word;
  }
  all.erase(std::remove(all.begin(), all.end(), '-'), all.end());
  std::string expected;
  for (const char* word : source) expected += word;
  return all == expected;
}

const std::vector<const char*> kProse{"the",
                                      "international",
                                      "committee",
                                      "for",
                                      "understanding",
                                      "information",
                                      "has",
                                      "published",
                                      "another",
                                      "remarkable",
                                      "collection",
                                      "of",
                                      "observations",
                                      "about",
                                      "the",
                                      "hyphenation",
                                      "of",
                                      "ordinary",
                                      "sentences",
                                      "in",
                                      "narrow",
                                      "columns",
                                      "where",
                                      "every",
                                      "line",
                                      "matters",
                                      "and",
                                      "nothing",
                                      "is",
                                      "forgiven",
                                      "by",
                                      "the",
                                      "reader",
                                      "who",
                                      "notices",
                                      "everything",
                                      "immediately",
                                      "and",
                                      "remembers",
                                      "it",
                                      "afterwards",
                                      "without",
                                      "any",
                                      "particular",
                                      "difficulty",
                                      "whatsoever"};

// The widest any gap of the justified lines (all but the last) grew by.
int worstStretch(const std::vector<Line>& lines, const int space = 4) {
  int worst = 0;
  for (size_t i = 0; i + 1 < lines.size(); ++i) worst = std::max(worst, stretchOf(lines[i], space));
  return worst;
}

std::vector<std::string> flat(const std::vector<Line>& lines) {
  std::vector<std::string> out;
  for (const auto& line : lines) out.insert(out.end(), line.words.begin(), line.words.end());
  return out;
}

}  // namespace

TEST(TotalFit, FewerHyphensAndNoLooserLinesThanFillingLineByLine) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  // Found by laying kProse out at every width: at these two the line-by-line breaker splits a word that the
  // paragraph as a whole does not need split.
  for (const uint16_t width : {176, 336}) {
    const Fitted f = fitted(kProse, width);
    ASSERT_TRUE(f.usedTotalFit) << width;
    EXPECT_LT(hyphenatedLines(f.total), hyphenatedLines(f.greedy)) << width;
    EXPECT_LT(worstStretch(f.total), worstStretch(f.greedy)) << width;
    EXPECT_TRUE(keepsTheText(f.total, kProse)) << width;
  }
}

TEST(TotalFit, NoMoreLinesThanFillingLineByLineAndNoLinePastTheEdge) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  int used = 0;
  for (uint16_t width = 160; width <= 420; width += 4) {
    const Fitted f = fitted(kProse, width);
    if (!f.usedTotalFit) continue;
    ++used;
    // A line costs more than any evening out of the gaps, so the paragraph does not grow.
    EXPECT_LE(f.total.size(), f.greedy.size()) << width;
    EXPECT_TRUE(keepsTheText(f.total, kProse)) << width;
    for (const auto& line : f.total) EXPECT_LE(line.right, width) << width;
    // Every justified line ends at the right edge (the spare pixels are shared out rounded down: less than one a
    // gap is left over).
    for (size_t i = 0; i + 1 < f.total.size(); ++i) {
      const int words = static_cast<int>(f.total[i].words.size());
      if (words > 1) EXPECT_GT(f.total[i].right, width - words + 1) << width << " line " << i;
    }
  }
  EXPECT_GT(used, 60);
}

TEST(TotalFit, NoWordIsSplitWhenNoSplitIsChosen) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  const std::vector<std::string> source(kProse.begin(), kProse.end());
  int unsplit = 0;
  for (uint16_t width = 160; width <= 520; width += 4) {
    const Fitted f = fitted(kProse, width);
    if (!f.usedTotalFit) continue;
    // One split word shows as two words, the first with a '-': the word list grows by one for each hyphenated
    // line and by nothing else, though every hyphenation point of the paragraph was measured.
    EXPECT_EQ(flat(f.total).size(), source.size() + hyphenatedLines(f.total)) << width;
    if (hyphenatedLines(f.total) == 0) {
      ++unsplit;
      EXPECT_EQ(flat(f.total), source) << width;
    }
  }
  EXPECT_GT(unsplit, 0);

  // A paragraph of one line is not weighed at all.
  const uint32_t before = ParsedText::totalFitParagraphs;
  const auto oneLine = layout({"international", "understanding"}, 400, true);
  EXPECT_EQ(ParsedText::totalFitParagraphs, before + 1);
  EXPECT_EQ(wordsOf(oneLine), (std::vector<std::vector<std::string>>{{"international", "understanding"}}));
}

TEST(TotalFit, AHugeParagraphIsFilledLineByLine) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  std::vector<const char*> words;
  while (words.size() < ParsedText::TOTAL_FIT_MAX_WORDS) words.push_back(kProse[words.size() % kProse.size()]);
  const Fitted atLimit = fitted(words, 300);
  EXPECT_TRUE(atLimit.usedTotalFit);
  EXPECT_TRUE(keepsTheText(atLimit.total, words));

  // What weighing the largest paragraph it takes costs on this computer, next to filling its lines one by one.
  const auto timed = [&words](const bool totalFit) {
    ParsedText::totalFitHyphenation = totalFit;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) layout(words, 300, true);
    ParsedText::totalFitHyphenation = true;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  };
  const double greedyMs = timed(false);
  const double totalMs = timed(true);
  printf("total fit: %.1f ms against %.1f ms line by line for 20 layouts of %zu words\n", totalMs, greedyMs,
         words.size());

  words.push_back("more");
  const Fitted over = fitted(words, 300);
  EXPECT_FALSE(over.usedTotalFit);
  EXPECT_EQ(wordsOf(over.total), wordsOf(over.greedy));
  EXPECT_EQ(xposOf(over.total), xposOf(over.greedy));
}

TEST(TotalFit, OnlyForJustifiedTextAndAWordWiderThanTheLineGoesToTheOtherBreaker) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  const Fitted left = fitted(kProse, 240, CssTextAlign::Left);
  EXPECT_FALSE(left.usedTotalFit);
  EXPECT_EQ(wordsOf(left.total), wordsOf(left.greedy));

  // 30 letters no pattern splits are 240 px: wider than the line, so no set of breaks fits and the line-by-line
  // breaker splits the word wherever it has to.
  const std::vector<const char*> wide{"aaaa", "bbbb", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "cccc", "dddd"};
  const Fitted f = fitted(wide, 200);
  EXPECT_FALSE(f.usedTotalFit);
  EXPECT_EQ(wordsOf(f.total), wordsOf(f.greedy));
  for (const auto& line : f.total) EXPECT_LE(line.right, 200);

  // Hyphenation off: the older total-fit breaker, as before.
  const uint32_t before = ParsedText::totalFitParagraphs;
  layout(kProse, 240, false);
  EXPECT_EQ(ParsedText::totalFitParagraphs, before);
}

TEST(TotalFit, ACondensedLineNeverGoesUnderTheGapFloor) {
  Engine engine(condense(50));
  Hyphenator::setPreferredLanguage("en");
  fixtureSpace = 10;
  const int floor = typoengine::gapFloor(10);
  ASSERT_EQ(floor, 6);
  int condensedLines = 0;
  for (uint16_t width = 200; width <= 420; width += 4) {
    const Fitted f = fitted(kProse, width);
    if (!f.usedTotalFit) continue;
    EXPECT_TRUE(keepsTheText(f.total, kProse)) << width;
    for (size_t i = 0; i < f.total.size(); ++i) {
      const Line& line = f.total[i];
      EXPECT_LE(line.right, width) << width << " line " << i;
      bool condensed = false;
      for (size_t w = 1; w < line.words.size(); ++w) {
        const int gap = line.xpos[w] - (line.xpos[w - 1] + 8 * static_cast<int>(line.words[w - 1].size()));
        EXPECT_GE(gap, floor) << width << " line " << i;
        condensed = condensed || gap < 10;
      }
      // The last line is not justified, so it is never condensed.
      if (i + 1 == f.total.size()) EXPECT_FALSE(condensed) << width;
      condensedLines += condensed ? 1 : 0;
    }
  }
  EXPECT_GT(condensedLines, 0);
}

// --- Hyphenation patterns for a book that names no language ---

namespace {

struct FallbackLanguage {
  explicit FallbackLanguage(const char* tag) { Hyphenator::setFallbackLanguage(tag); }
  ~FallbackLanguage() {
    Hyphenator::setFallbackLanguage("");
    Hyphenator::setPreferredLanguage("");
  }
};

// What the patterns of `tag` give for the word, with the book tagged as that language.
std::vector<size_t> offsetsIn(const char* tag, const std::string& word) {
  Hyphenator::setPreferredLanguage(tag);
  return offsets(word);
}

}  // namespace

TEST(HyphenationLanguageFallback, ABookWithNoLanguageBreaksWordsByScript) {
  Engine engine(Config{});
  FallbackLanguage ui("");
  const auto english = offsetsIn("en", "international");
  const auto russian = offsetsIn("ru", "международный");
  const auto greek = offsetsIn("el", "καλημέρα");
  ASSERT_FALSE(english.empty());
  ASSERT_FALSE(russian.empty());
  ASSERT_FALSE(greek.empty());
  for (const char* none : {"", "und", "UND", "mul", "zxx", "mis", "-", "x", "english", "e1"}) {
    Hyphenator::setPreferredLanguage(none);
    EXPECT_EQ(offsets("international"), english) << none;
    EXPECT_EQ(offsets("международный"), russian) << none;
    EXPECT_EQ(offsets("καλημέρα"), greek) << none;
    // A word of two scripts has no pattern set of its own.
    EXPECT_TRUE(offsets("interнародный").empty()) << none;
  }
}

TEST(HyphenationLanguageFallback, LatinWordsTakeTheInterfaceLanguageWhenItHasPatterns) {
  Engine engine(Config{});
  const auto german = offsetsIn("de", "Quadratkilometer");
  const auto english = offsetsIn("en", "Quadratkilometer");
  const auto russian = offsetsIn("ru", "международный");
  const auto ukrainian = offsetsIn("uk", "міжнародний");
  ASSERT_FALSE(german.empty());
  ASSERT_NE(german, english);
  {
    FallbackLanguage ui("DE");  // the codes of the interface languages are upper case
    Hyphenator::setPreferredLanguage("");
    EXPECT_EQ(offsets("Quadratkilometer"), german);
    EXPECT_EQ(offsets("международный"), russian);
  }
  // An interface language with no patterns, or with patterns of another script: English for Latin words.
  for (const char* tag : {"NL", "EL", "RU", "UK", "JA", ""}) {
    FallbackLanguage ui(tag);
    Hyphenator::setPreferredLanguage("");
    EXPECT_EQ(offsets("Quadratkilometer"), english) << tag;
  }
  {
    FallbackLanguage ui("UK");  // a Ukrainian interface: Ukrainian patterns for Cyrillic words
    Hyphenator::setPreferredLanguage("");
    EXPECT_EQ(offsets("міжнародний"), ukrainian);
  }
}

TEST(HyphenationLanguageFallback, ABookLanguageAndTheChosenLanguageAreNotOverridden) {
  Engine engine(Config{});
  FallbackLanguage ui("DE");
  const auto german = offsetsIn("de", "Quadratkilometer");
  const auto english = offsetsIn("en", "Quadratkilometer");
  const auto french = offsetsIn("fr", "Quadratkilometer");
  ASSERT_NE(german, english);

  // A book that names its language keeps it, whatever the interface language.
  Hyphenator::setPreferredLanguage("en-GB");
  EXPECT_EQ(offsets("Quadratkilometer"), english);
  Hyphenator::setPreferredLanguage("fra");
  EXPECT_EQ(offsets("Quadratkilometer"), french);
  // A language the book names and the reader has no patterns for: its words stay whole, as before.
  for (const char* tag : {"nl", "ja", "bg", "nld", "ko-KR"}) {
    Hyphenator::setPreferredLanguage(tag);
    EXPECT_TRUE(offsets("Quadratkilometer").empty()) << tag;
    EXPECT_TRUE(offsets("международный").empty()) << tag;
    EXPECT_FALSE(offsets("καλημέρα").empty()) << tag;  // Greek words break in every book
  }

  // The Hyphenation language setting is used for every word, with or without a book language.
  Config c;
  for (uint8_t i = 0; i < typoengine::LANGUAGE_COUNT; ++i) {
    if (std::string(typoengine::LANGUAGE_TAGS[i]) == "en") c.language = static_cast<uint8_t>(i + 1);
  }
  typoengine::set(typoengine::pack(c));
  Hyphenator::setPreferredLanguage("");
  EXPECT_EQ(offsets("Quadratkilometer"), english);
  EXPECT_TRUE(offsets("международный").empty());
}

TEST(TotalFit, AFailedSplitOrNoRoomLeavesTheWordsWholeForTheOtherBreaker) {
  Engine engine(Config{});
  Hyphenator::setPreferredLanguage("en");
  struct Restore {
    ~Restore() {
      ParsedText::failSplitForTest = 0;
      ParsedText::failAllocationForTest = false;
    }
  } restore;
  // 188 px: the total-fit breaker splits several words of kProse.
  const Fitted whole = fitted(kProse, 188);
  ASSERT_TRUE(whole.usedTotalFit);
  ASSERT_GE(hyphenatedLines(whole.total), 3);
  // The first, the second or the third prefix can not be stored: none of the chosen splits is made, so the other
  // breaker gets the paragraph as it was and gives exactly its own lines.
  for (const uint8_t failing : {1, 2, 3}) {
    ParsedText::failSplitForTest = failing;
    const Fitted f = fitted(kProse, 188);
    EXPECT_FALSE(f.usedTotalFit) << static_cast<int>(failing);
    EXPECT_EQ(wordsOf(f.total), wordsOf(whole.greedy)) << static_cast<int>(failing);
    EXPECT_EQ(xposOf(f.total), xposOf(whole.greedy)) << static_cast<int>(failing);
  }
  ParsedText::failSplitForTest = 0;
  // No room for the hyphenator's own allocations: the same.
  ParsedText::failAllocationForTest = true;
  const Fitted noRoom = fitted(kProse, 188);
  EXPECT_FALSE(noRoom.usedTotalFit);
  EXPECT_EQ(wordsOf(noRoom.total), wordsOf(whole.greedy));
}

TEST(HyphenationLanguageFallback, TheSectionCacheKeyFollowsTheInterfaceLanguageOnlyForABookWithNoLanguage) {
  FallbackLanguage ui("");
  const uint8_t english = Hyphenator::scriptFallbackSignature("");
  EXPECT_NE(english, 0);
  EXPECT_EQ(Hyphenator::scriptFallbackSignature("und"), english);
  // A book that names a language, with or without patterns: nothing follows the interface language.
  for (const char* tag : {"en", "el", "fr-FR", "nl", "ja"}) EXPECT_EQ(Hyphenator::scriptFallbackSignature(tag), 0);

  Hyphenator::setFallbackLanguage("DE");
  const uint8_t german = Hyphenator::scriptFallbackSignature("");
  EXPECT_NE(german, english);
  EXPECT_EQ(Hyphenator::scriptFallbackSignature("en"), 0);
  Hyphenator::setFallbackLanguage("UK");
  const uint8_t ukrainian = Hyphenator::scriptFallbackSignature("");
  EXPECT_NE(ukrainian, english);
  EXPECT_NE(ukrainian, german);
  // Interface languages that give the same patterns give the same key: no rebuild for nothing.
  for (const char* tag : {"NL", "EL", "RU", "EN"}) {
    Hyphenator::setFallbackLanguage(tag);
    EXPECT_EQ(Hyphenator::scriptFallbackSignature(""), english) << tag;
  }
}
