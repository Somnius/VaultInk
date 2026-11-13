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

TEST(MenuLayout, ATextPanelOrderSavedBeforeARowWasAddedStillLoads) {
  // 0.73.0 had 13 Text rows (0-12); Continuous scrolling is row 13 (kos25k #30). A saved order of the 13 keeps
  // its order and its hidden rows, and the new row comes last.
  std::vector<int> rows14;
  for (int i = 0; i < 14; ++i) rows14.push_back(i);
  const std::string saved = encode({12, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
  EXPECT_EQ(saved.size(), 13u);
  EXPECT_EQ(fullOrder(rows14, saved), (std::vector<int>{12, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13}));
  EXPECT_EQ(arrange(rows14, saved, maskOf(5) | maskOf(11), 0),
            (std::vector<int>{12, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 13}));
  // All 14 fit the stored field (char textMenuOrder[24]).
  EXPECT_LT(encode(rows14).size(), 24u);
}

TEST(MenuLayout, IdsOutsideTheMaskNeverCrash) {
  EXPECT_EQ(maskOf(-1), 0u);
  EXPECT_EQ(maskOf(32), 0u);
  EXPECT_EQ(arrange({40, 1}, "", 0, 0), (std::vector<int>{1}));
}
