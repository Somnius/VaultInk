#include <gtest/gtest.h>

#include <algorithm>

#include "src/util/HomeRowLayout.h"

using homelayout::compute;
using homelayout::Fonts;
using homelayout::Layout;

namespace {
constexpr Fonts FONTS{30, 26, 20};  // title, body, small line heights

// What the list draws: a row is never shorter than its content.
int drawnHeight(const Layout& layout, const int rows, const Fonts& fonts) {
  const int row = std::max(layout.rowHeight, homelayout::rowNeed(fonts, layout.bigPrimary, layout.subtitles));
  return rows * (row + layout.rowGap) - layout.rowGap;
}
}  // namespace

TEST(HomeRowLayoutTest, FullSizeWhenThereIsRoom) {
  for (int rows = 1; rows <= 9; ++rows) {
    const Layout layout = compute(rows * (68 + 8), rows, FONTS);
    EXPECT_EQ(layout.rowHeight, 68) << rows;
    EXPECT_EQ(layout.rowGap, 8) << rows;
    EXPECT_TRUE(layout.bigPrimary && layout.subtitles && layout.fits) << rows;
  }
}

TEST(HomeRowLayoutTest, TenRowsUseTheCompactGap) {
  const Layout layout = compute(10 * (68 + 3), 10, FONTS);
  EXPECT_EQ(layout.rowHeight, 68);
  EXPECT_EQ(layout.rowGap, 3);
  EXPECT_TRUE(layout.bigPrimary && layout.subtitles && layout.fits);
}

TEST(HomeRowLayoutTest, HeightShrinksFirst) {
  // 8 rows in 560 px: (560 + 8) / 8 - 8 = 63, still above what a tile needs (50).
  const Layout layout = compute(560, 8, FONTS);
  EXPECT_EQ(layout.rowHeight, 63);
  EXPECT_EQ(layout.rowGap, 8);
  EXPECT_TRUE(layout.bigPrimary && layout.subtitles && layout.fits);
}

TEST(HomeRowLayoutTest, ThenTheGap) {
  // 9 rows in 500 px: with gap 8 a row would be 48, under the 50 it needs; with gap 3 it is 52.
  const Layout layout = compute(500, 9, FONTS);
  EXPECT_EQ(layout.rowGap, 3);
  EXPECT_EQ(layout.rowHeight, 52);
  EXPECT_TRUE(layout.bigPrimary && layout.subtitles && layout.fits);
}

TEST(HomeRowLayoutTest, ThenTheBigBooksTile) {
  // 10 rows in 500 px: 47 px a row, enough for body + small (46) but not title + small (50).
  const Layout layout = compute(500, 10, FONTS);
  EXPECT_EQ(layout.rowHeight, 47);
  EXPECT_FALSE(layout.bigPrimary);
  EXPECT_TRUE(layout.subtitles);
  EXPECT_TRUE(layout.fits);
}

TEST(HomeRowLayoutTest, ThenTheStatusLines) {
  // 10 rows in 470 px: 44 px a row, the touch minimum, label only.
  const Layout layout = compute(470, 10, FONTS);
  EXPECT_EQ(layout.rowHeight, 44);
  EXPECT_FALSE(layout.bigPrimary);
  EXPECT_FALSE(layout.subtitles);
  EXPECT_TRUE(layout.fits);
}

TEST(HomeRowLayoutTest, TooShortIsReportedAndKeepsTheMinimum) {
  const Layout layout = compute(200, 10, FONTS);
  EXPECT_FALSE(layout.fits);
  EXPECT_EQ(layout.rowHeight, 44);
  EXPECT_EQ(layout.rowGap, 3);
  EXPECT_FALSE(layout.subtitles);
}

TEST(HomeRowLayoutTest, BigFontsCountWithTheRealRowHeight) {
  // Title + small is 80, over the 68 nominal height: three rows still fit 300 px as they are drawn.
  const Fonts big{48, 40, 32};
  const Layout layout = compute(300, 3, big);
  EXPECT_EQ(layout.rowHeight, 80);
  EXPECT_TRUE(layout.bigPrimary && layout.subtitles && layout.fits);
  // Four rows don't (4 * 80 + 3 * 3 = 329): the smaller tiles do (4 * 72 + 9 = 297).
  const Layout four = compute(300, 4, big);
  EXPECT_FALSE(four.bigPrimary);
  EXPECT_TRUE(four.subtitles);
  EXPECT_EQ(four.rowHeight, 72);
  EXPECT_TRUE(four.fits);
}

TEST(HomeRowLayoutTest, WhateverFitsIsNeverCut) {
  const Fonts fonts[] = {FONTS, {22, 18, 14}, {40, 34, 26}, {48, 40, 32}};
  for (const Fonts& f : fonts) {
    for (int rows = 1; rows <= 10; ++rows) {
      for (int space = 0; space <= 900; ++space) {
        const Layout layout = compute(space, rows, f);
        ASSERT_GE(layout.rowHeight, homelayout::MIN_ROW_HEIGHT);
        if (layout.fits) {
          ASSERT_LE(drawnHeight(layout, rows, f), space) << rows << " rows in " << space;
        } else {
          // Only when even the smallest layout is too tall.
          ASSERT_GT(rows * (homelayout::rowNeed(f, false, false) + 3) - 3, space) << rows << " rows in " << space;
        }
      }
    }
  }
}

TEST(HomeRowLayoutTest, NoRows) {
  const Layout layout = compute(500, 0, FONTS);
  EXPECT_TRUE(layout.fits);
  EXPECT_EQ(homelayout::listHeight(layout, 0), 0);
}
