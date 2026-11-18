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
