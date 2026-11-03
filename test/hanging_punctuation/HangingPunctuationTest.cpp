#include <Epub/ParsedText.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <TextFeatures.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> xpos;
};

// Turns HANGING on for one test and restores the defaults after it.
struct HangingOn {
  explicit HangingOn(const bool on) {
    textfeatures::set(on ? textfeatures::DEFAULTS | textfeatures::HANGING : textfeatures::DEFAULTS);
  }
  ~HangingOn() { textfeatures::set(textfeatures::DEFAULTS); }
};

// Fixture metrics (stub renderer): every glyph is 8 px wide (unless glyphAdvance says otherwise), a space is 4 px.
std::vector<Line> layout(const std::vector<const char*>& words, const uint16_t width,
                         const CssTextAlign alignment = CssTextAlign::Justify, const int glyphAdvance = 8) {
  GfxRenderer renderer;
  renderer.glyphAdvance = glyphAdvance;
  BlockStyle style;
  style.alignment = alignment;
  style.textIndentDefined = true;
  ParsedText text(false, false, style, 0);
  for (const char* word : words) text.addWord(word, EpdFontFamily::REGULAR);
  std::vector<Line> lines;
  text.layoutAndExtractLines(renderer, 0, width, [&](std::unique_ptr<TextBlock> block, auto) {
    auto& line = lines.emplace_back();
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      line.words.emplace_back(block->wordText(i));
      line.xpos.push_back(block->wordXpos(i));
    }
  });
  return lines;
}

// "\"Hello" (48 px) + space (4) + "world." (48) = 100 px: fills a 100 px line exactly; the long word wraps.
const std::vector<const char*> kQuoted{"\"Hello", "world.", "x"};

}  // namespace

TEST(HangingPunctuation, FlagOffKeepsThePositions) {
  HangingOn flag(false);
  const auto lines = layout(kQuoted, 100);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].words, (std::vector<std::string>{"\"Hello", "world."}));
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 52}));
}

TEST(HangingPunctuation, JustifiedLineHangsQuoteLeftAndPeriodRight) {
  HangingOn flag(true);
  const auto lines = layout(kQuoted, 100);
  ASSERT_EQ(lines.size(), 2u);
  // Same line breaks as with the flag off.
  EXPECT_EQ(lines[0].words, (std::vector<std::string>{"\"Hello", "world."}));
  // The quote (8 px) hangs left: the H sits at x = 0. The period hangs right: the line is
  // stretched to 100 + 8, so "world." (48 px) ends at 108.
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{-8, 60}));
  EXPECT_EQ(lines[0].xpos[1] + 48, 108);
  // The last line of the paragraph is not stretched.
  EXPECT_EQ(lines[1].xpos, (std::vector<int16_t>{0}));
}

TEST(HangingPunctuation, LeftAlignedLineHangsOnlyTheQuote) {
  HangingOn flag(true);
  const auto lines = layout(kQuoted, 100, CssTextAlign::Left);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{-8, 44}));
}

TEST(HangingPunctuation, CenteredLineDoesNotHang) {
  HangingOn flag(true);
  const auto lines = layout(kQuoted, 100, CssTextAlign::Center);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{0, 52}));
}

TEST(HangingPunctuation, LineWithoutPunctuationIsUnchanged) {
  // "Hello" (40) + space (4) + "world" (40) = 84 px; justified to 100 the gap grows to 20.
  const std::vector<const char*> words{"Hello", "world", "xxxxxxxxxxxx"};
  std::vector<int16_t> off;
  std::vector<int16_t> on;
  {
    HangingOn flag(false);
    off = layout(words, 100)[0].xpos;
  }
  {
    HangingOn flag(true);
    on = layout(words, 100)[0].xpos;
  }
  EXPECT_EQ(off, (std::vector<int16_t>{0, 60}));
  EXPECT_EQ(on, off);
}

TEST(HangingPunctuation, GreekGuillemetAndAnoTeleiaHang) {
  // "«Γεια" (40 px) + space + "σου·" (32 px) = 76 px on a 100 px line; the long word wraps.
  const std::vector<const char*> words{"«Γεια", "σου·", "xxxxxxxxxxxx"};
  {
    HangingOn flag(false);
    EXPECT_EQ(layout(words, 100)[0].xpos, (std::vector<int16_t>{0, 68}));
  }
  HangingOn flag(true);
  const auto lines = layout(words, 100);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{-8, 76}));
  EXPECT_EQ(lines[0].xpos[1] + 32, 108);
}

TEST(HangingPunctuation, HangIsCappedAtTwelvePixels) {
  // 16 px glyphs: "\"Hello" (96) + space (4) + "world." (96) = 196 px on a 200 px line.
  HangingOn flag(true);
  const auto lines = layout(kQuoted, 200, CssTextAlign::Justify, 16);
  ASSERT_EQ(lines.size(), 2u);
  // A 16 px quote hangs only 12 px, and the period likewise: the line ends at 200 + 12.
  EXPECT_EQ(lines[0].xpos, (std::vector<int16_t>{-12, 116}));
  EXPECT_EQ(lines[0].xpos[1] + 96, 212);
}
