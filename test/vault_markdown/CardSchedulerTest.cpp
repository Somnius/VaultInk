#include <gtest/gtest.h>

#include "CardScheduler.h"

using namespace cardsched;

namespace {
constexpr uint32_t TODAY = 20000;
const Config CFG{};
constexpr uint32_t NO_FUZZ = 5;  // seed % 11 == 5 is a 0% nudge

CardState known(uint16_t interval, uint16_t ease = 250) {
  CardState s;
  s.last = TODAY - interval;
  s.due = TODAY;
  s.interval = interval;
  s.ease = ease;
  s.reps = 3;
  return s;
}
}  // namespace

TEST(CardScheduler, NewCardPaths) {
  const CardState fresh;
  ASSERT_TRUE(fresh.isNew());
  EXPECT_EQ(review(fresh, Answer::Again, TODAY, CFG).due, TODAY);
  EXPECT_TRUE(review(fresh, Answer::Again, TODAY, CFG).last == TODAY);  // seen today, no longer new
  EXPECT_EQ(review(fresh, Answer::Hard, TODAY, CFG).due, TODAY + 1);
  EXPECT_EQ(review(fresh, Answer::Good, TODAY, CFG).due, TODAY + 1);
  EXPECT_EQ(review(fresh, Answer::Easy, TODAY, CFG).due, TODAY + 4);
  EXPECT_EQ(review(fresh, Answer::Good, TODAY, CFG).reps, 1);
  EXPECT_EQ(review(fresh, Answer::Good, TODAY, CFG).ease, 250);
}

TEST(CardScheduler, ReviewAnswers) {
  const CardState s = known(10);
  const CardState good = review(s, Answer::Good, TODAY, CFG, NO_FUZZ);
  EXPECT_EQ(good.interval, 25);  // 10 * 2.5
  EXPECT_EQ(good.due, TODAY + 25);
  EXPECT_EQ(good.ease, 250);
  const CardState hard = review(s, Answer::Hard, TODAY, CFG, NO_FUZZ);
  EXPECT_EQ(hard.interval, 12);
  EXPECT_EQ(hard.ease, 235);
  const CardState easy = review(s, Answer::Easy, TODAY, CFG, NO_FUZZ);
  EXPECT_EQ(easy.interval, 32);  // 10 * 3.25
  EXPECT_EQ(easy.ease, 265);
  const CardState again = review(s, Answer::Again, TODAY, CFG, NO_FUZZ);
  EXPECT_EQ(again.interval, 1);
  EXPECT_EQ(again.due, TODAY);
  EXPECT_EQ(again.ease, 230);
  EXPECT_EQ(again.lapses, 1);
  EXPECT_EQ(again.reps, 0);
}

TEST(CardScheduler, EveryAnswerMovesAtLeastOneDay) {
  const CardState s = known(1, MIN_EASE);
  EXPECT_GE(review(s, Answer::Hard, TODAY, CFG).interval, 2);
  EXPECT_GE(review(s, Answer::Good, TODAY, CFG).interval, 2);
}

TEST(CardScheduler, EaseStaysInRange) {
  CardState s = known(10, MIN_EASE + 5);
  s = review(s, Answer::Again, TODAY, CFG);
  EXPECT_EQ(s.ease, MIN_EASE);
  CardState e = known(10, MAX_EASE - 5);
  e = review(e, Answer::Easy, TODAY, CFG);
  EXPECT_EQ(e.ease, MAX_EASE);
}

TEST(CardScheduler, IntervalIsCapped) {
  Config small;
  small.maxInterval = 30;
  EXPECT_EQ(review(known(100), Answer::Good, TODAY, small, NO_FUZZ).interval, 30);
  EXPECT_EQ(review(known(200), Answer::Easy, TODAY, CFG, NO_FUZZ).interval, 365);
}

TEST(CardScheduler, FuzzIsDeterministicAndSmall) {
  const CardState s = known(100);
  const auto a = review(s, Answer::Good, TODAY, CFG, 7);
  const auto b = review(s, Answer::Good, TODAY, CFG, 7);
  EXPECT_EQ(a.interval, b.interval);
  for (uint32_t seed = 0; seed < 40; ++seed) {
    const int got = review(s, Answer::Good, TODAY, CFG, seed).interval;
    EXPECT_GE(got, 237);  // 250 -5%
    EXPECT_LE(got, 263);
  }
  EXPECT_EQ(review(known(2), Answer::Good, TODAY, CFG, 3).interval, 5);  // short intervals aren't fuzzed
}

TEST(CardScheduler, LapsesAndRepsSaturate) {
  CardState s = known(10);
  s.lapses = 255;
  EXPECT_EQ(review(s, Answer::Again, TODAY, CFG).lapses, 255);
  s.reps = 255;
  EXPECT_EQ(review(s, Answer::Good, TODAY, CFG).reps, 255);
}

TEST(CardScheduler, DueAndDays) {
  CardState s = known(10);
  s.due = TODAY + 3;
  EXPECT_FALSE(s.isDue(TODAY));
  EXPECT_TRUE(s.isDue(TODAY + 3));
  EXPECT_TRUE(CardState{}.isDue(0));
  EXPECT_EQ(daysUntilDue(known(10), Answer::Again, TODAY, CFG, NO_FUZZ), 0u);
  EXPECT_EQ(daysUntilDue(known(10), Answer::Good, TODAY, CFG, NO_FUZZ), 25u);
  EXPECT_EQ(dayOf(0), 0u);
  EXPECT_EQ(dayOf(978307199), 0u);
  EXPECT_EQ(dayOf(1793369657), 1793369657u / 86400);
}
