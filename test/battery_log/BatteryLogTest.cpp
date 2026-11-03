#include <BatteryLog.h>
#include <gtest/gtest.h>

using namespace batterylog;

namespace {
Record rec(int64_t t, bool sleep, int pct, bool usb = false) {
  Record r;
  r.epoch = t;
  r.sleep = sleep;
  r.percent = static_cast<uint8_t>(pct);
  r.usb = usb;
  return r;
}
constexpr int64_t H = 3600;
}  // namespace

TEST(BatteryLog, LineRoundTrip) {
  Record out;
  ASSERT_TRUE(fromLine(toLine(rec(1790000000, true, 87, true)), out));
  EXPECT_EQ(out.epoch, 1790000000);
  EXPECT_TRUE(out.sleep);
  EXPECT_EQ(out.percent, 87);
  EXPECT_TRUE(out.usb);
}

TEST(BatteryLog, BadLinesRejected) {
  Record out;
  EXPECT_FALSE(fromLine("", out));
  EXPECT_FALSE(fromLine("100\tX\t50\t0", out));
  EXPECT_FALSE(fromLine("100\tS\t101\t0", out));
  EXPECT_FALSE(fromLine("0\tS\t50\t0", out));
  EXPECT_FALSE(fromLine("100\tS\t50", out));
  EXPECT_FALSE(fromLine("100\tS\t50\t2", out));
}

TEST(BatteryLog, AsleepRateSumsPairs) {
  std::vector<Record> v;
  append(v, rec(1000, true, 90));
  append(v, rec(1000 + 10 * H, false, 89));  // 1 % in 10 h
  append(v, rec(1000 + 11 * H, true, 89));
  append(v, rec(1000 + 21 * H, false, 87));  // 2 % in 10 h
  const Stats s = compute(v);
  ASSERT_TRUE(s.asleep.known);
  EXPECT_EQ(s.asleep.hundredthsPerHour, 15u);  // 3 % in 20 h = 0.15 %/h
  EXPECT_EQ(s.asleep.seconds, static_cast<uint32_t>(20 * H));
  ASSERT_FALSE(s.awake.known);  // one hour awake, no drop yet
}

TEST(BatteryLog, AwakeRate) {
  std::vector<Record> v;
  append(v, rec(1000, false, 80));
  append(v, rec(1000 + 2 * H, true, 76));
  const Stats s = compute(v);
  ASSERT_TRUE(s.awake.known);
  EXPECT_EQ(s.awake.hundredthsPerHour, 200u);
  EXPECT_FALSE(s.asleep.known);
}

TEST(BatteryLog, UsbRiseAndGapsAreSkipped) {
  std::vector<Record> v;
  append(v, rec(1000, true, 50));
  append(v, rec(1000 + 8 * H, false, 60));       // percent rose: charged
  append(v, rec(1000 + 9 * H, true, 60, true));  // USB at the start
  append(v, rec(1000 + 20 * H, false, 58));
  append(v, rec(1000 + 21 * H, true, 58));
  append(v, rec(1000 + 21 * H + 15 * 24 * H, false, 40));  // over 14 days
  const Stats s = compute(v);
  EXPECT_FALSE(s.asleep.known);
}

TEST(BatteryLog, MissingWakeIsNotPaired) {
  std::vector<Record> v;
  append(v, rec(1000, true, 90));
  append(v, rec(1000 + 8 * H, true, 85));  // second sleep: the wake was never logged
  append(v, rec(1000 + 16 * H, false, 84));
  const Stats s = compute(v);
  ASSERT_TRUE(s.asleep.known);
  EXPECT_EQ(s.asleep.hundredthsPerHour, 13u);  // only the last pair: 1 % in 8 h = 0.125, rounds to 13
}

TEST(BatteryLog, ZeroDropShowsOnlyAfterSixHours) {
  std::vector<Record> v;
  append(v, rec(1000, true, 90));
  append(v, rec(1000 + 3 * H, false, 90));
  EXPECT_FALSE(compute(v).asleep.known);
  append(v, rec(1000 + 4 * H, true, 90));
  append(v, rec(1000 + 8 * H, false, 90));
  const Stats s = compute(v);
  ASSERT_TRUE(s.asleep.known);
  EXPECT_EQ(s.asleep.hundredthsPerHour, 0u);
}

TEST(BatteryLog, RingKeepsNewest) {
  std::vector<Record> v;
  for (int i = 0; i < 200; ++i) append(v, rec(1000 + i, i % 2 == 0, 50));
  EXPECT_EQ(v.size(), MAX_RECORDS);
  EXPECT_EQ(v.back().epoch, 1000 + 199);
}

TEST(BatteryLog, OldLinesHaveNoLightField) {
  Record out;
  ASSERT_TRUE(fromLine("100\tS\t50\t0", out));
  EXPECT_FALSE(out.lightKnown);
  ASSERT_TRUE(fromLine("100\tS\t50\t0\t1", out));
  EXPECT_TRUE(out.lightKnown);
  EXPECT_TRUE(out.light);
  EXPECT_FALSE(fromLine("100\tS\t50\t0\t2", out));
  ASSERT_TRUE(fromLine(toLine(rec(100, false, 50)), out));
  EXPECT_TRUE(out.lightKnown);
  EXPECT_FALSE(out.light);
}

TEST(BatteryLog, AwakeRateSplitsByLight) {
  auto sleepLine = [](int64_t t, int pct, bool light) {
    Record r = rec(t, true, pct);
    r.lightKnown = true;
    r.light = light;
    return r;
  };
  std::vector<Record> v;
  append(v, rec(1000, false, 90));
  append(v, sleepLine(1000 + 2 * H, 88, false));  // 2 % in 2 h, light off
  append(v, rec(1000 + 3 * H, false, 88));
  append(v, sleepLine(1000 + 5 * H, 82, true));  // 6 % in 2 h, light on
  const Stats s = compute(v);
  ASSERT_TRUE(s.awakeLightOff.known);
  ASSERT_TRUE(s.awakeLightOn.known);
  EXPECT_EQ(s.awakeLightOff.hundredthsPerHour, 100u);
  EXPECT_EQ(s.awakeLightOn.hundredthsPerHour, 300u);
  EXPECT_EQ(s.awake.hundredthsPerHour, 200u);
}

TEST(BatteryLog, AwakePairsWithoutLightInfoStayOutOfTheSplit) {
  std::vector<Record> v;
  append(v, rec(1000, false, 90));
  append(v, rec(1000 + 10 * H, true, 80));  // an old line: no light field
  const Stats s = compute(v);
  EXPECT_TRUE(s.awake.known);
  EXPECT_FALSE(s.awakeLightOff.known);
  EXPECT_FALSE(s.awakeLightOn.known);
}
