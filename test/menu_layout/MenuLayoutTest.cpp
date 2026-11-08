#include <gtest/gtest.h>

#include "util/MenuLayout.h"

using menulayout::arrange;
using menulayout::encode;
using menulayout::fullOrder;
using menulayout::maskOf;
using menulayout::move;

namespace {
const std::vector<int> DEFAULTS = {1, 2, 3, 4, 5};
}

TEST(MenuLayout, NothingStoredKeepsTheDefaultOrder) { EXPECT_EQ(arrange(DEFAULTS, "", 0, 0), DEFAULTS); }

TEST(MenuLayout, TheStoredOrderComesFirst) {
  EXPECT_EQ(arrange(DEFAULTS, encode({4, 2}), 0, 0), (std::vector<int>{4, 2, 1, 3, 5}));
}

TEST(MenuLayout, UnknownAndRepeatedIdsAreDropped) {
  // 9 is not a current entry, 2 repeats, '!' is not an id at all
  EXPECT_EQ(arrange(DEFAULTS, encode({2, 9, 2, 5}) + "!", 0, 0), (std::vector<int>{2, 5, 1, 3, 4}));
}

TEST(MenuLayout, ConditionalEntriesThatAreAbsentLeaveNoHole) {
  const std::vector<int> without4 = {1, 2, 3, 5};
  EXPECT_EQ(arrange(without4, encode({5, 4, 1}), 0, 0), (std::vector<int>{5, 1, 2, 3}));
}

TEST(MenuLayout, HiddenEntriesAreLeftOut) {
  EXPECT_EQ(arrange(DEFAULTS, "", maskOf(2) | maskOf(4), 0), (std::vector<int>{1, 3, 5}));
}

TEST(MenuLayout, ALockedEntryShowsEvenWhenHidden) {
  EXPECT_EQ(arrange(DEFAULTS, "", maskOf(2) | maskOf(3), maskOf(3)), (std::vector<int>{1, 3, 4, 5}));
}

TEST(MenuLayout, AnEmptyPanelFallsBackToTheDefaults) {
  EXPECT_EQ(arrange(DEFAULTS, "", maskOf(1) | maskOf(2) | maskOf(3) | maskOf(4) | maskOf(5), 0), DEFAULTS);
}

TEST(MenuLayout, FullOrderKeepsHiddenEntriesForTheEditor) {
  EXPECT_EQ(fullOrder(DEFAULTS, encode({3})), (std::vector<int>{3, 1, 2, 4, 5}));
}

TEST(MenuLayout, MoveShiftsTheOthers) {
  std::vector<int> ids = {1, 2, 3, 4, 5};
  move(ids, 3, 0);
  EXPECT_EQ(ids, (std::vector<int>{4, 1, 2, 3, 5}));
  move(ids, 0, 4);
  EXPECT_EQ(ids, (std::vector<int>{1, 2, 3, 5, 4}));
  move(ids, 2, 2);  // no-op
  move(ids, -1, 2);
  move(ids, 2, 9);
  EXPECT_EQ(ids, (std::vector<int>{1, 2, 3, 5, 4}));
}

TEST(MenuLayout, IdsOutsideTheMaskNeverCrash) {
  EXPECT_EQ(maskOf(-1), 0u);
  EXPECT_EQ(maskOf(32), 0u);
  EXPECT_EQ(arrange({40, 1}, "", 0, 0), (std::vector<int>{1}));
}
