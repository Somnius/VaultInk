#include <HiddenFlows.h>
#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace hiddenflows;

namespace {
// Five spine items of 100, 300, 200, 0 and 400 bytes: 1000 in all.
const std::vector<uint32_t> BOOK = {100, 400, 600, 600, 1000};
const std::vector<uint8_t> NONE = {};
}  // namespace

TEST(HiddenFlows, ItemSizes) {
  EXPECT_EQ(itemSize(BOOK, 0), 100u);
  EXPECT_EQ(itemSize(BOOK, 1), 300u);
  EXPECT_EQ(itemSize(BOOK, 3), 0u);
  EXPECT_EQ(itemSize(BOOK, 4), 400u);
  EXPECT_EQ(itemSize(BOOK, -1), 0u);
  EXPECT_EQ(itemSize(BOOK, 5), 0u);
  EXPECT_EQ(itemSize({100, 50}, 1), 0u);  // totals that go backwards
}

TEST(HiddenFlows, NothingHiddenIsTheRawBook) {
  EXPECT_FALSE(active(BOOK, NONE));
  EXPECT_EQ(visibleSize(BOOK, NONE), 1000u);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, NONE, 0, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, NONE, 1, 0.5f), 0.25f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, NONE, 4, 1.0f), 1.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, NONE, 5, 0.0f), 1.0f);  // one past the end, as the reader asks
  const Location at = locate(BOOK, NONE, 250);
  EXPECT_EQ(at.spineIndex, 1);
  EXPECT_FLOAT_EQ(at.fraction, 0.5f);
}

TEST(HiddenFlows, MiddleChapterHidden) {
  const std::vector<uint8_t> hidden = {0, 1, 0, 0, 0};
  EXPECT_TRUE(active(BOOK, hidden));
  EXPECT_EQ(visibleSize(BOOK, hidden), 700u);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 0, 0.5f), 50.0f / 700.0f);
  // Inside the hidden chapter the number stands still at the cut.
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 1, 0.0f), 100.0f / 700.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 1, 0.7f), 100.0f / 700.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 1, 1.0f), 100.0f / 700.0f);
  // The next chapter starts from the same point.
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 2, 0.0f), 100.0f / 700.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 2, 0.5f), 200.0f / 700.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 4, 1.0f), 1.0f);
}

TEST(HiddenFlows, LocateSkipsHiddenSpans) {
  const std::vector<uint8_t> hidden = {0, 1, 0, 0, 0};
  Location at = locate(BOOK, hidden, 50);
  EXPECT_EQ(at.spineIndex, 0);
  EXPECT_FLOAT_EQ(at.fraction, 0.5f);
  at = locate(BOOK, hidden, 100);  // the border belongs to the earlier chapter
  EXPECT_EQ(at.spineIndex, 0);
  EXPECT_FLOAT_EQ(at.fraction, 1.0f);
  at = locate(BOOK, hidden, 101);  // over the cut: chapter 2, never the hidden chapter 1
  EXPECT_EQ(at.spineIndex, 2);
  EXPECT_NEAR(at.fraction, 1.0f / 200.0f, 1e-6f);
  at = locate(BOOK, hidden, 500);  // the empty item 3 is passed over
  EXPECT_EQ(at.spineIndex, 4);
  EXPECT_FLOAT_EQ(at.fraction, 0.5f);
  at = locate(BOOK, hidden, 700);
  EXPECT_EQ(at.spineIndex, 4);
  EXPECT_FLOAT_EQ(at.fraction, 1.0f);
  at = locate(BOOK, hidden, 5000);  // past the end
  EXPECT_EQ(at.spineIndex, 4);
  EXPECT_FLOAT_EQ(at.fraction, 1.0f);
}

TEST(HiddenFlows, FirstChapterHidden) {
  const std::vector<uint8_t> hidden = {1};  // shorter than the spine: the rest is shown
  EXPECT_TRUE(active(BOOK, hidden));
  EXPECT_EQ(visibleSize(BOOK, hidden), 900u);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 0, 0.9f), 0.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 1, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 1, 1.0f), 300.0f / 900.0f);
  const Location start = locate(BOOK, hidden, 0);
  EXPECT_EQ(start.spineIndex, 1);
  EXPECT_FLOAT_EQ(start.fraction, 0.0f);
}

TEST(HiddenFlows, LastChapterHidden) {
  const std::vector<uint8_t> hidden = {0, 0, 0, 0, 1};
  EXPECT_EQ(visibleSize(BOOK, hidden), 600u);
  // In the hidden last chapter the book reads as finished.
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 4, 0.0f), 1.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 4, 0.5f), 1.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 5, 0.0f), 1.0f);
  const Location end = locate(BOOK, hidden, 600);
  EXPECT_EQ(end.spineIndex, 2);
  EXPECT_FLOAT_EQ(end.fraction, 1.0f);
  const Location past = locateFraction(BOOK, hidden, 1.0f);
  EXPECT_EQ(past.spineIndex, 2);
  EXPECT_FLOAT_EQ(past.fraction, 1.0f);
}

TEST(HiddenFlows, EverythingHiddenFallsBackToRaw) {
  const std::vector<uint8_t> hidden = {1, 1, 1, 1, 1};
  EXPECT_FALSE(active(BOOK, hidden));
  EXPECT_EQ(visibleSize(BOOK, hidden), 1000u);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 1, 0.5f), 0.25f);
  const Location at = locate(BOOK, hidden, 250);
  EXPECT_EQ(at.spineIndex, 1);
  EXPECT_FLOAT_EQ(at.fraction, 0.5f);
}

TEST(HiddenFlows, OnlyEmptyItemsHiddenChangesNothing) {
  const std::vector<uint8_t> hidden = {0, 0, 0, 1, 0};
  EXPECT_FALSE(active(BOOK, hidden));
  EXPECT_EQ(visibleSize(BOOK, hidden), 1000u);
}

TEST(HiddenFlows, OnlyEmptyItemsShownFallsBackToRaw) {
  const std::vector<uint8_t> hidden = {1, 1, 1, 0, 1};
  EXPECT_FALSE(active(BOOK, hidden));
  EXPECT_EQ(visibleSize(BOOK, hidden), 1000u);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 4, 0.5f), 0.8f);
}

TEST(HiddenFlows, EmptyBook) {
  const std::vector<uint32_t> empty;
  EXPECT_FALSE(active(empty, NONE));
  EXPECT_EQ(visibleSize(empty, NONE), 0u);
  EXPECT_FLOAT_EQ(visibleFraction(empty, NONE, 0, 0.5f), 0.0f);
  EXPECT_EQ(locate(empty, NONE, 10).spineIndex, 0);
  const std::vector<uint32_t> zeros = {0, 0};
  EXPECT_FLOAT_EQ(visibleFraction(zeros, {1, 0}, 1, 0.5f), 0.0f);
  EXPECT_EQ(locateFraction(zeros, {1, 0}, 0.5f).fraction, 0.0f);
}

TEST(HiddenFlows, BadInputIsClamped) {
  const std::vector<uint8_t> hidden = {0, 1, 0, 0, 0};
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, -1, 0.5f), 0.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 99, 0.5f), 1.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 2, 7.0f), 300.0f / 700.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 2, -3.0f), 100.0f / 700.0f);
  EXPECT_FLOAT_EQ(visibleFraction(BOOK, hidden, 2, std::nanf("")), 100.0f / 700.0f);
  EXPECT_EQ(locateFraction(BOOK, hidden, -1.0f).spineIndex, 0);
  EXPECT_EQ(locateFraction(BOOK, hidden, 9.0f).spineIndex, 4);
}

TEST(HiddenFlows, RoundTripsThroughEveryVisibleByte) {
  const std::vector<std::vector<uint8_t>> cases = {
      {}, {1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 0, 0, 1}, {1, 0, 1, 0, 0}, {0, 1, 1, 1, 0}, {1, 1, 1, 1, 1}};
  for (const auto& hidden : cases) {
    const uint32_t total = visibleSize(BOOK, hidden);
    for (uint32_t bytes = 0; bytes <= total; bytes += 7) {
      const Location at = locate(BOOK, hidden, bytes);
      if (active(BOOK, hidden)) {
        EXPECT_FALSE(isHidden(hidden, at.spineIndex));
      }
      EXPECT_EQ(visibleBytesAt(BOOK, hidden, at.spineIndex, at.fraction), bytes);
      EXPECT_NEAR(visibleFraction(BOOK, hidden, at.spineIndex, at.fraction), static_cast<float>(bytes) / total, 1e-5f);
    }
  }
}

TEST(HiddenFlows, FractionNeverGoesBackwards) {
  const std::vector<uint8_t> hidden = {0, 1, 0, 0, 1};
  float last = 0.0f;
  for (int spine = 0; spine < 5; spine++) {
    for (int step = 0; step <= 10; step++) {
      const float now = visibleFraction(BOOK, hidden, spine, static_cast<float>(step) / 10.0f);
      EXPECT_GE(now, last);
      last = now;
    }
  }
  EXPECT_FLOAT_EQ(last, 1.0f);
}

TEST(HiddenFlows, LargeBookDoesNotOverflow) {
  const std::vector<uint32_t> big = {2000000000u, 4000000000u};
  const std::vector<uint8_t> hidden = {1, 0};
  EXPECT_EQ(visibleSize(big, hidden), 2000000000u);
  EXPECT_FLOAT_EQ(visibleFraction(big, hidden, 1, 0.5f), 0.5f);
  const Location at = locateFraction(big, hidden, 0.5f);
  EXPECT_EQ(at.spineIndex, 1);
  EXPECT_NEAR(at.fraction, 0.5f, 1e-6f);
}

TEST(HiddenFlows, ChapterRange) {
  // Contents entries -> spine items; entry 2 is a sub-heading inside item 3, entry 4 has no item.
  const std::vector<int> toc = {1, 3, 3, 6, -1, 2};
  const auto spineOf = [&toc](int i) { return toc[i]; };
  int first = -1, last = -1;
  ASSERT_TRUE(chapterRange(0, 6, 10, spineOf, first, last));
  EXPECT_EQ(first, 1);
  EXPECT_EQ(last, 2);  // items without an entry of their own belong to the chapter before them
  ASSERT_TRUE(chapterRange(1, 6, 10, spineOf, first, last));
  EXPECT_EQ(first, 3);
  EXPECT_EQ(last, 5);
  ASSERT_TRUE(chapterRange(2, 6, 10, spineOf, first, last));  // the sub-heading stands for the same items
  EXPECT_EQ(first, 3);
  EXPECT_EQ(last, 5);
  ASSERT_TRUE(chapterRange(3, 6, 10, spineOf, first, last));  // the next entry points backwards
  EXPECT_EQ(first, 6);
  EXPECT_EQ(last, 6);
  EXPECT_FALSE(chapterRange(4, 6, 10, spineOf, first, last));
  ASSERT_TRUE(chapterRange(5, 6, 10, spineOf, first, last));  // the last entry runs to the end of the book
  EXPECT_EQ(first, 2);
  EXPECT_EQ(last, 9);
  EXPECT_FALSE(chapterRange(-1, 6, 10, spineOf, first, last));
  EXPECT_FALSE(chapterRange(6, 6, 10, spineOf, first, last));
  EXPECT_FALSE(chapterRange(3, 6, 5, spineOf, first, last));  // a spine item that is not there
}

TEST(HiddenFlows, FileRoundTrip) {
  const std::vector<std::string> hrefs = {"OEBPS/notes.xhtml", "OEBPS/Text/\xCE\xB1.xhtml", "x"};
  const std::string bytes = encode(hrefs);
  EXPECT_EQ(static_cast<uint8_t>(bytes[0]), FILE_VERSION);
  std::vector<std::string> back;
  ASSERT_TRUE(decode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), back));
  EXPECT_EQ(back, hrefs);
}

TEST(HiddenFlows, FileEmptyList) {
  const std::string bytes = encode({});
  EXPECT_EQ(bytes.size(), 3u);
  std::vector<std::string> back = {"old"};
  EXPECT_TRUE(decode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), back));
  EXPECT_TRUE(back.empty());
}

TEST(HiddenFlows, FileSkipsBadHrefs) {
  const std::string bytes = encode({"", std::string(MAX_HREF_LENGTH + 1, 'a'), "ok.xhtml"});
  std::vector<std::string> back;
  ASSERT_TRUE(decode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), back));
  ASSERT_EQ(back.size(), 1u);
  EXPECT_EQ(back[0], "ok.xhtml");
}

TEST(HiddenFlows, FileRejectsDamage) {
  const std::string good = encode({"a.xhtml", "b.xhtml"});
  const auto* data = reinterpret_cast<const uint8_t*>(good.data());
  std::vector<std::string> back;
  EXPECT_FALSE(decode(nullptr, 0, back));
  EXPECT_FALSE(decode(data, 2, back));
  for (size_t cut = 3; cut < good.size(); cut++) {
    EXPECT_FALSE(decode(data, cut, back)) << cut;
    EXPECT_TRUE(back.empty());
  }
  std::string other = good;
  other[0] = static_cast<char>(FILE_VERSION + 1);
  EXPECT_FALSE(decode(reinterpret_cast<const uint8_t*>(other.data()), other.size(), back));
  std::string zeroLength = good;
  zeroLength[3] = 0;
  zeroLength[4] = 0;
  EXPECT_FALSE(decode(reinterpret_cast<const uint8_t*>(zeroLength.data()), zeroLength.size(), back));
  std::string manyHrefs = good;
  manyHrefs[1] = static_cast<char>(0xFF);
  manyHrefs[2] = static_cast<char>(0xFF);
  EXPECT_FALSE(decode(reinterpret_cast<const uint8_t*>(manyHrefs.data()), manyHrefs.size(), back));
}
