#include <gtest/gtest.h>

#include "src/util/HomeRowOrder.h"

using homerows::Order;

TEST(HomeRowOrderTest, ZeroIsTheDefault) {
  EXPECT_EQ(homerows::decode(0), homerows::DEFAULT_ORDER);
  EXPECT_EQ(homerows::encode(homerows::DEFAULT_ORDER), 0u);
}

TEST(HomeRowOrderTest, RoundTrip) {
  Order order = homerows::DEFAULT_ORDER;
  homerows::move(order, 7, 0);  // Settings first
  EXPECT_EQ(order, (Order{7, 0, 1, 2, 3, 4, 5, 6}));
  EXPECT_EQ(homerows::decode(homerows::encode(order)), order);
}

TEST(HomeRowOrderTest, MoveDownShiftsTheRowsBetween) {
  Order order = homerows::DEFAULT_ORDER;
  homerows::move(order, 1, 4);  // Vaults after Gallery
  EXPECT_EQ(order, (Order{0, 2, 3, 4, 1, 5, 6, 7}));
  homerows::move(order, 4, 4);  // no-op
  homerows::move(order, -1, 3);
  homerows::move(order, 2, 8);
  EXPECT_EQ(order, (Order{0, 2, 3, 4, 1, 5, 6, 7}));
}

TEST(HomeRowOrderTest, RepairDropsUnknownAndRepeatsAndAppendsMissing) {
  // Stored: Settings, Settings again, an id from a future version (12), Books; then the end.
  const uint32_t packed = (8u) | (8u << 4) | (13u << 8) | (1u << 12);
  EXPECT_EQ(homerows::decode(packed), (Order{7, 0, 1, 2, 3, 4, 5, 6}));
}

TEST(HomeRowOrderTest, ShorterStoredOrderGetsNewRowsAtTheEnd) {
  // A seven-row order saved before the eighth row existed (here: without Gallery, id 4).
  Order seven{5, 0, 1, 2, 3, 6, 7, 0};
  uint32_t packed = 0;
  for (int i = 0; i < 7; ++i) packed |= static_cast<uint32_t>(seven[i] + 1) << (4 * i);
  EXPECT_EQ(homerows::decode(packed), (Order{5, 0, 1, 2, 3, 6, 7, 4}));
}
