#include <gtest/gtest.h>

#include "src/util/HomeRowOrder.h"

using homerows::Order;

TEST(HomeRowOrderTest, ZeroIsTheDefault) {
  EXPECT_EQ(homerows::decode(0), homerows::DEFAULT_ORDER);
  EXPECT_EQ(homerows::encode(homerows::DEFAULT_ORDER), 0u);
}

TEST(HomeRowOrderTest, RoundTrip) {
  Order order =
      homerows::DEFAULT_ORDER;  // Books, Vaults, Favourites, Search, Files, Gallery, Transfer, Library, Settings
  homerows::move(order, 8, 0);  // Settings first
  EXPECT_EQ(order, (Order{7, 0, 1, 8, 2, 3, 4, 5, 6}));
  EXPECT_EQ(homerows::decode(homerows::encode(order)), order);
}

TEST(HomeRowOrderTest, MoveDownShiftsTheRowsBetween) {
  Order order = homerows::DEFAULT_ORDER;
  homerows::move(order, 1, 5);  // Vaults after Gallery
  EXPECT_EQ(order, (Order{0, 8, 2, 3, 4, 1, 5, 6, 7}));
  homerows::move(order, 4, 4);  // no-op
  homerows::move(order, -1, 3);
  homerows::move(order, 2, 9);
  EXPECT_EQ(order, (Order{0, 8, 2, 3, 4, 1, 5, 6, 7}));
}

TEST(HomeRowOrderTest, RepairDropsUnknownAndRepeatsAndAppendsMissing) {
  // Stored: Settings, Settings again, an id from a future version (12), Books; then the end.
  const uint64_t packed = (8u) | (8u << 4) | (13u << 8) | (1u << 12);
  EXPECT_EQ(homerows::decode(packed), (Order{7, 0, 1, 8, 2, 3, 4, 5, 6}));
}

TEST(HomeRowOrderTest, AnOrderSavedBeforeFavouritesGetsItAtTheEnd) {
  // An eight-row order from 0.32.0-0.43.0 (a uint32): Settings first, then the rest.
  const Order eight{7, 0, 1, 2, 3, 4, 5, 6, 0};
  uint32_t packed = 0;
  for (int i = 0; i < 8; ++i) packed |= static_cast<uint32_t>(eight[i] + 1) << (4 * i);
  EXPECT_EQ(homerows::decode(packed), (Order{7, 0, 1, 2, 3, 4, 5, 6, 8}));
}

TEST(HomeRowOrderTest, BooksAndSettingsCantBeHidden) {
  const uint16_t all = 0xFFFF;
  EXPECT_FALSE(homerows::hidden(all, 0));  // Books
  EXPECT_FALSE(homerows::hidden(all, 7));  // Settings
  EXPECT_TRUE(homerows::hidden(all, 8));   // Favourites
  EXPECT_FALSE(homerows::hidden(0, 2));
  EXPECT_TRUE(homerows::hidden(1u << 2, 2));
}
