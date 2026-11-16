#include <gtest/gtest.h>

#include "activities/PanelAfterMenu.h"

using panelaftermenu::Screen;
using panelaftermenu::Step;
using panelaftermenu::step;

namespace {
// The clean page is back: nothing queued, a frame painted after the menu closed, the book in front.
constexpr Screen kPageBack{false, false, false, true, true, true};
}  // namespace

TEST(PanelAfterMenu, OpensOnceTheCleanPageIsPainted) { EXPECT_EQ(step(200, kPageBack), Step::Open); }

// The classic book menu is its own screen: the pop back to the book is still queued.
TEST(PanelAfterMenu, WaitsForTheMenuScreenToPop) {
  Screen s = kPageBack;
  s.changePending = true;
  EXPECT_EQ(step(0, s), Step::Wait);
}

TEST(PanelAfterMenu, WaitsForTheRequestedFrame) {
  Screen s = kPageBack;
  s.updateRequested = true;
  EXPECT_EQ(step(10, s), Step::Wait);
  s = kPageBack;
  s.rendering = true;
  EXPECT_EQ(step(10, s), Step::Wait);
}

// The menu is still in the frame buffer until a frame is finished after it closed.
TEST(PanelAfterMenu, WaitsWhileTheMenuIsStillOnTheGlass) {
  Screen s = kPageBack;
  s.paintedSinceClose = false;
  EXPECT_EQ(step(10, s), Step::Wait);
}

// A frame of the menu screen finished after the swipe does not count for the book under it.
TEST(PanelAfterMenu, WaitsForTheBooksOwnFrameAfterAPop) {
  Screen s = kPageBack;
  s.visitPainted = false;
  EXPECT_EQ(step(10, s), Step::Wait);
}

// Never two menus: another menu opened meanwhile, or the book is not in front any more.
TEST(PanelAfterMenu, DropsWhenAMenuIsOpenAgainOrTheBookIsGone) {
  Screen s = kPageBack;
  s.readerPage = false;
  EXPECT_EQ(step(10, s), Step::Drop);
}

TEST(PanelAfterMenu, DropsAfterTheWait) {
  Screen s = kPageBack;
  s.rendering = true;
  EXPECT_EQ(step(panelaftermenu::WAIT_MS, s), Step::Wait);
  EXPECT_EQ(step(panelaftermenu::WAIT_MS + 1, s), Step::Drop);
  EXPECT_EQ(step(panelaftermenu::WAIT_MS + 1, kPageBack), Step::Drop);
}
