#include <gtest/gtest.h>

#include "src/util/HeaderButtonsLayout.h"

// The file browser's header: refresh, gear and search at the right, the folder name left of them.
// On an X4 Pro (480 wide, side padding 20, header 84) a long folder name ran under the buttons.

namespace {
constexpr int W = 480;
constexpr int PAD = 20;
constexpr int H = 84;
}  // namespace

TEST(HeaderButtonsLayoutTest, ButtonsSitWhereTheyDidBefore) {
  // The same numbers the file browser used inline: size 60, search at 392, the others 82 apart.
  EXPECT_EQ(headerbuttons::size(H), 60);
  EXPECT_EQ(headerbuttons::searchX(W, PAD, H), 392);
  EXPECT_EQ(headerbuttons::gearX(W, PAD, H), 310);
  EXPECT_EQ(headerbuttons::refreshX(W, PAD, H), 228);
}

TEST(HeaderButtonsLayoutTest, ButtonsDoNotTouchAndStayOnTheScreen) {
  for (const int width : {480, 528, 800}) {
    const int size = headerbuttons::size(H);
    EXPECT_LT(headerbuttons::refreshX(width, PAD, H) + size, headerbuttons::gearX(width, PAD, H));
    EXPECT_LT(headerbuttons::gearX(width, PAD, H) + size, headerbuttons::searchX(width, PAD, H));
    EXPECT_LE(headerbuttons::searchX(width, PAD, H) + size, width - PAD);
  }
}

TEST(HeaderButtonsLayoutTest, TheTitleEndsBeforeTheFirstButton) {
  EXPECT_EQ(headerbuttons::titleMaxRight(W, PAD, H), 220);
  EXPECT_LT(headerbuttons::titleMaxRight(W, PAD, H), headerbuttons::refreshX(W, PAD, H));
}

TEST(HeaderButtonsLayoutTest, ReserveCutsTheTextBackToTheLimit) {
  // The header's text would run to 460 (480 less the padding): 240 px are taken off.
  const int textRight = W - PAD;
  const int reserve = headerbuttons::titleReserve(textRight, headerbuttons::titleMaxRight(W, PAD, H));
  EXPECT_EQ(reserve, 240);
  EXPECT_EQ(textRight - reserve, headerbuttons::titleMaxRight(W, PAD, H));
}

TEST(HeaderButtonsLayoutTest, NoLimitOrALimitBeyondTheTextReservesNothing) {
  EXPECT_EQ(headerbuttons::titleReserve(460, 0), 0);    // every other header
  EXPECT_EQ(headerbuttons::titleReserve(460, 470), 0);  // never negative
  EXPECT_EQ(headerbuttons::titleReserve(460, 460), 0);
}

TEST(HeaderButtonsLayoutTest, AShortCentredTitleKeepsTheWholeBand) {
  // The default theme centres titles: "Books" (say 90 px) centred in 480 ends at 285, past the
  // limit of 220, so it is drawn from the left; a 40 px one would too (260). With the limit at
  // 300 the 90 px title stays centred, a 140 px one (310) does not.
  const int limit = headerbuttons::titleMaxRight(W, PAD, H);
  EXPECT_FALSE(headerbuttons::centredTitleFits(0, W, 90, limit));
  EXPECT_TRUE(headerbuttons::centredTitleFits(0, W, 90, 300));
  EXPECT_TRUE(headerbuttons::centredTitleFits(0, W, 120, 300));
  EXPECT_FALSE(headerbuttons::centredTitleFits(0, W, 121, 300));
  EXPECT_FALSE(headerbuttons::centredTitleFits(0, W, 140, 300));
  // The band's own offset counts.
  EXPECT_FALSE(headerbuttons::centredTitleFits(30, W - 20, 120, 300));
  // No limit: every title fits, as on every other screen.
  EXPECT_TRUE(headerbuttons::centredTitleFits(0, W, 1000, 0));
}
