#include <Epub/ParsedText.h>
#include <Epub/ReaderRenderSpec.h>
#include <Epub/TypographyEngine.h>
#include <Epub/blocks/TextBlock.h>
#include <Epub/hyphenation/Hyphenator.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

using typoengine::Config;

// Sets the engine for one test and switches it off again after it.
struct Engine {
  explicit Engine(const Config& config) { typoengine::set(typoengine::pack(config)); }
  ~Engine() {
    typoengine::set(0);
    Hyphenator::setPreferredLanguage("");
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
  EXPECT_EQ(s.condensePercent, 50);
  EXPECT_EQ(s.letterSpacing, 2);
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
    const auto lines = layout(kThreeAndOne, 100, hyphenation);
    ASSERT_EQ(lines.size(), 2u);
    // 104 px on a 100 px line: each of the two gaps gives 2 px (4 -> 2), and the line ends at the edge.
    EXPECT_EQ(lines[0].words, (std::vector<std::string>{"aaaa", "bbbb", "cccc"}));
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 34, 68}));
    EXPECT_EQ(lines[0].right, 100);
    EXPECT_EQ(lines[1].xpos, (std::vector<int16_t>{0}));
  }
}

TEST(SpaceCondensing, AnOddOverrunRoundsToTheInsideOfThePage) {
  for (const bool hyphenation : {false, true}) {
    Engine engine(condense(50));
    const auto lines = layout(kThreeAndOne, 101, hyphenation);
    ASSERT_EQ(lines.size(), 2u);
    // 3 px over, two gaps: 2 px off each, so the line ends 1 px before the edge, not after it.
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 34, 68}));
    EXPECT_EQ(lines[0].right, 100);
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
          for (int cap = 0; cap <= 2; ++cap) {
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

// --- Letter spacing on loose lines: the positions ---

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
      // Unlocked: three gaps give 6 px, so the line fits 110 px.
      Engine engine(c);
      const auto lines = layout(words, 110, hyphenation);
      EXPECT_EQ(lines[0].words, (std::vector<std::string>{"—", "aaaa", "bbbb", "cccc"}));
      EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 10, 44, 78}));
      EXPECT_EQ(lines[0].right, 110);
    }
    c.lockedSpace = true;
    Engine engine(c);
    // Locked: only two gaps give, 4 px. 110 px is too narrow for the fourth word, 112 px is enough.
    EXPECT_EQ(layout(words, 110, hyphenation)[0].words, (std::vector<std::string>{"—", "aaaa", "bbbb"}));
    const auto lines = layout(words, 112, hyphenation);
    EXPECT_EQ(lines[0].words, (std::vector<std::string>{"—", "aaaa", "bbbb", "cccc"}));
    EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 12, 46, 80}));
    EXPECT_EQ(lines[0].right, 112);
  }
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
        Engine engine(c);
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
