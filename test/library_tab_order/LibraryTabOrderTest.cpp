#include <gtest/gtest.h>

#include "src/util/LibraryTabOrder.h"

using libtabs::Order;

TEST(LibraryTabOrderTest, ZeroIsTheDefault) {
  EXPECT_EQ(libtabs::decode(0), libtabs::DEFAULT_ORDER);
  EXPECT_EQ(libtabs::encode(libtabs::DEFAULT_ORDER), 0u);
}

TEST(LibraryTabOrderTest, RoundTrip) {
  Order order = libtabs::DEFAULT_ORDER;
  libtabs::move(order, 3, 0);  // Favourites first
  EXPECT_EQ(order, (Order{3, 0, 1, 2, 4}));
  EXPECT_EQ(libtabs::decode(libtabs::encode(order)), order);
}

TEST(LibraryTabOrderTest, MoveShiftsTheTabsBetweenAndIgnoresBadPositions) {
  Order order = libtabs::DEFAULT_ORDER;
  libtabs::move(order, 0, 2);
  EXPECT_EQ(order, (Order{1, 2, 0, 3, 4}));
  libtabs::move(order, 2, 2);
  libtabs::move(order, -1, 1);
  libtabs::move(order, 1, 5);
  EXPECT_EQ(order, (Order{1, 2, 0, 3, 4}));
}

TEST(LibraryTabOrderTest, RepairDropsUnknownAndRepeatsAndAppendsMissing) {
  // Stored: Author, Author again, an id from a future version (9), Recent; then the end.
  const uint32_t packed = 3u | (3u << 4) | (10u << 8) | (1u << 12);
  EXPECT_EQ(libtabs::decode(packed), (Order{2, 0, 1, 3, 4}));
}

TEST(LibraryTabOrderTest, PositionOfFindsTheTab) {
  const Order order{3, 0, 1, 2, 4};
  EXPECT_EQ(libtabs::positionOf(order, 3), 0);
  EXPECT_EQ(libtabs::positionOf(order, 4), 4);
}
