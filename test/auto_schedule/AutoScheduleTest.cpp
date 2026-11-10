#include <AutoSchedule.h>
#include <gtest/gtest.h>

using namespace autoschedule;

TEST(AutoSchedule, WindowThatCrossesMidnight) {
  EXPECT_TRUE(inNightWindow(20, 7, 20));
  EXPECT_TRUE(inNightWindow(20, 7, 23));
  EXPECT_TRUE(inNightWindow(20, 7, 0));
  EXPECT_TRUE(inNightWindow(20, 7, 6));
  EXPECT_FALSE(inNightWindow(20, 7, 7));  // the end hour is day
  EXPECT_FALSE(inNightWindow(20, 7, 12));
  EXPECT_FALSE(inNightWindow(20, 7, 19));
}

TEST(AutoSchedule, WindowInsideOneDay) {
  EXPECT_TRUE(inNightWindow(1, 5, 1));
  EXPECT_TRUE(inNightWindow(1, 5, 4));
  EXPECT_FALSE(inNightWindow(1, 5, 5));
  EXPECT_FALSE(inNightWindow(1, 5, 0));
}

TEST(AutoSchedule, EmptyOrBadWindowIsNeverNight) {
  EXPECT_FALSE(inNightWindow(7, 7, 7));
  EXPECT_FALSE(inNightWindow(24, 7, 3));
  EXPECT_FALSE(inNightWindow(20, 99, 3));
  EXPECT_FALSE(inNightWindow(20, 7, 24));
}

TEST(AutoSchedule, StepsOnlyOnTransitions) {
  EXPECT_EQ(next(true, 20, 7, 21, 0), Step::EnterNight);
  EXPECT_EQ(next(true, 20, 7, 22, 1), Step::None);  // already applied; a manual change stays
  EXPECT_EQ(next(true, 20, 7, 7, 1), Step::EnterDay);
  EXPECT_EQ(next(true, 20, 7, 12, 0), Step::None);
}

TEST(AutoSchedule, DisabledDoesNothing) {
  EXPECT_EQ(next(false, 20, 7, 22, 0), Step::None);
  EXPECT_EQ(next(false, 20, 7, 12, 1), Step::None);
}
