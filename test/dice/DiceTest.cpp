#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <vector>

#include "Dice.h"

using namespace dice;

namespace {

// Hands out scripted values in order; past the script it returns 1 (and records the overrun).
class ScriptRng final : public Rng {
 public:
  explicit ScriptRng(std::initializer_list<int> values) : script(values) {}
  int roll(int sides) override {
    if (next >= script.size()) {
      ++overrun;
      return 1;
    }
    const int v = script[next++];
    EXPECT_LE(v, sides) << "script value above the die size";
    return v;
  }
  size_t used() const { return next; }
  int overrun = 0;

 private:
  std::vector<int> script;
  size_t next = 0;
};

std::vector<int> values(const Result& r) {
  std::vector<int> out;
  for (int i = 0; i < r.count; ++i) out.push_back(r.dice[i].value);
  return out;
}

int countFlag(const Result& r, DieFlag f) {
  return static_cast<int>(
      std::count_if(r.dice.begin(), r.dice.begin() + r.count, [f](const Die& d) { return d.has(f); }));
}

// ---- RNG ----

TEST(DiceRng, UniformRangeAndSeedDeterminism) {
  Xorshift32 a(42);
  Xorshift32 b(42);
  for (int i = 0; i < 1000; ++i) {
    const int v = a.roll(20);
    EXPECT_GE(v, 1);
    EXPECT_LE(v, 20);
    EXPECT_EQ(v, b.roll(20));
  }
  Xorshift32 zero(0);
  EXPECT_NE(zero.next(), 0u);
}

TEST(DiceRng, StatisticsLookUniform) {
  Xorshift32 rng(2026);
  constexpr int SIDES = 6;
  constexpr int N = 60000;
  std::array<int, SIDES + 1> hist{};
  for (int i = 0; i < N; ++i) ++hist[rng.roll(SIDES)];
  EXPECT_EQ(hist[0], 0);
  // Expected 10000 per face; sd ~ 91. Allow 5 sd.
  for (int f = 1; f <= SIDES; ++f) EXPECT_NEAR(hist[f], N / SIDES, 460) << "face " << f;
  // d10 too, since every pool system uses it.
  std::array<int, 11> h10{};
  for (int i = 0; i < N; ++i) ++h10[rng.roll(10)];
  for (int f = 1; f <= 10; ++f) EXPECT_NEAR(h10[f], N / 10, 400) << "face " << f;
}

// ---- polyhedral ----

TEST(DicePolyhedral, ParsesCommonNotation) {
  PolyParams p;
  ParseError err = ParseError::None;
  ASSERT_TRUE(parseNotation("2d6+3", p, err));
  EXPECT_EQ(p.count, 2);
  EXPECT_EQ(p.sides, 6);
  EXPECT_EQ(p.modifier, 3);
  EXPECT_EQ(p.keep, 0);
  EXPECT_FALSE(p.explode);

  ASSERT_TRUE(parseNotation("d20", p, err));
  EXPECT_EQ(p.count, 1);
  EXPECT_EQ(p.sides, 20);

  ASSERT_TRUE(parseNotation("4d6kh3", p, err));
  EXPECT_EQ(p.count, 4);
  EXPECT_EQ(p.keep, 3);
  EXPECT_TRUE(p.keepHigh);

  ASSERT_TRUE(parseNotation("2D20KL1 - 2", p, err));
  EXPECT_EQ(p.keep, 1);
  EXPECT_FALSE(p.keepHigh);
  EXPECT_EQ(p.modifier, -2);

  ASSERT_TRUE(parseNotation("d%", p, err));
  EXPECT_EQ(p.sides, 100);
  ASSERT_TRUE(parseNotation("d100", p, err));
  EXPECT_EQ(p.sides, 100);

  ASSERT_TRUE(parseNotation("3d6!", p, err));
  EXPECT_TRUE(p.explode);
}

TEST(DicePolyhedral, ParseErrors) {
  PolyParams p;
  ParseError err = ParseError::None;
  EXPECT_FALSE(parseNotation("", p, err));
  EXPECT_EQ(err, ParseError::Empty);
  EXPECT_FALSE(parseNotation(nullptr, p, err));
  EXPECT_EQ(err, ParseError::Empty);
  EXPECT_FALSE(parseNotation("abc", p, err));
  EXPECT_EQ(err, ParseError::BadSyntax);
  EXPECT_FALSE(parseNotation("2d", p, err));
  EXPECT_EQ(err, ParseError::BadSyntax);
  EXPECT_FALSE(parseNotation("65d6", p, err));
  EXPECT_EQ(err, ParseError::TooManyDice);
  EXPECT_FALSE(parseNotation("0d6", p, err));
  EXPECT_EQ(err, ParseError::TooManyDice);
  EXPECT_FALSE(parseNotation("2d1", p, err));
  EXPECT_EQ(err, ParseError::BadSides);
  EXPECT_FALSE(parseNotation("4d6kh5", p, err));
  EXPECT_EQ(err, ParseError::BadKeep);
  EXPECT_FALSE(parseNotation("4d6kx2", p, err));
  EXPECT_EQ(err, ParseError::BadSyntax);
  EXPECT_FALSE(parseNotation("d6+", p, err));
  EXPECT_EQ(err, ParseError::BadModifier);
  EXPECT_FALSE(parseNotation("d6+12345", p, err));
  EXPECT_EQ(err, ParseError::BadModifier);
  EXPECT_FALSE(parseNotation("d6 junk", p, err));
  EXPECT_EQ(err, ParseError::BadSyntax);

  Xorshift32 rng(1);
  const Result r = rollNotation(rng, "nope");
  EXPECT_FALSE(r.ok);
  EXPECT_EQ(r.error, ParseError::BadSyntax);
  EXPECT_EQ(r.count, 0);
}

TEST(DicePolyhedral, SumWithModifier) {
  ScriptRng rng{3, 5};
  const Result r = rollNotation(rng, "2d6+3");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.count, 2);
  EXPECT_EQ(r.total, 11);
  EXPECT_EQ(r.modifier, 3);
  EXPECT_EQ(countFlag(r, FLAG_KEPT), 2);
  EXPECT_EQ(r.outcome, Outcome::None);
}

TEST(DicePolyhedral, KeepHighestThree) {
  ScriptRng rng{2, 6, 1, 5};
  const Result r = rollNotation(rng, "4d6kh3");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(r.total, 13);
  EXPECT_EQ(countFlag(r, FLAG_KEPT), 3);
  EXPECT_FALSE(r.dice[2].has(FLAG_KEPT));  // the 1 is dropped
}

TEST(DicePolyhedral, KeepLowestDropsTies) {
  ScriptRng rng{4, 4, 9};
  const Result r = rollNotation(rng, "3d10kl1");
  EXPECT_EQ(r.total, 4);
  EXPECT_EQ(countFlag(r, FLAG_KEPT), 1);
}

TEST(DicePolyhedral, AdvantageAndDisadvantage) {
  ScriptRng adv{7, 20};
  const Result a = rollAdvantage(adv, 5);
  EXPECT_EQ(a.total, 25);
  EXPECT_EQ(a.outcome, Outcome::CriticalSuccess);
  EXPECT_TRUE(a.dice[1].has(FLAG_KEPT));
  EXPECT_FALSE(a.dice[0].has(FLAG_KEPT));

  ScriptRng dis{1, 15};
  const Result d = rollDisadvantage(dis, 2);
  EXPECT_EQ(d.total, 3);
  EXPECT_EQ(d.outcome, Outcome::CriticalFailure);

  ScriptRng plain{10, 12};
  EXPECT_EQ(rollAdvantage(plain, 0).outcome, Outcome::None);
}

TEST(DicePolyhedral, ExplodingDice) {
  ScriptRng rng{6, 6, 2, 3};
  const Result r = rollNotation(rng, "2d6!");
  ASSERT_TRUE(r.ok);
  EXPECT_EQ(values(r), (std::vector<int>{6, 6, 2, 3}));
  EXPECT_EQ(r.total, 17);
  EXPECT_TRUE(r.dice[0].has(FLAG_EXPLODED));
  EXPECT_TRUE(r.dice[1].has(FLAG_EXPLODED));
  EXPECT_TRUE(r.dice[1].has(FLAG_EXTRA));
  EXPECT_TRUE(r.dice[2].has(FLAG_EXTRA));
  EXPECT_FALSE(r.dice[3].has(FLAG_EXTRA));
  EXPECT_FALSE(r.truncated);
}

TEST(DicePolyhedral, ExplodeCapAtTwentyExtraDice) {
  // An RNG that always rolls the max face.
  class MaxRng final : public Rng {
   public:
    int roll(int sides) override { return sides; }
  } rng;
  const Result r = rollNotation(rng, "1d6!");
  EXPECT_EQ(r.count, 1 + MAX_EXPLODE);
  EXPECT_TRUE(r.truncated);
  EXPECT_EQ(r.total, 6 * (1 + MAX_EXPLODE));
}

TEST(DicePolyhedral, ExplodingKeepCountsWholeChain) {
  ScriptRng rng{6, 1, 5};
  const Result r = rollNotation(rng, "2d6!kh1");
  EXPECT_EQ(r.total, 7);  // chain 6+1 beats 5
  EXPECT_TRUE(r.dice[0].has(FLAG_KEPT));
  EXPECT_TRUE(r.dice[1].has(FLAG_KEPT));
  EXPECT_FALSE(r.dice[2].has(FLAG_KEPT));
}

TEST(DicePolyhedral, D100RangeWithSeed) {
  Xorshift32 rng(7);
  for (int i = 0; i < 200; ++i) {
    const Result r = rollNotation(rng, "d%");
    EXPECT_GE(r.total, 1);
    EXPECT_LE(r.total, 100);
  }
}

// ---- oWoD ----

TEST(DiceWod, BotchNeedsNoSuccessAndAOne) {
  ScriptRng rng{1, 3, 5};
  WodParams p;
  p.pool = 3;
  p.difficulty = 6;
  const Result r = rollWod(rng, p);
  EXPECT_EQ(r.outcome, Outcome::Botch);
  EXPECT_EQ(r.successes, 0);
  EXPECT_EQ(r.ones, 1);
  EXPECT_TRUE(r.dice[0].has(FLAG_ONE));
}

TEST(DiceWod, OnesCancelButCancelledSuccessIsFailureNotBotch) {
  ScriptRng rng{1, 7, 2};
  WodParams p;
  p.pool = 3;
  const Result r = rollWod(rng, p);
  EXPECT_EQ(r.outcome, Outcome::Failure);
  EXPECT_EQ(r.successes, 0);
}

TEST(DiceWod, NetSuccessesAndDegrees) {
  WodParams p;
  p.pool = 6;
  p.difficulty = 6;
  {
    ScriptRng rng{6, 7, 8, 9, 10, 2};
    const Result r = rollWod(rng, p);
    EXPECT_EQ(r.successes, 5);
    EXPECT_EQ(r.outcome, Outcome::PhenomenalSuccess);
  }
  {
    ScriptRng rng{6, 7, 8, 9, 1, 2};
    const Result r = rollWod(rng, p);
    EXPECT_EQ(r.successes, 3);
    EXPECT_EQ(r.outcome, Outcome::Success);
  }
  {
    ScriptRng rng{6, 7, 8, 9, 3, 2};
    EXPECT_EQ(rollWod(rng, p).outcome, Outcome::ExceptionalSuccess);
  }
  {
    ScriptRng rng{6, 7, 8, 9, 3, 2};
    p.difficulty = 8;
    const Result r = rollWod(rng, p);
    EXPECT_EQ(r.successes, 2);
    EXPECT_EQ(r.target, 8);
  }
}

TEST(DiceWod, WillpowerNeverBotches) {
  ScriptRng rng{1, 1, 2};
  WodParams p;
  p.pool = 3;
  p.willpower = true;
  const Result r = rollWod(rng, p);
  EXPECT_EQ(r.successes, 1);
  EXPECT_EQ(r.outcome, Outcome::Success);
}

TEST(DiceWod, SpecialtyTensExplodeAndChain) {
  // Pool 10, 10, 4 -> two extra dice: 10 (chains) and 3; chained extra: 7.
  ScriptRng rng{10, 10, 4, 10, 3, 7};
  WodParams p;
  p.pool = 3;
  p.specialty = true;
  const Result r = rollWod(rng, p);
  EXPECT_EQ(r.count, 6);
  EXPECT_EQ(countFlag(r, FLAG_EXTRA), 3);
  EXPECT_EQ(countFlag(r, FLAG_EXPLODED), 3);
  EXPECT_EQ(r.successes, 4);  // 10, 10, extra 10, extra 7
  EXPECT_EQ(rng.used(), 6u);
}

TEST(DiceWod, RerolledOnesCancelOnlyForMage) {
  WodParams p;
  p.pool = 2;
  p.specialty = true;
  {
    ScriptRng rng{10, 7, 1};
    const Result r = rollWod(rng, p);
    EXPECT_EQ(r.successes, 2);  // Vampire/Werewolf: the extra 1 cancels nothing
    EXPECT_EQ(r.ones, 0);
  }
  {
    ScriptRng rng{10, 7, 1};
    p.rerollOnesCancel = true;
    const Result r = rollWod(rng, p);
    EXPECT_EQ(r.successes, 1);
    EXPECT_EQ(r.ones, 0);  // base-pool 1s only
  }
}

TEST(DiceWod, RejectsBadParams) {
  Xorshift32 rng(3);
  WodParams p;
  p.pool = 0;
  EXPECT_FALSE(rollWod(rng, p).ok);
  p.pool = 5;
  p.difficulty = 1;
  EXPECT_FALSE(rollWod(rng, p).ok);
  p.difficulty = 11;
  EXPECT_EQ(rollWod(rng, p).error, ParseError::BadParams);
}

// ---- V5 ----

TEST(DiceV5, HungerDiceComeLastAndAreFlagged) {
  ScriptRng rng{2, 7, 9, 4};
  V5Params p;
  p.pool = 4;
  p.hunger = 2;
  const Result r = rollV5(rng, p);
  EXPECT_EQ(countFlag(r, FLAG_HUNGER), 2);
  EXPECT_FALSE(r.dice[0].has(FLAG_HUNGER));
  EXPECT_TRUE(r.dice[2].has(FLAG_HUNGER));
  EXPECT_TRUE(r.dice[3].has(FLAG_HUNGER));
  EXPECT_EQ(r.successes, 2);
  EXPECT_EQ(r.outcome, Outcome::Success);
  EXPECT_EQ(r.margin, 1);
}

TEST(DiceV5, PairOfTensIsFourSuccesses) {
  ScriptRng rng{10, 10, 10, 2};
  V5Params p;
  p.pool = 4;
  p.difficulty = 3;
  const Result r = rollV5(rng, p);
  EXPECT_EQ(r.pairs, 1);
  EXPECT_EQ(r.successes, 5);  // 3 base + 2 for the pair
  EXPECT_EQ(r.outcome, Outcome::CriticalSuccess);
  EXPECT_EQ(countFlag(r, FLAG_CRIT), 3);
}

TEST(DiceV5, MessyCriticalWhenAHungerTenIsInThePair) {
  ScriptRng rng{10, 3, 10};
  V5Params p;
  p.pool = 3;
  p.hunger = 1;
  p.difficulty = 2;
  const Result r = rollV5(rng, p);
  EXPECT_EQ(r.successes, 4);
  EXPECT_EQ(r.outcome, Outcome::MessyCritical);
}

TEST(DiceV5, CriticalOnlyWhenTheRollWins) {
  ScriptRng rng{10, 10, 2};
  V5Params p;
  p.pool = 3;
  p.difficulty = 5;
  const Result r = rollV5(rng, p);
  EXPECT_EQ(r.successes, 4);
  EXPECT_EQ(r.outcome, Outcome::Failure);
  EXPECT_EQ(r.margin, -1);
}

TEST(DiceV5, BestialFailureNeedsAHungerOneOnAFailedRoll) {
  V5Params p;
  p.pool = 3;
  p.hunger = 1;
  p.difficulty = 2;
  {
    ScriptRng rng{6, 2, 1};
    const Result r = rollV5(rng, p);
    EXPECT_EQ(r.outcome, Outcome::BestialFailure);
    EXPECT_EQ(r.ones, 1);
    EXPECT_TRUE(r.dice[2].has(FLAG_ONE));
  }
  {
    ScriptRng rng{1, 2, 3};  // the 1 is a normal die
    EXPECT_EQ(rollV5(rng, p).outcome, Outcome::TotalFailure);
  }
  {
    ScriptRng rng{6, 2, 2};
    EXPECT_EQ(rollV5(rng, p).outcome, Outcome::Failure);
  }
  {
    ScriptRng rng{6, 6, 1};  // wins, Hunger 1 does not matter
    EXPECT_EQ(rollV5(rng, p).outcome, Outcome::Success);
  }
}

TEST(DiceV5, ZeroSuccessesNeverWinsAtDifficultyZero) {
  ScriptRng rng{2, 3};
  V5Params p;
  p.pool = 2;
  p.difficulty = 0;
  EXPECT_EQ(rollV5(rng, p).outcome, Outcome::TotalFailure);
}

TEST(DiceV5, HungerIsClampedToThePool) {
  ScriptRng rng{7, 7};
  V5Params p;
  p.pool = 2;
  p.hunger = 5;
  EXPECT_EQ(countFlag(rollV5(rng, p), FLAG_HUNGER), 2);
}

TEST(DiceV5, WillpowerRerollsUpToThreeFailingNormalDice) {
  ScriptRng rng{2, 3, 4, 5, 1, 10, 10, 10};
  V5Params p;
  p.pool = 5;
  p.hunger = 1;
  p.difficulty = 3;
  const Result first = rollV5(rng, p);
  EXPECT_EQ(first.outcome, Outcome::BestialFailure);
  const Result second = rerollV5Willpower(rng, first);
  ASSERT_TRUE(second.ok);
  EXPECT_EQ(countFlag(second, FLAG_REROLLED), 3);
  EXPECT_FALSE(second.dice[3].has(FLAG_REROLLED));  // the 5 stays, lowest three were picked
  EXPECT_FALSE(second.dice[4].has(FLAG_REROLLED));  // Hunger is never rerolled
  EXPECT_EQ(second.successes, 5);                   // 10, 10, 10 -> 3 base + one pair
  EXPECT_EQ(second.outcome, Outcome::CriticalSuccess);
  EXPECT_EQ(rng.used(), 8u);
}

TEST(DiceV5, WillpowerWithNothingToRerollIsUnchanged) {
  ScriptRng rng{8, 9, 7};
  V5Params p;
  p.pool = 3;
  const Result first = rollV5(rng, p);
  const Result second = rerollV5Willpower(rng, first);
  EXPECT_EQ(values(second), values(first));
  EXPECT_EQ(countFlag(second, FLAG_REROLLED), 0);
  EXPECT_EQ(rng.used(), 3u);
}

TEST(DiceV5, WillpowerRejectsOtherSystems) {
  ScriptRng rng{3};
  const Result fate = rollFate(rng, 0);
  EXPECT_FALSE(rerollV5Willpower(rng, fate).ok);
}

TEST(DiceV5, RouseCheck) {
  {
    ScriptRng rng{6};
    const Result r = rollRouse(rng, 2);
    EXPECT_EQ(r.outcome, Outcome::Success);
    EXPECT_EQ(r.total, 2);
    EXPECT_TRUE(r.dice[0].has(FLAG_HUNGER));
  }
  {
    ScriptRng rng{5};
    const Result r = rollRouse(rng, 2);
    EXPECT_EQ(r.outcome, Outcome::Failure);
    EXPECT_EQ(r.total, 3);
    EXPECT_EQ(r.margin, 0);
  }
  {
    ScriptRng rng{1};
    const Result r = rollRouse(rng, 5);
    EXPECT_EQ(r.total, 5);
    EXPECT_EQ(r.margin, 1);  // at max Hunger: frenzy test
  }
}

// ---- Chronicles of Darkness ----

TEST(DiceCod, TenAgainChainsAndCountsEights) {
  ScriptRng rng{10, 8, 3, 10, 7};  // each 10 is followed by its extra die
  CodParams p;
  p.pool = 3;
  const Result r = rollCod(rng, p);
  EXPECT_EQ(r.count, 5);
  EXPECT_EQ(values(r), (std::vector<int>{10, 8, 3, 10, 7}));
  EXPECT_EQ(r.successes, 3);
  EXPECT_EQ(r.outcome, Outcome::Success);
  EXPECT_TRUE(r.dice[0].has(FLAG_EXPLODED));
  EXPECT_TRUE(r.dice[1].has(FLAG_EXTRA));
  EXPECT_TRUE(r.dice[3].has(FLAG_EXPLODED));
  EXPECT_TRUE(r.dice[4].has(FLAG_EXTRA));
}

TEST(DiceCod, NineAgainAndNoAgain) {
  CodParams p;
  p.pool = 2;
  {
    ScriptRng rng{9, 2, 8};
    p.again = 9;
    const Result r = rollCod(rng, p);
    EXPECT_EQ(r.count, 3);
    EXPECT_EQ(r.successes, 2);
  }
  {
    ScriptRng rng{10, 10};
    p.again = 0;
    const Result r = rollCod(rng, p);
    EXPECT_EQ(r.count, 2);
    EXPECT_EQ(r.successes, 2);
  }
}

TEST(DiceCod, RoteRerollsFailuresOnce) {
  ScriptRng rng{3, 9, 5, 2};  // 3 -> reroll 9; 5 -> reroll 2 (stays failed)
  CodParams p;
  p.pool = 2;
  p.rote = true;
  p.again = 0;
  const Result r = rollCod(rng, p);
  EXPECT_EQ(r.count, 2);
  EXPECT_EQ(values(r), (std::vector<int>{9, 2}));
  EXPECT_EQ(countFlag(r, FLAG_REROLLED), 2);
  EXPECT_EQ(r.successes, 1);
}

TEST(DiceCod, ChanceDie) {
  CodParams p;
  p.pool = 0;
  {
    ScriptRng rng{10};
    const Result r = rollCod(rng, p);
    EXPECT_EQ(r.successes, 1);
    EXPECT_EQ(r.outcome, Outcome::Success);
    EXPECT_EQ(r.count, 1);
  }
  {
    ScriptRng rng{1};
    EXPECT_EQ(rollCod(rng, p).outcome, Outcome::DramaticFailure);
  }
  {
    ScriptRng rng{8};
    EXPECT_EQ(rollCod(rng, p).outcome, Outcome::Failure);
  }
}

TEST(DiceCod, ExceptionalAtFive) {
  ScriptRng rng{8, 8, 8, 8, 9};
  CodParams p;
  p.pool = 5;
  EXPECT_EQ(rollCod(rng, p).outcome, Outcome::ExceptionalSuccess);
}

// ---- Fate ----

TEST(DiceFate, FourFudgeDiceRange) {
  {
    ScriptRng rng{1, 1, 1, 1};
    const Result r = rollFate(rng, 0);
    EXPECT_EQ(r.total, -4);
    EXPECT_EQ(r.dice[0].value, -1);
  }
  {
    ScriptRng rng{3, 3, 3, 3};
    EXPECT_EQ(rollFate(rng, 2).total, 6);
  }
  Xorshift32 rng(11);
  for (int i = 0; i < 500; ++i) {
    const Result r = rollFate(rng, 0);
    EXPECT_GE(r.total, -4);
    EXPECT_LE(r.total, 4);
    EXPECT_EQ(r.count, 4);
  }
}

// ---- PbtA ----

TEST(DicePbta, HitBands) {
  {
    ScriptRng rng{5, 5};
    const Result r = rollPbta(rng, 0);
    EXPECT_EQ(r.total, 10);
    EXPECT_EQ(r.outcome, Outcome::StrongHit);
  }
  {
    ScriptRng rng{3, 3};
    EXPECT_EQ(rollPbta(rng, 1).outcome, Outcome::WeakHit);
  }
  {
    ScriptRng rng{3, 3};
    EXPECT_EQ(rollPbta(rng, -1).outcome, Outcome::Miss);
  }
}

// ---- Blades ----

TEST(DiceBlades, HighestDieDecides) {
  {
    ScriptRng rng{2, 6, 3};
    const Result r = rollBlades(rng, 3);
    EXPECT_EQ(r.outcome, Outcome::Full);
    EXPECT_EQ(r.total, 6);
    EXPECT_TRUE(r.dice[1].has(FLAG_KEPT));
  }
  {
    ScriptRng rng{4, 2};
    EXPECT_EQ(rollBlades(rng, 2).outcome, Outcome::Partial);
  }
  {
    ScriptRng rng{3};
    EXPECT_EQ(rollBlades(rng, 1).outcome, Outcome::Bad);
  }
  {
    ScriptRng rng{6, 6, 1};
    const Result r = rollBlades(rng, 3);
    EXPECT_EQ(r.outcome, Outcome::CriticalSuccess);
    EXPECT_EQ(countFlag(r, FLAG_CRIT), 2);
  }
}

TEST(DiceBlades, ZeroDiceRollsTwoKeepsLowestNoCrit) {
  {
    ScriptRng rng{6, 2};
    const Result r = rollBlades(rng, 0);
    EXPECT_EQ(r.count, 2);
    EXPECT_EQ(r.total, 2);
    EXPECT_EQ(r.outcome, Outcome::Bad);
  }
  {
    ScriptRng rng{6, 6};
    EXPECT_EQ(rollBlades(rng, 0).outcome, Outcome::Full);
  }
}

// ---- Call of Cthulhu ----

TEST(DiceCoc, SuccessLevels) {
  CocParams p;
  p.skill = 50;
  {
    ScriptRng rng{1, 1};  // units 0, tens 00 -> 100
    const Result r = rollCoc(rng, p);
    EXPECT_EQ(r.total, 100);
    EXPECT_EQ(r.outcome, Outcome::Fumble);
  }
  {
    ScriptRng rng{2, 1};  // units 1, tens 00 -> 01
    const Result r = rollCoc(rng, p);
    EXPECT_EQ(r.total, 1);
    EXPECT_EQ(r.outcome, Outcome::Critical);
  }
  {
    ScriptRng rng{1, 2};  // units 0, tens 10 -> 10 <= 50/5
    EXPECT_EQ(rollCoc(rng, p).outcome, Outcome::Extreme);
  }
  {
    ScriptRng rng{6, 3};  // 25 <= 50/2
    EXPECT_EQ(rollCoc(rng, p).outcome, Outcome::Hard);
  }
  {
    ScriptRng rng{9, 5};  // 48
    EXPECT_EQ(rollCoc(rng, p).outcome, Outcome::Regular);
  }
  {
    ScriptRng rng{7, 10};  // 96 at skill 50: plain failure
    EXPECT_EQ(rollCoc(rng, p).outcome, Outcome::Failure);
  }
  {
    ScriptRng rng{7, 10};  // 96 under 50: fumble
    p.skill = 49;
    EXPECT_EQ(rollCoc(rng, p).outcome, Outcome::Fumble);
  }
}

TEST(DiceCoc, BonusDieKeepsLowestTens) {
  ScriptRng rng{5, 8, 3};  // units 4, tens 70 and 20
  CocParams p;
  p.skill = 60;
  p.bonusDice = 1;
  const Result r = rollCoc(rng, p);
  EXPECT_EQ(r.count, 3);
  EXPECT_EQ(r.total, 24);
  EXPECT_TRUE(r.dice[1].has(FLAG_KEPT));
  EXPECT_TRUE(r.dice[1].has(FLAG_EXTRA));
  EXPECT_FALSE(r.dice[0].has(FLAG_KEPT));
  EXPECT_EQ(r.outcome, Outcome::Hard);
}

TEST(DiceCoc, PenaltyDieKeepsHighestAndZeroTensIsHundredOnlyWithZeroUnits) {
  {
    ScriptRng rng{5, 8, 3};
    CocParams p;
    p.skill = 60;
    p.penaltyDice = 1;
    const Result r = rollCoc(rng, p);
    EXPECT_EQ(r.total, 74);
    EXPECT_EQ(r.outcome, Outcome::Failure);
  }
  {
    ScriptRng rng{1, 1, 4};  // units 0: tens 00 = 100 is the highest
    CocParams p;
    p.penaltyDice = 1;
    EXPECT_EQ(rollCoc(rng, p).total, 100);
  }
  {
    ScriptRng rng{1, 1, 4};  // bonus keeps the 30 instead
    CocParams p;
    p.bonusDice = 1;
    EXPECT_EQ(rollCoc(rng, p).total, 30);
  }
}

TEST(DiceCoc, BonusAndPenaltyCancel) {
  ScriptRng rng{5, 8};
  CocParams p;
  p.bonusDice = 2;
  p.penaltyDice = 2;
  const Result r = rollCoc(rng, p);
  EXPECT_EQ(r.count, 2);
  EXPECT_EQ(r.total, 74);
}

// ---- Shadowrun ----

TEST(DiceShadowrun, HitsAndGlitches) {
  {
    ScriptRng rng{5, 6, 2, 3};
    const Result r = rollShadowrun(rng, 4);
    EXPECT_EQ(r.successes, 2);
    EXPECT_EQ(r.outcome, Outcome::Success);
  }
  {
    ScriptRng rng{1, 1, 5, 3};  // two 1s of four: glitch with hits
    const Result r = rollShadowrun(rng, 4);
    EXPECT_EQ(r.outcome, Outcome::Glitch);
    EXPECT_EQ(r.ones, 2);
  }
  {
    ScriptRng rng{1, 1, 2};
    EXPECT_EQ(rollShadowrun(rng, 3).outcome, Outcome::CriticalGlitch);
  }
  {
    ScriptRng rng{1, 2, 3, 4};  // one 1 of four: no glitch
    EXPECT_EQ(rollShadowrun(rng, 4).outcome, Outcome::Failure);
  }
}

// ---- Savage Worlds ----

TEST(DiceSavage, AcesKeepBetterAndRaises) {
  ScriptRng rng{8, 8, 3, 2};  // trait d8 aces twice: 19; wild 2
  SavageParams p;
  p.traitSides = 8;
  p.modifier = 1;
  const Result r = rollSavage(rng, p);
  EXPECT_EQ(r.count, 4);
  EXPECT_EQ(r.total, 20);
  EXPECT_EQ(r.margin, 4);  // (20-4)/4 raises
  EXPECT_EQ(r.outcome, Outcome::Raise);
  EXPECT_TRUE(r.dice[0].has(FLAG_KEPT));
  EXPECT_TRUE(r.dice[2].has(FLAG_KEPT));
  EXPECT_TRUE(r.dice[3].has(FLAG_WILD));
  EXPECT_FALSE(r.dice[3].has(FLAG_KEPT));
}

TEST(DiceSavage, WildDieWinsSnakeEyesFails) {
  {
    ScriptRng rng{2, 6, 1};  // trait 2; wild 6+1 = 7
    SavageParams p;
    const Result r = rollSavage(rng, p);
    EXPECT_EQ(r.total, 7);
    EXPECT_EQ(r.outcome, Outcome::Success);
    EXPECT_FALSE(r.dice[0].has(FLAG_KEPT));
    EXPECT_TRUE(r.dice[1].has(FLAG_KEPT));
  }
  {
    ScriptRng rng{1, 1};
    SavageParams p;
    EXPECT_EQ(rollSavage(rng, p).outcome, Outcome::CriticalFailure);
  }
  {
    ScriptRng rng{3};
    SavageParams p;
    p.wildDie = false;
    const Result r = rollSavage(rng, p);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.outcome, Outcome::Failure);
  }
}

// ---- Year Zero ----

TEST(DiceYearZero, SuccessesBanesAndPush) {
  ScriptRng rng{6, 1, 3, 4, 6, 2};
  const Result first = rollYearZero(rng, 4);
  EXPECT_EQ(first.successes, 1);
  EXPECT_EQ(first.ones, 1);
  EXPECT_EQ(first.outcome, Outcome::Success);
  const Result pushed = pushYearZero(rng, first);
  EXPECT_EQ(values(pushed), (std::vector<int>{6, 1, 6, 2}));
  EXPECT_EQ(countFlag(pushed, FLAG_REROLLED), 2);
  EXPECT_EQ(pushed.successes, 2);
  EXPECT_EQ(rng.used(), 6u);
}

// ---- Cyberpunk Red ----

TEST(DiceCyberpunk, ExplodesUpAndDown) {
  {
    ScriptRng rng{10, 4};
    CyberpunkParams p;
    p.modifier = 5;
    const Result r = rollCyberpunk(rng, p);
    EXPECT_EQ(r.total, 19);
    EXPECT_EQ(r.outcome, Outcome::CriticalSuccess);
    EXPECT_TRUE(r.dice[0].has(FLAG_CRIT));
  }
  {
    ScriptRng rng{1, 7};
    CyberpunkParams p;
    p.modifier = 5;
    const Result r = rollCyberpunk(rng, p);
    EXPECT_EQ(r.total, -1);
    EXPECT_EQ(r.dice[1].value, -7);
    EXPECT_EQ(r.outcome, Outcome::CriticalFailure);
  }
  {
    ScriptRng rng{6};
    CyberpunkParams p;
    p.modifier = 10;
    p.dv = 15;
    const Result r = rollCyberpunk(rng, p);
    EXPECT_EQ(r.total, 16);
    EXPECT_EQ(r.outcome, Outcome::Success);
    EXPECT_EQ(r.margin, 1);
  }
  {
    ScriptRng rng{5};
    CyberpunkParams p;
    p.modifier = 10;
    p.dv = 15;
    EXPECT_EQ(rollCyberpunk(rng, p).outcome, Outcome::Failure);  // ties lose
  }
}

// ---- keys ----

TEST(DiceOutcome, KeysAreStable) {
  EXPECT_STREQ(outcomeKey(Outcome::MessyCritical), "messy_critical");
  EXPECT_STREQ(outcomeKey(Outcome::Botch), "botch");
  EXPECT_STREQ(outcomeKey(Outcome::Raise), "raise");
  EXPECT_STREQ(outcomeKey(Outcome::None), "none");
}

}  // namespace
