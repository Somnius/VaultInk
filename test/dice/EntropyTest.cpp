#include <gtest/gtest.h>

#include <bitset>
#include <set>

#include "DiceEntropy.h"

using dice::entropy::Noise;
using dice::entropy::seed;

namespace {
Noise base() {
  Noise n;
  n.hardware = 0x12345678;
  n.dateTime = 20261101111428ull;
  n.epochSeconds = 1793531668u;
  n.micros = 123456;
  n.millis = 98765;
  n.batteryPercent = 87;
  n.batteryMillivolts = 3912;
  n.counter = 1;
  return n;
}
}  // namespace

TEST(DiceEntropy, TheSameNoiseGivesTheSameSeed) { EXPECT_EQ(seed(base()), seed(base())); }

TEST(DiceEntropy, EveryPartChangesTheSeed) {
  const uint32_t reference = seed(base());
  Noise n = base();
  n.hardware ^= 1;
  EXPECT_NE(seed(n), reference);
  n = base();
  n.dateTime += 1;
  EXPECT_NE(seed(n), reference);
  n = base();
  n.dateTime += 1ull << 40;  // the high half counts too
  EXPECT_NE(seed(n), reference);
  n = base();
  n.epochSeconds += 1;
  EXPECT_NE(seed(n), reference);
  n = base();
  n.micros += 1;
  EXPECT_NE(seed(n), reference);
  n = base();
  n.millis += 1;
  EXPECT_NE(seed(n), reference);
  n = base();
  n.batteryPercent = 86;
  EXPECT_NE(seed(n), reference);
  n = base();
  n.batteryMillivolts = 3913;
  EXPECT_NE(seed(n), reference);
  n = base();
  n.counter = 2;
  EXPECT_NE(seed(n), reference);
}

TEST(DiceEntropy, OneStepOfOnePartMovesAboutHalfTheBits) {
  // The avalanche check: a one-millivolt change (the smallest change of any input) flips many seed bits.
  int total = 0;
  const int runs = 200;
  for (int i = 0; i < runs; ++i) {
    Noise n = base();
    n.batteryMillivolts = 3000 + i;
    Noise m = n;
    m.batteryMillivolts += 1;
    total += static_cast<int>(std::bitset<32>(seed(n) ^ seed(m)).count());
  }
  const double mean = static_cast<double>(total) / runs;
  EXPECT_GT(mean, 12.0);
  EXPECT_LT(mean, 20.0);
}

TEST(DiceEntropy, RollsInTheSameInstantStillDiffer) {
  std::set<uint32_t> seeds;
  Noise n = base();
  for (uint32_t i = 0; i < 1000; ++i) {
    n.counter = i;  // everything else frozen: the same second, battery and voltage
    seeds.insert(seed(n));
  }
  EXPECT_EQ(seeds.size(), 1000u);
}
