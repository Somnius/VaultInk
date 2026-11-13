#include <ScrollView.h>
#include <gtest/gtest.h>

#include <vector>

using namespace scrollview;

namespace {

// A chapter of pages; a page is `lines` rows of 20 px, one under the other.
struct Chapter {
  std::vector<std::vector<Row>> pages;

  void addPage(const int lines, const int lineHeight = 20, const int firstTop = 0) {
    std::vector<Row> rows;
    for (int i = 0; i < lines; ++i) {
      rows.push_back({static_cast<int16_t>(firstTop + i * lineHeight), static_cast<int16_t>(lineHeight)});
    }
    pages.push_back(rows);
  }

  auto rowsOf() const {
    return [this](const int page) -> const std::vector<Row>* {
      if (page < 0 || page >= static_cast<int>(pages.size())) return nullptr;
      return &pages[static_cast<size_t>(page)];
    };
  }
};

// Three full pages of ten 20 px lines: a 200 px screen.
Chapter threePages() {
  Chapter c;
  c.addPage(10);
  c.addPage(10);
  c.addPage(10);
  return c;
}

int rowsShown(const View& view) {
  int n = 0;
  for (const Segment& s : view.segments) n += s.rowCount;
  return n;
}

}  // namespace

TEST(ScrollViewRows, ItemsOnOneLineJoinARow) {
  // Two table cells 3 px apart, then a line, then an image.
  const auto rows = buildRows({{40, 20}, {0, 20}, {3, 22}, {60, 150}}, 6);
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0].top, 0);
  EXPECT_EQ(rows[0].height, 25);  // down to the taller cell's end
  EXPECT_EQ(rows[1].top, 40);
  EXPECT_EQ(rows[2].height, 150);
  EXPECT_EQ(contentHeight(rows), 210);
  EXPECT_EQ(rowPitch(rows, 0), 40);
  EXPECT_EQ(rowPitch(rows, 2), 150);  // the last row: its own height
  EXPECT_EQ(rowPitch(rows, 7), 0);
}

TEST(ScrollViewRows, RowAtFindsTheLineOfAY) {
  const auto rows = buildRows({{0, 20}, {20, 20}, {40, 20}}, 4);
  EXPECT_EQ(rowAt(rows, -5), 0);
  EXPECT_EQ(rowAt(rows, 0), 0);
  EXPECT_EQ(rowAt(rows, 19), 0);
  EXPECT_EQ(rowAt(rows, 20), 1);
  EXPECT_EQ(rowAt(rows, 500), 2);
  EXPECT_EQ(rowAt({}, 10), 0);
  EXPECT_EQ(contentHeight({}), 0);
}

TEST(ScrollViewCompose, APageTopIsThePageItself) {
  const auto c = threePages();
  const View view = compose({0, 0}, 200, c.rowsOf());
  ASSERT_EQ(view.segments.size(), 1u);
  EXPECT_EQ(view.segments[0].page, 0);
  EXPECT_EQ(view.segments[0].firstRow, 0);
  EXPECT_EQ(view.segments[0].rowCount, 10);
  EXPECT_EQ(view.segments[0].yShift, 0);
  EXPECT_FALSE(view.reachesEnd);
}

TEST(ScrollViewCompose, AViewRunsOnIntoTheNextPage) {
  const auto c = threePages();
  const View view = compose({0, 4}, 200, c.rowsOf());
  ASSERT_EQ(view.segments.size(), 2u);
  EXPECT_EQ(view.segments[0].firstRow, 4);
  EXPECT_EQ(view.segments[0].rowCount, 6);
  EXPECT_EQ(view.segments[0].yShift, -80);  // row 4 (y 80) sits at the top
  EXPECT_EQ(view.segments[0].top, 0);
  EXPECT_EQ(view.segments[0].bottom, 120);
  EXPECT_EQ(view.segments[1].page, 1);
  EXPECT_EQ(view.segments[1].firstRow, 0);
  EXPECT_EQ(view.segments[1].rowCount, 4);
  EXPECT_EQ(view.segments[1].yShift, 120);  // right under page 0's last line
  EXPECT_EQ(view.segments[1].bottom, 200);
  EXPECT_FALSE(view.reachesEnd);
  EXPECT_EQ(rowsShown(view), 10);
}

TEST(ScrollViewCompose, OnlyWholeRows) {
  const auto c = threePages();
  const View view = compose({0, 4}, 190, c.rowsOf());  // 9.5 lines of room
  EXPECT_EQ(rowsShown(view), 9);
}

TEST(ScrollViewCompose, AShortPageLetsTheNextOneIn) {
  Chapter c;
  c.addPage(3);  // a page the widow control or an image left short
  c.addPage(10);
  const View view = compose({0, 0}, 200, c.rowsOf());
  ASSERT_EQ(view.segments.size(), 2u);
  EXPECT_EQ(view.segments[0].rowCount, 3);
  EXPECT_EQ(view.segments[1].rowCount, 7);
  EXPECT_EQ(view.segments[1].yShift, 60);
}

TEST(ScrollViewCompose, TheNextPageKeepsTheSpaceAboveItsFirstLine) {
  Chapter c;
  c.addPage(3);
  c.addPage(5, 20, 30);  // a heading with 30 px above it
  const View view = compose({0, 0}, 200, c.rowsOf());
  ASSERT_EQ(view.segments.size(), 2u);
  EXPECT_EQ(view.segments[1].yShift, 60);
  EXPECT_EQ(view.segments[1].top, 90);
  // As the view's own top line the same row sits at y 0.
  const View top = compose({1, 0}, 200, c.rowsOf());
  EXPECT_EQ(top.segments[0].yShift, -30);
  EXPECT_EQ(top.segments[0].top, 0);
}

TEST(ScrollViewCompose, TheChapterEnd) {
  const auto c = threePages();
  EXPECT_FALSE(compose({1, 9}, 200, c.rowsOf()).reachesEnd);
  const View view = compose({2, 0}, 200, c.rowsOf());
  EXPECT_TRUE(view.reachesEnd);
  EXPECT_EQ(rowsShown(view), 10);
  const View tail = compose({2, 7}, 200, c.rowsOf());
  EXPECT_TRUE(tail.reachesEnd);
  EXPECT_EQ(rowsShown(tail), 3);
}

TEST(ScrollViewCompose, ARowTallerThanTheScreenIsStillShown) {
  Chapter c;
  c.pages.push_back({{0, 400}});  // an image
  c.addPage(4);
  const View view = compose({0, 0}, 200, c.rowsOf());
  ASSERT_EQ(view.segments.size(), 1u);
  EXPECT_EQ(view.segments[0].rowCount, 1);
  EXPECT_FALSE(view.reachesEnd);
}

TEST(ScrollViewCompose, EmptyPagesAndBadPositions) {
  Chapter c;
  c.addPage(2);
  c.pages.push_back({});  // nothing to stop at
  c.addPage(2);
  const View view = compose({0, 0}, 200, c.rowsOf());
  ASSERT_EQ(view.segments.size(), 2u);
  EXPECT_EQ(view.segments[1].page, 2);
  EXPECT_TRUE(view.reachesEnd);
  // A row past the page's end is its last row, a page before the first is the first.
  EXPECT_EQ(clampPos({0, 99}, c.rowsOf()), (Pos{0, 1}));
  EXPECT_EQ(clampPos({-3, 4}, c.rowsOf()), (Pos{0, 0}));
  EXPECT_EQ(clampPos({1, 5}, c.rowsOf()), (Pos{1, 0}));
  // No pages at all: an empty view that is at its end.
  const Chapter none;
  const View empty = compose({0, 0}, 200, none.rowsOf());
  EXPECT_TRUE(empty.segments.empty());
  EXPECT_TRUE(empty.reachesEnd);
}

TEST(ScrollViewAdvance, RowsAcrossPages) {
  const auto c = threePages();
  EXPECT_EQ(advance({0, 0}, 3, c.rowsOf()), (Pos{0, 3}));
  EXPECT_EQ(advance({0, 8}, 3, c.rowsOf()), (Pos{1, 1}));
  EXPECT_EQ(advance({1, 1}, -3, c.rowsOf()), (Pos{0, 8}));
  EXPECT_EQ(advance({0, 0}, 25, c.rowsOf()), (Pos{2, 5}));
  EXPECT_EQ(advance({2, 5}, -25, c.rowsOf()), (Pos{0, 0}));
  EXPECT_EQ(advance({1, 4}, 0, c.rowsOf()), (Pos{1, 4}));
}

TEST(ScrollViewAdvance, StopsAtTheChapterEnds) {
  const auto c = threePages();
  EXPECT_EQ(advance({0, 2}, -10, c.rowsOf()), (Pos{0, 0}));
  EXPECT_EQ(advance({2, 7}, 10, c.rowsOf()), (Pos{2, 9}));
}

TEST(ScrollViewAdvance, StepsOverEmptyPages) {
  Chapter c;
  c.addPage(2);
  c.pages.push_back({});
  c.addPage(2);
  EXPECT_EQ(advance({0, 1}, 1, c.rowsOf()), (Pos{2, 0}));
  EXPECT_EQ(advance({2, 0}, -1, c.rowsOf()), (Pos{0, 1}));
}

TEST(ScrollViewAdvance, ByPixels) {
  const auto c = threePages();
  EXPECT_EQ(advanceByPixels({0, 0}, 100, c.rowsOf()), (Pos{0, 5}));
  EXPECT_EQ(advanceByPixels({0, 0}, 110, c.rowsOf()), (Pos{0, 5}));  // whole rows only
  EXPECT_EQ(advanceByPixels({0, 8}, 100, c.rowsOf()), (Pos{1, 3}));
  EXPECT_EQ(advanceByPixels({1, 3}, -100, c.rowsOf()), (Pos{0, 8}));
  EXPECT_EQ(advanceByPixels({0, 0}, 5, c.rowsOf()), (Pos{0, 1}));  // a short swipe: one row at least
  EXPECT_EQ(advanceByPixels({0, 3}, -5, c.rowsOf()), (Pos{0, 2}));
  EXPECT_EQ(advanceByPixels({0, 3}, 0, c.rowsOf()), (Pos{0, 3}));
  EXPECT_EQ(advanceByPixels({0, 1}, -500, c.rowsOf()), (Pos{0, 0}));
}

TEST(ScrollViewAdvance, PixelsRoundTrip) {
  const auto c = threePages();
  for (int row = 0; row < 10; ++row) {
    const Pos start{1, row};
    const Pos there = advanceByPixels(start, 100, c.rowsOf());
    if (there.page == 2 && there.row == 9) continue;  // stopped by the end
    EXPECT_EQ(advanceByPixels(there, -100, c.rowsOf()), start) << "row " << row;
  }
}

TEST(ScrollViewEnd, TheLastScreenIsAFullOne) {
  const auto c = threePages();
  EXPECT_EQ(endStart({2, 9}, 200, c.rowsOf()), (Pos{2, 0}));
  EXPECT_EQ(endStart({0, 0}, 200, c.rowsOf()), (Pos{2, 0}));
  Chapter shortEnd;
  shortEnd.addPage(10);
  shortEnd.addPage(4);  // the usual half-empty last page
  EXPECT_EQ(endStart({1, 3}, 200, shortEnd.rowsOf()), (Pos{0, 4}));
  const View view = compose({0, 4}, 200, shortEnd.rowsOf());
  EXPECT_TRUE(view.reachesEnd);
  EXPECT_EQ(rowsShown(view), 10);
}

TEST(ScrollViewEnd, AChapterShorterThanTheScreen) {
  Chapter c;
  c.addPage(4);
  EXPECT_EQ(endStart({0, 3}, 200, c.rowsOf()), (Pos{0, 0}));
  EXPECT_TRUE(compose({0, 0}, 200, c.rowsOf()).reachesEnd);
}

TEST(ScrollViewStep, TheSetting) {
  EXPECT_EQ(stepFor(kStep3Lines, 200, 20).rows, 3);
  EXPECT_EQ(stepFor(kStep5Lines, 200, 20).rows, 5);
  EXPECT_EQ(stepFor(kStep10Lines, 200, 20).rows, 10);
  EXPECT_EQ(stepFor(kStepHalfScreen, 200, 20).pixels, 100);
  EXPECT_EQ(stepFor(kStepHalfScreen, 200, 20).rows, 0);
  EXPECT_EQ(stepFor(kStepTwoThirds, 300, 20).pixels, 200);
  EXPECT_EQ(stepFor(kStepScreen, 200, 20).pixels, 180);
  EXPECT_EQ(stepFor(99, 200, 20).pixels, 100);  // an unknown value: half a screen
  EXPECT_EQ(stepFor(kStepScreen, 10, 50).pixels, 1);
}

TEST(ScrollViewScroll, StepsForwardAndBack) {
  const auto c = threePages();
  const Step lines3 = stepFor(kStep3Lines, 200, 20);
  EXPECT_EQ(scroll({0, 0}, 1, lines3, 0, 200, c.rowsOf()), (Pos{0, 3}));
  EXPECT_EQ(scroll({0, 0}, 2, lines3, 0, 200, c.rowsOf()), (Pos{0, 6}));
  EXPECT_EQ(scroll({0, 9}, 1, lines3, 0, 200, c.rowsOf()), (Pos{1, 2}));
  EXPECT_EQ(scroll({1, 2}, -1, lines3, 0, 200, c.rowsOf()), (Pos{0, 9}));
  EXPECT_EQ(scroll({0, 2}, -1, lines3, 0, 200, c.rowsOf()), (Pos{0, 0}));
  EXPECT_EQ(scroll({1, 2}, 0, lines3, 0, 200, c.rowsOf()), (Pos{1, 2}));
  const Step half = stepFor(kStepHalfScreen, 200, 20);
  EXPECT_EQ(scroll({0, 0}, 1, half, 0, 200, c.rowsOf()), (Pos{0, 5}));
  // The screen step leaves the last line read on screen.
  const Step screen = stepFor(kStepScreen, 200, 20);
  EXPECT_EQ(scroll({0, 0}, 1, screen, 0, 200, c.rowsOf()), (Pos{0, 9}));
}

TEST(ScrollViewScroll, ForwardStopsAtTheLastFullScreen) {
  const auto c = threePages();
  const Step lines5 = stepFor(kStep5Lines, 200, 20);
  // From (1,8) five lines on would be (2,3), which leaves part of the screen empty: it stops at (2,0).
  EXPECT_EQ(scroll({1, 8}, 1, lines5, 0, 200, c.rowsOf()), (Pos{2, 0}));
  // Already there: nothing moves (the reader then turns to the next chapter).
  EXPECT_EQ(scroll({2, 0}, 1, lines5, 0, 200, c.rowsOf()), (Pos{2, 0}));
  // A view that was opened past the last full screen (a bookmark on the last lines) is not pulled back.
  EXPECT_EQ(scroll({2, 6}, 1, lines5, 0, 200, c.rowsOf()), (Pos{2, 6}));
}

TEST(ScrollViewScroll, ASwipeGoesByItsLength) {
  const auto c = threePages();
  const Step lines3 = stepFor(kStep3Lines, 200, 20);
  EXPECT_EQ(scroll({0, 0}, 1, lines3, 140, 200, c.rowsOf()), (Pos{0, 7}));
  EXPECT_EQ(scroll({0, 7}, -1, lines3, 140, 200, c.rowsOf()), (Pos{0, 0}));
  EXPECT_EQ(scroll({0, 7}, -1, lines3, -140, 200, c.rowsOf()), (Pos{0, 0}));  // the sign is the step's
  EXPECT_EQ(scroll({0, 0}, 1, lines3, 4, 200, c.rowsOf()), (Pos{0, 1}));
}

TEST(ScrollViewScroll, EveryLineIsSeenOnTheWayThrough) {
  // Uneven pages, every step kind: walking forward never skips a row and ends on the last screen.
  Chapter c;
  c.addPage(10);
  c.addPage(7);
  c.addPage(1, 150);  // an image page
  c.addPage(10);
  c.addPage(3);
  for (uint8_t kind = 0; kind < kStepKindCount; ++kind) {
    const Step step = stepFor(kind, 200, 20);
    Pos pos{0, 0};
    std::vector<std::vector<bool>> seen;
    for (const auto& page : c.pages) seen.emplace_back(page.size(), false);
    bool ended = false;
    for (int guard = 0; guard < 200; ++guard) {
      const View view = compose(pos, 200, c.rowsOf());
      for (const Segment& s : view.segments) {
        for (int r = s.firstRow; r < s.firstRow + s.rowCount; ++r) {
          seen[static_cast<size_t>(s.page)][static_cast<size_t>(r)] = true;
        }
      }
      const Pos next = scroll(pos, 1, step, 0, 200, c.rowsOf());
      if (next == pos) {
        EXPECT_TRUE(view.reachesEnd) << "kind " << static_cast<int>(kind);
        ended = true;
        break;
      }
      EXPECT_TRUE(before(pos, next));
      pos = next;
    }
    EXPECT_TRUE(ended);
    for (size_t p = 0; p < seen.size(); ++p) {
      for (size_t r = 0; r < seen[p].size(); ++r) {
        EXPECT_TRUE(seen[p][r]) << "kind " << static_cast<int>(kind) << " page " << p << " row " << r;
      }
    }
  }
}

TEST(ScrollViewScroll, EveryLineIsSeenOnTheWayBack) {
  Chapter c;
  c.addPage(10);
  c.addPage(1, 150);  // an image page: room for two lines under it
  c.addPage(10);
  c.addPage(3);
  for (uint8_t kind = 0; kind < kStepKindCount; ++kind) {
    const Step step = stepFor(kind, 200, 20);
    Pos pos = endStart({3, 2}, 200, c.rowsOf());
    std::vector<std::vector<bool>> seen;
    for (const auto& page : c.pages) seen.emplace_back(page.size(), false);
    for (int guard = 0; guard < 200; ++guard) {
      const View view = compose(pos, 200, c.rowsOf());
      for (const Segment& s : view.segments) {
        for (int r = s.firstRow; r < s.firstRow + s.rowCount; ++r) {
          seen[static_cast<size_t>(s.page)][static_cast<size_t>(r)] = true;
        }
      }
      const Pos prev = scroll(pos, -1, step, 0, 200, c.rowsOf());
      if (prev == pos) break;
      pos = prev;
    }
    EXPECT_EQ(pos, (Pos{0, 0}));
    for (size_t p = 0; p < seen.size(); ++p) {
      for (size_t r = 0; r < seen[p].size(); ++r) {
        EXPECT_TRUE(seen[p][r]) << "kind " << static_cast<int>(kind) << " page " << p << " row " << r;
      }
    }
  }
}

TEST(ScrollViewScroll, BackFromTheEndReachesTheStart) {
  Chapter c;
  c.addPage(10);
  c.addPage(7);
  c.addPage(10);
  for (uint8_t kind = 0; kind < kStepKindCount; ++kind) {
    const Step step = stepFor(kind, 200, 20);
    Pos pos = endStart({2, 9}, 200, c.rowsOf());
    for (int guard = 0; guard < 200 && pos != (Pos{0, 0}); ++guard) {
      const Pos prev = scroll(pos, -1, step, 0, 200, c.rowsOf());
      ASSERT_TRUE(before(prev, pos)) << "kind " << static_cast<int>(kind);
      pos = prev;
    }
    EXPECT_EQ(pos, (Pos{0, 0}));
  }
}

TEST(ScrollViewScroll, TheSavedPageIsThePageOfTheTopLine) {
  // Progress, bookmarks and KOReader sync store the page: it is the same in both modes for a view at a page top.
  const auto c = threePages();
  const Step half = stepFor(kStepHalfScreen, 200, 20);
  Pos pos{0, 0};
  pos = scroll(pos, 1, half, 0, 200, c.rowsOf());
  EXPECT_EQ(pos.page, 0);  // top line still on page 0
  pos = scroll(pos, 1, half, 0, 200, c.rowsOf());
  EXPECT_EQ(pos, (Pos{1, 0}));  // a whole page on: the next page, as a page turn gives
}

TEST(ScrollViewTouch, SegmentUnderAFinger) {
  const auto c = threePages();
  const View view = compose({0, 4}, 200, c.rowsOf());
  EXPECT_EQ(segmentAt(view, -3), -1);
  EXPECT_EQ(segmentAt(view, 0), 0);
  EXPECT_EQ(segmentAt(view, 119), 0);
  EXPECT_EQ(segmentAt(view, 120), 1);
  EXPECT_EQ(segmentAt(view, 900), 1);
  // Page y of a touch at view y 130: page 1, y 10.
  EXPECT_EQ(130 - view.segments[1].yShift, 10);
}

TEST(ScrollViewRefresh, ACleaningRefreshEveryNScrolls) {
  EXPECT_EQ(capRefreshCounter(15, 8), 8);
  EXPECT_EQ(capRefreshCounter(3, 8), 3);
  EXPECT_EQ(capRefreshCounter(15, 0), 15);
  EXPECT_EQ(capRefreshCounter(1, 8), 1);
  // The reader's own cycle: a HALF refresh at 1 and a reload (here to 15), else count down. Capped at 4, the
  // HALF refresh comes on every fourth draw.
  int counter = 15;
  std::vector<int> halfAt;
  for (int draw = 1; draw <= 12; ++draw) {
    counter = capRefreshCounter(counter, 4);
    if (counter <= 1) {
      halfAt.push_back(draw);
      counter = 15;
    } else {
      --counter;
    }
  }
  EXPECT_EQ(halfAt, (std::vector<int>{4, 8, 12}));
}
