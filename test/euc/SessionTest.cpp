#include <gtest/gtest.h>

#include "Session.h"

using namespace euc;
using namespace euc::session;

namespace {
Telemetry moving(const float kmh) {
  Telemetry t;
  t.speedKmh = kmh;
  t.mark(F_SPEED);
  return t;
}
}  // namespace

TEST(Session, IsMoving) {
  EXPECT_FALSE(isMoving(moving(0.0f)));
  EXPECT_FALSE(isMoving(moving(2.0f)));  // the rule is "above 2 km/h"
  EXPECT_TRUE(isMoving(moving(2.1f)));
  EXPECT_TRUE(isMoving(moving(-5.0f)));  // reverse counts as moving
  Telemetry unknown;
  EXPECT_TRUE(isMoving(unknown));  // no speed yet: fail safe
}

TEST(Session, StoppedAllowsEverySafeCommand) {
  const Telemetry t = moving(0.0f);
  for (uint8_t i = 0; i < static_cast<uint8_t>(Command::Count); ++i) {
    EXPECT_TRUE(commandAllowed(static_cast<Command>(i), t)) << i;
  }
  EXPECT_FALSE(commandAllowed(Command::Count, t));
}

TEST(Session, MovingAllowsOnlyTheWhitelist) {
  const Telemetry t = moving(25.0f);
  EXPECT_TRUE(commandAllowed(Command::Horn, t));
  EXPECT_TRUE(commandAllowed(Command::Beep, t));
  EXPECT_TRUE(commandAllowed(Command::Lights, t));
  EXPECT_TRUE(commandAllowed(Command::RideLeds, t));
  EXPECT_TRUE(commandAllowed(Command::Volume, t));
  EXPECT_TRUE(commandAllowed(Command::RequestInfo, t));
  EXPECT_FALSE(commandAllowed(Command::PedalMode, t));
  EXPECT_FALSE(commandAllowed(Command::ResetTrip, t));
  EXPECT_FALSE(commandAllowed(Command::Miles, t));
  EXPECT_FALSE(commandAllowed(Command::Count, t));
}

TEST(Session, UnknownSpeedCountsAsMoving) {
  const Telemetry t;
  EXPECT_FALSE(commandAllowed(Command::PedalMode, t));
  EXPECT_FALSE(commandAllowed(Command::ResetTrip, t));
  EXPECT_TRUE(commandAllowed(Command::Horn, t));
}

TEST(Session, RefreshClamp) {
  EXPECT_EQ(clampRefreshSeconds(0), 5);
  EXPECT_EQ(clampRefreshSeconds(1), 5);
  EXPECT_EQ(clampRefreshSeconds(4), 5);
  EXPECT_EQ(clampRefreshSeconds(5), 5);
  EXPECT_EQ(clampRefreshSeconds(10), 10);
  EXPECT_EQ(clampRefreshSeconds(30), 30);
  EXPECT_EQ(clampRefreshSeconds(31), 30);
  EXPECT_EQ(clampRefreshSeconds(0xFFFFFFFFu), 30);
}
