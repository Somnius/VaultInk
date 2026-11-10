#include <BookMap.h>
#include <HiddenFlows.h>
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace bookmap;

namespace {
// Five spine items of 100, 300, 200, 0 and 400 bytes: 1000 in all.
const std::vector<uint32_t> BOOK = {100, 400, 600, 600, 1000};
const std::vector<uint8_t> NONE = {};

std::vector<uint32_t> evenBook(const int items, const uint32_t each) {
  std::vector<uint32_t> cumulative;
  for (int i = 1; i <= items; i++) cumulative.push_back(each * static_cast<uint32_t>(i));
  return cumulative;
}

// The segments cover the strip exactly, in order, with no gaps.
void expectPartition(const Layout& l) {
  int at = l.x;
  for (const Segment& s : l.segments) {
    EXPECT_EQ(s.x, at);
    EXPECT_GT(s.width, 0);
    at += s.width;
  }
  EXPECT_EQ(at, l.x + l.width);
}

// The groups cover every spine item once, in order.
void expectSpinePartition(const std::vector<Group>& groups, const int spineCount) {
  int next = 0;
  for (const Group& g : groups) {
    EXPECT_EQ(g.firstSpine, next);
    EXPECT_GE(g.lastSpine, g.firstSpine);
    next = g.lastSpine + 1;
  }
  EXPECT_EQ(next, spineCount);
}
}  // namespace

TEST(BookMapGroups, NoContentsIsOneGroupPerSpineItem) {
  const auto groups = groupChapters(BOOK, NONE, {});
  ASSERT_EQ(groups.size(), 5u);
  expectSpinePartition(groups, 5);
  EXPECT_EQ(groups[1].start, 100u);
  EXPECT_EQ(groups[1].bytes, 300u);
  EXPECT_EQ(groups[3].bytes, 0u);  // a zero-size item
  EXPECT_FALSE(groups[3].hidden);
  EXPECT_EQ(groups[4].start, 600u);
  EXPECT_EQ(groups[0].tocIndex, -1);
}

TEST(BookMapGroups, EmptyBook) {
  EXPECT_TRUE(groupChapters({}, NONE, {0, 1}).empty());
  EXPECT_TRUE(layout({}, 10, 400).segments.empty());
  EXPECT_EQ(segmentAt(layout({}, 10, 400), 50), -1);
  EXPECT_FLOAT_EQ(fractionAt(layout({}, 10, 400), 50), 0.0f);
}

TEST(BookMapGroups, OneChapterHoldsTheWholeBook) {
  const auto groups = groupChapters(evenBook(500, 10), NONE, {0});
  ASSERT_EQ(groups.size(), 1u);
  EXPECT_EQ(groups[0].firstSpine, 0);
  EXPECT_EQ(groups[0].lastSpine, 499);
  EXPECT_EQ(groups[0].bytes, 5000u);
  EXPECT_EQ(groups[0].tocIndex, 0);
  const Layout l = layout(groups, 16, 448);
  ASSERT_EQ(l.segments.size(), 1u);
  EXPECT_EQ(l.segments[0].width, 448);
}

TEST(BookMapGroups, ContentsEntriesBecomeChapters) {
  // Entries at items 1 and 2 (two entries share item 2), one without an item, items 0 before the first entry.
  const auto groups = groupChapters(BOOK, NONE, {1, 2, 2, -1});
  ASSERT_EQ(groups.size(), 3u);
  expectSpinePartition(groups, 5);
  EXPECT_EQ(groups[0].tocIndex, -1);  // front matter
  EXPECT_EQ(groups[0].lastSpine, 0);
  EXPECT_EQ(groups[1].tocIndex, 0);
  EXPECT_EQ(groups[1].bytes, 300u);
  EXPECT_EQ(groups[2].tocIndex, 1);
  EXPECT_EQ(groups[2].lastSpine, 4);
  EXPECT_EQ(groups[2].bytes, 600u);
}

TEST(BookMapGroups, EntriesThatPointBackwardsAreSkipped) {
  const auto groups = groupChapters(BOOK, NONE, {0, 3, 1, 4});
  expectSpinePartition(groups, 5);
  ASSERT_EQ(groups.size(), 3u);
  EXPECT_EQ(groups[0].lastSpine, 2);
  EXPECT_EQ(groups[1].tocIndex, 1);
  EXPECT_EQ(groups[1].firstSpine, 3);
  EXPECT_EQ(groups[2].tocIndex, 3);
}

TEST(BookMapGroups, HiddenChaptersCountNothing) {
  const std::vector<uint8_t> hidden = {1, 0, 0, 0, 1};  // the first and the last
  const auto groups = groupChapters(BOOK, hidden, {});
  ASSERT_EQ(groups.size(), 5u);
  EXPECT_TRUE(groups[0].hidden);
  EXPECT_EQ(groups[0].bytes, 0u);
  EXPECT_EQ(groups[1].start, 0u);
  EXPECT_EQ(groups[1].bytes, 300u);
  EXPECT_TRUE(groups[4].hidden);
  EXPECT_EQ(groups[4].start, 500u);
  // The same numbers as the percent.
  EXPECT_EQ(hiddenflows::visibleSize(BOOK, hidden), 500u);
  EXPECT_EQ(groups[2].start, hiddenflows::visibleBytesAt(BOOK, hidden, 2, 0.0f));
}

TEST(BookMapGroups, AChapterWithOnlySomeItemsHiddenIsShown) {
  const auto groups = groupChapters(BOOK, {0, 1, 0, 0, 0}, {0, 2});
  ASSERT_EQ(groups.size(), 2u);
  EXPECT_FALSE(groups[0].hidden);
  EXPECT_EQ(groups[0].bytes, 100u);
  EXPECT_EQ(groups[1].start, 100u);
}

TEST(BookMapGroups, EverythingHiddenIsTheRawBook) {
  const auto groups = groupChapters(BOOK, {1, 1, 1, 1, 1}, {});
  ASSERT_EQ(groups.size(), 5u);
  for (const Group& g : groups) EXPECT_FALSE(g.hidden);
  EXPECT_EQ(groups[4].start + groups[4].bytes, 1000u);
}

TEST(BookMapGroups, FiveHundredSpineItemsAreMergedToFit) {
  auto groups = groupChapters(evenBook(500, 100), NONE, {});
  ASSERT_EQ(groups.size(), 500u);
  const size_t room = maxGroupsFor(448);
  EXPECT_EQ(room, 74u);
  groups = mergeGroups(groups, room);
  EXPECT_EQ(groups.size(), room);
  expectSpinePartition(groups, 500);
  uint32_t start = 0;
  for (const Group& g : groups) {
    EXPECT_EQ(g.start, start);
    start += g.bytes;
  }
  EXPECT_EQ(start, 50000u);
  const Layout l = layout(groups, 16, 448);
  expectPartition(l);
  for (const Segment& s : l.segments) EXPECT_GE(s.width, MIN_WIDTH);
}

TEST(BookMapGroups, MergedGroupsAreHiddenOnlyWhenAllAre) {
  std::vector<uint8_t> hidden(8, 0);
  hidden[0] = hidden[1] = hidden[2] = 1;
  const auto merged = mergeGroups(groupChapters(evenBook(8, 10), hidden, {}), 4);
  ASSERT_EQ(merged.size(), 4u);
  EXPECT_TRUE(merged[0].hidden);
  EXPECT_FALSE(merged[1].hidden);  // item 2 hidden, item 3 shown
  EXPECT_EQ(merged[1].bytes, 10u);
  EXPECT_EQ(mergeGroups(merged, 10).size(), 4u);
}

TEST(BookMapLayout, ProportionalWidths) {
  const Layout l = layout(groupChapters({100, 400, 1000}, NONE, {}), 0, 100);
  expectPartition(l);
  ASSERT_EQ(l.segments.size(), 3u);
  EXPECT_EQ(l.segments[0].width, 10);
  EXPECT_EQ(l.segments[1].width, 30);
  EXPECT_EQ(l.segments[2].width, 60);
  EXPECT_EQ(l.total, 1000u);
}

TEST(BookMapLayout, SmallChaptersGetTheMinimumFromTheLargeOnes) {
  // 1, 1 and 998 bytes over 100 px: the small ones would be a tenth of a pixel.
  const Layout l = layout(groupChapters({1, 2, 1000}, NONE, {}), 20, 100);
  expectPartition(l);
  EXPECT_EQ(l.segments[0].width, MIN_WIDTH);
  EXPECT_EQ(l.segments[1].width, MIN_WIDTH);
  EXPECT_EQ(l.segments[2].width, 100 - 2 * MIN_WIDTH);
}

TEST(BookMapLayout, ZeroSizeItemsGetTheMinimum) {
  const Layout l = layout(groupChapters(BOOK, NONE, {}), 0, 203);
  expectPartition(l);
  EXPECT_EQ(l.segments[3].width, MIN_WIDTH);
  EXPECT_EQ(l.segments[0].width, 20);
  EXPECT_EQ(l.segments[4].width, 80);
}

TEST(BookMapLayout, HiddenChaptersAreFixedSliversOutsideTheScale) {
  const std::vector<uint8_t> hidden = {1, 0, 0, 0, 1};
  // 300 and 200 bytes shown, a zero-size item, two slivers: 112 = 2 * 6 + 3 + 97.
  const Layout l = layout(groupChapters(BOOK, hidden, {}), 0, 112);
  expectPartition(l);
  EXPECT_TRUE(l.segments[0].hidden);
  EXPECT_EQ(l.segments[0].width, HIDDEN_WIDTH);
  EXPECT_EQ(l.segments[4].width, HIDDEN_WIDTH);
  EXPECT_EQ(l.segments[3].width, MIN_WIDTH);
  EXPECT_EQ(l.segments[1].width + l.segments[2].width, 97);
  EXPECT_NEAR(l.segments[1].width, 58, 1);
  EXPECT_EQ(l.total, 500u);
}

TEST(BookMapLayout, AllHiddenGroupsShareTheStrip) {
  std::vector<Group> groups(4);
  for (Group& g : groups) g.hidden = true;
  const Layout l = layout(groups, 0, 102);
  expectPartition(l);
  for (const Segment& s : l.segments) EXPECT_FALSE(s.hidden);
  EXPECT_FLOAT_EQ(fractionAt(l, 50), 0.0f);
  EXPECT_EQ(xAt(l, 0.5f), 0);
}

TEST(BookMapLayout, MoreGroupsThanPixelsStillFillsTheStrip) {
  const Layout l = layout(groupChapters(evenBook(100, 10), NONE, {}), 0, 150);
  expectPartition(l);
  ASSERT_EQ(l.segments.size(), 100u);
}

TEST(BookMapLayout, FractionAndPixelRoundTrip) {
  const std::vector<uint8_t> hidden = {0, 1, 0, 0, 0};
  for (const auto& flags : {NONE, hidden}) {
    const Layout l = layout(groupChapters(BOOK, flags, {}), 16, 448);
    expectPartition(l);
    float before = -1.0f;
    for (const Segment& s : l.segments) {
      if (s.hidden || s.bytes == 0) continue;
      for (int x = s.x; x < s.x + s.width; x++) {
        if (segmentAt(l, x) != segmentContaining(l, x)) continue;  // a pixel a thin neighbour takes
        const float f = fractionAt(l, x);
        EXPECT_GT(f, before);
        before = f;
        EXPECT_EQ(xAt(l, f), x);
        // Strictly inside the chapter, so the jump lands in it and not at the end of the one before.
        const auto at = hiddenflows::locateFraction(BOOK, flags, f);
        EXPECT_GE(at.spineIndex, s.firstSpine);
        EXPECT_LE(at.spineIndex, s.lastSpine);
      }
    }
  }
}

TEST(BookMapLayout, EndsOfTheStrip) {
  const Layout l = layout(groupChapters(BOOK, NONE, {}), 16, 448);
  EXPECT_EQ(xAt(l, 0.0f), 16);
  EXPECT_EQ(xAt(l, 1.0f), 16 + 448 - 1);
  EXPECT_EQ(xAt(l, 7.0f), 16 + 448 - 1);
  EXPECT_EQ(xAt(l, -1.0f), 16);
  EXPECT_EQ(segmentContaining(l, -50), 0);
  EXPECT_EQ(segmentContaining(l, 5000), 4);
  EXPECT_LT(fractionAt(l, -50), 0.01f);
  EXPECT_GT(fractionAt(l, 5000), 0.99f);
}

TEST(BookMapLayout, LastChapterHiddenEndsOnTheLastShownOne) {
  const Layout l = layout(groupChapters(BOOK, {0, 0, 0, 0, 1}, {}), 0, 200);
  ASSERT_TRUE(l.segments[4].hidden);
  EXPECT_EQ(xAt(l, 1.0f), l.segments[4].x - MIN_WIDTH - 1);  // the end of item 2, before the empty item 3
  EXPECT_FLOAT_EQ(fractionAt(l, 199), 1.0f);                 // on the sliver: the border
  EXPECT_EQ(xAtPlace(l, 4, 1.0f), l.segments[4].x + HIDDEN_WIDTH / 2);
}

TEST(BookMapLayout, FirstChapterHidden) {
  const Layout l = layout(groupChapters(BOOK, {1, 0, 0, 0, 0}, {}), 0, 200);
  ASSERT_TRUE(l.segments[0].hidden);
  EXPECT_EQ(segmentAt(l, 0), 0);
  EXPECT_FLOAT_EQ(fractionAt(l, 0), 0.0f);
  EXPECT_EQ(xAt(l, 0.0f), HIDDEN_WIDTH);
  EXPECT_EQ(xAtPlace(l, 0, 0.0f), HIDDEN_WIDTH / 2);
  EXPECT_EQ(xAtPlace(l, 1, 0.0f), HIDDEN_WIDTH);
}

TEST(BookMapHit, AThinSegmentIsHitFromNearby) {
  // 49500, 200 and 50300 bytes over 400 px: the middle one is 3 px wide.
  const Layout l = layout(groupChapters({49500, 49700, 100000}, NONE, {}), 0, 400);
  ASSERT_EQ(l.segments[1].width, MIN_WIDTH);
  const int centre = l.segments[1].x + 1;
  EXPECT_EQ(segmentContaining(l, centre - 8), 0);
  EXPECT_EQ(segmentAt(l, centre - 8), 1);
  EXPECT_EQ(segmentAt(l, centre + 12), 1);
  EXPECT_EQ(segmentAt(l, centre + 13), 2);
  EXPECT_EQ(segmentAt(l, centre - 13), 0);
  EXPECT_EQ(segmentAt(l, 0), 0);
  // The jump from a nearby pixel lands inside the thin chapter.
  const auto at = hiddenflows::locateFraction({49500, 49700, 100000}, NONE, fractionAt(l, centre - 8));
  EXPECT_EQ(at.spineIndex, 1);
}

TEST(BookMapHit, TheNearestOfTwoThinSegmentsWins) {
  const Layout l = layout(groupChapters({500, 501, 502, 1000}, NONE, {}), 0, 400);
  ASSERT_EQ(l.segments[1].width, MIN_WIDTH);
  ASSERT_EQ(l.segments[2].width, MIN_WIDTH);
  EXPECT_EQ(segmentAt(l, l.segments[1].x - 5), 1);
  EXPECT_EQ(segmentAt(l, l.segments[2].x + MIN_WIDTH + 5), 2);
  EXPECT_EQ(segmentAt(l, l.segments[1].x), 1);
  EXPECT_EQ(segmentAt(l, l.segments[2].x + 1), 2);
}

TEST(BookMapHit, TicksArePlacedAndBeatSegments) {
  const Layout l = layout(groupChapters(BOOK, {0, 1, 0, 0, 0}, {}), 0, 400);
  std::vector<Tick> ticks(3);
  ticks[0].fraction = 0.5f;
  ticks[0].spineIndex = 2;
  ticks[1].fraction = hiddenflows::visibleFraction(BOOK, {0, 1, 0, 0, 0}, 1, 0.5f);  // in the hidden chapter
  ticks[1].spineIndex = 1;
  ticks[2].fraction = 0.52f;
  ticks[2].spineIndex = 2;
  placeTicks(l, ticks);
  EXPECT_EQ(ticks[0].x, xAt(l, 0.5f));
  EXPECT_EQ(ticks[1].x, l.segments[1].x + HIDDEN_WIDTH / 2);
  EXPECT_GT(ticks[2].x, ticks[0].x);

  EXPECT_EQ(nearestTick(ticks, ticks[0].x), 0);
  EXPECT_EQ(nearestTick(ticks, ticks[0].x - TICK_TOLERANCE), 0);
  EXPECT_EQ(nearestTick(ticks, ticks[0].x - TICK_TOLERANCE - 1), -1);
  EXPECT_EQ(nearestTick(ticks, ticks[2].x + 1), 2);
  EXPECT_EQ(nearestTick({}, 100), -1);

  const Hit onTick = hitTest(l, ticks, ticks[0].x + 2);
  EXPECT_EQ(onTick.kind, Hit::Kind::Tick);
  EXPECT_EQ(onTick.index, 0);
  EXPECT_FLOAT_EQ(onTick.fraction, 0.5f);
  const Hit onSegment = hitTest(l, ticks, 399);
  EXPECT_EQ(onSegment.kind, Hit::Kind::Segment);
  EXPECT_EQ(onSegment.index, 4);
  EXPECT_FLOAT_EQ(onSegment.fraction, fractionAt(l, 399));
  EXPECT_EQ(hitTest(Layout{}, {}, 5).kind, Hit::Kind::None);
}

TEST(BookMapHit, TwoTicksAtTheSamePlaceGiveTheFirst) {
  std::vector<Tick> ticks(2);
  ticks[0].x = ticks[1].x = 50;
  EXPECT_EQ(nearestTick(ticks, 52), 0);
}

TEST(BookMapPages, PageNumbers) {
  EXPECT_EQ(pageAt(0.5f, 0.0f, 0.0f), 0);    // no scale
  EXPECT_EQ(pageAt(0.0f, 0.0f, 200.0f), 1);  // never below 1
  EXPECT_EQ(pageAt(0.5f, 0.0f, 200.0f), 100);
  EXPECT_EQ(pageAt(1.0f, 0.1f, 200.0f), 180);
  EXPECT_EQ(pageAt(0.05f, 0.1f, 200.0f), 1);  // before chapter 1
  // A chapter that starts after 100 pages starts on page 101; the first chapter on page 1.
  EXPECT_EQ(chapterStartPage(0.5f, 0.0f, 200.0f), 101);
  EXPECT_EQ(chapterStartPage(0.0f, 0.0f, 200.0f), 1);
  EXPECT_EQ(chapterStartPage(0.05f, 0.1f, 200.0f), 1);
  EXPECT_EQ(chapterStartPage(0.5f, 0.0f, 0.0f), 0);
}
