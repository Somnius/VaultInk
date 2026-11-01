#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "Habits.h"

using namespace habits;

namespace {

constexpr Day D_2024_02_29 = 1520;
constexpr Day D_2026_11_01 = 2496;

std::vector<Day> days(const HabitList& l, size_t i) {
  return std::vector<Day>(l.habits[i].done, l.habits[i].done + l.habits[i].count);
}

}  // namespace

// ---- days ----

TEST(HabitsDays, KnownDates) {
  EXPECT_EQ(dayFromDate(2020, 1, 1), 0);
  EXPECT_EQ(dayFromDate(2020, 1, 2), 1);
  EXPECT_EQ(dayFromDate(2020, 12, 31), 365);  // 2020 is a leap year
  EXPECT_EQ(dayFromDate(2024, 2, 29), D_2024_02_29);
  EXPECT_EQ(dayFromDate(2024, 3, 1), D_2024_02_29 + 1);
  EXPECT_EQ(dayFromDate(2026, 11, 1), D_2026_11_01);
  EXPECT_EQ(dayFromDate(2019, 12, 31), 0);  // before the epoch clamps
  EXPECT_EQ(dayFromDate(2300, 1, 1), 65535);

  const Date leap = dateFromDay(D_2024_02_29);
  EXPECT_EQ(leap.year, 2024);
  EXPECT_EQ(leap.month, 2);
  EXPECT_EQ(leap.day, 29);
  const Date nov = dateFromDay(D_2026_11_01);
  EXPECT_EQ(nov.year, 2026);
  EXPECT_EQ(nov.month, 11);
  EXPECT_EQ(nov.day, 1);
  const Date zero = dateFromDay(0);
  EXPECT_EQ(zero.year, 2020);
  EXPECT_EQ(zero.month, 1);
  EXPECT_EQ(zero.day, 1);
}

TEST(HabitsDays, RoundTripEveryDayForTenYears) {
  for (Day d = 0; d < 3653; ++d) {
    const Date date = dateFromDay(d);
    ASSERT_EQ(dayFromDate(date), d) << date.year << "-" << date.month << "-" << date.day;
    ASSERT_GE(date.month, 1);
    ASSERT_LE(date.month, 12);
    ASSERT_GE(date.day, 1);
    ASSERT_LE(date.day, daysInMonth(date.year, date.month));
  }
}

TEST(HabitsDays, Epoch) {
  EXPECT_EQ(dayFromEpoch(1577836800), 0);          // 2020-01-01T00:00:00Z
  EXPECT_EQ(dayFromEpoch(1577836800 + 86399), 0);  // last second of day 0
  EXPECT_EQ(dayFromEpoch(1577836800 + 86400), 1);
  EXPECT_EQ(dayFromEpoch(1577836799), 0);  // 2019 clamps to 0
  EXPECT_EQ(dayFromEpoch(-1), 0);
  EXPECT_EQ(dayFromEpoch(1709164800), D_2024_02_29);  // 2024-02-29T00:00:00Z
  EXPECT_EQ(dayFromEpoch(1793491200), D_2026_11_01);  // 2026-11-01T00:00:00Z
  const Date d = epochToDate(1793491200 + 3600 * 23);
  EXPECT_EQ(d.year, 2026);
  EXPECT_EQ(d.month, 11);
  EXPECT_EQ(d.day, 1);
}

TEST(HabitsDays, DayOfWeek) {
  EXPECT_EQ(dayOfWeek(0), 2);                        // 2020-01-01 Wednesday
  EXPECT_EQ(dayOfWeek(dayFromDate(2020, 1, 6)), 0);  // Monday
  EXPECT_EQ(dayOfWeek(D_2024_02_29), 3);             // Thursday
  EXPECT_EQ(dayOfWeek(D_2026_11_01), 6);             // Sunday
}

TEST(HabitsDays, LeapYears) {
  EXPECT_TRUE(isLeapYear(2020));
  EXPECT_TRUE(isLeapYear(2024));
  EXPECT_FALSE(isLeapYear(2023));
  EXPECT_FALSE(isLeapYear(2100));
  EXPECT_TRUE(isLeapYear(2000));
  EXPECT_EQ(daysInMonth(2024, 2), 29);
  EXPECT_EQ(daysInMonth(2025, 2), 28);
  EXPECT_EQ(dayFromDate(2024, 2, 31), dayFromDate(2024, 2, 29));  // clamped day
}

// ---- list ----

TEST(HabitsList, AddTrimsSanitisesAndRejects) {
  HabitList l;
  EXPECT_TRUE(l.add("  Read |a\nbook  "));
  EXPECT_STREQ(l.habits[0].name, "Read  a book");
  EXPECT_FALSE(l.add("Read  a book"));  // duplicate
  EXPECT_FALSE(l.add("   "));
  EXPECT_FALSE(l.add(""));
  EXPECT_FALSE(l.add(nullptr));
  EXPECT_FALSE(l.add("|\n|"));  // only separators -> empty after cleaning
  EXPECT_EQ(l.count, 1);
  EXPECT_EQ(findHabit(l, "Read  a book"), 0);
  EXPECT_EQ(findHabit(l, "nope"), -1);
}

TEST(HabitsList, LongNameCutOnUtf8Boundary) {
  HabitList l;
  // 30 ASCII bytes then a 2-byte "é": byte 31 would split it, so the cut lands before it.
  const std::string name = std::string(30, 'a') + "\xC3\xA9" + "zzz";
  EXPECT_TRUE(l.add(name.c_str()));
  EXPECT_EQ(std::strlen(l.habits[0].name), 30u);
  const std::string plain(50, 'b');
  EXPECT_TRUE(l.add(plain.c_str()));
  EXPECT_EQ(std::strlen(l.habits[1].name), kNameCapacity - 1);
}

TEST(HabitsList, FullListAndRemove) {
  HabitList l;
  for (size_t i = 0; i < HabitList::kMax; ++i) {
    ASSERT_TRUE(l.add(("h" + std::to_string(i)).c_str()));
  }
  EXPECT_FALSE(l.add("one more"));
  EXPECT_EQ(l.count, HabitList::kMax);
  l.markDone(3, 10);
  l.markDone(4, 20);
  EXPECT_TRUE(l.remove(3));
  EXPECT_EQ(l.count, HabitList::kMax - 1);
  EXPECT_STREQ(l.habits[3].name, "h4");
  EXPECT_TRUE(l.isDone(3, 20));
  EXPECT_FALSE(l.remove(HabitList::kMax));
  EXPECT_FALSE(l.remove(l.count));
  EXPECT_TRUE(l.add("one more"));
}

TEST(HabitsList, MarkUnmarkToggleKeepOrder) {
  HabitList l;
  l.add("x");
  EXPECT_TRUE(l.markDone(0, 50));
  EXPECT_TRUE(l.markDone(0, 10));
  EXPECT_TRUE(l.markDone(0, 30));
  EXPECT_FALSE(l.markDone(0, 30));  // repeat
  EXPECT_EQ(days(l, 0), (std::vector<Day>{10, 30, 50}));
  EXPECT_TRUE(l.unmark(0, 30));
  EXPECT_FALSE(l.unmark(0, 30));
  EXPECT_EQ(days(l, 0), (std::vector<Day>{10, 50}));
  EXPECT_TRUE(l.toggle(0, 30));
  EXPECT_FALSE(l.toggle(0, 30));
  EXPECT_EQ(l.totalDone(0), 2);
  EXPECT_FALSE(l.markDone(1, 5));  // bad index
  EXPECT_FALSE(l.isDone(1, 5));
  EXPECT_FALSE(l.toggle(7, 5));
  EXPECT_EQ(l.totalDone(7), 0);
}

TEST(HabitsList, FullHabitDropsOldest) {
  HabitList l;
  l.add("x");
  for (Day d = 100; d < 100 + kDaysPerHabit; ++d) {
    ASSERT_TRUE(l.markDone(0, d));
  }
  EXPECT_EQ(l.totalDone(0), kDaysPerHabit);
  EXPECT_TRUE(l.markDone(0, 1000));
  EXPECT_EQ(l.totalDone(0), kDaysPerHabit);
  EXPECT_FALSE(l.isDone(0, 100));
  EXPECT_TRUE(l.isDone(0, 101));
  EXPECT_TRUE(l.isDone(0, 1000));
  // Older than everything kept: not recorded, nothing dropped.
  EXPECT_FALSE(l.markDone(0, 5));
  EXPECT_TRUE(l.isDone(0, 101));
  EXPECT_EQ(l.totalDone(0), kDaysPerHabit);
  // Inserting in the middle of a full habit still drops the oldest.
  EXPECT_TRUE(l.markDone(0, 600));
  EXPECT_FALSE(l.isDone(0, 101));
  EXPECT_TRUE(l.isDone(0, 600));
}

TEST(HabitsList, StreakEdgeCases) {
  HabitList l;
  l.add("x");
  const Day today = D_2026_11_01;
  EXPECT_EQ(l.streak(0, today), 0);
  l.markDone(0, today - 1);
  l.markDone(0, today - 2);
  l.markDone(0, today - 3);
  EXPECT_EQ(l.streak(0, today), 3);  // today not done yet: streak ending yesterday still counts
  l.markDone(0, today);
  EXPECT_EQ(l.streak(0, today), 4);
  EXPECT_EQ(l.streak(0, today + 1), 4);  // tomorrow, before marking
  EXPECT_EQ(l.streak(0, today + 2), 0);  // one missed day breaks it
  l.unmark(0, today - 2);                // a gap inside
  EXPECT_EQ(l.streak(0, today), 2);
  EXPECT_EQ(l.streak(0, today - 3), 1);
  EXPECT_EQ(l.streak(0, 0), 0);
  EXPECT_EQ(l.streak(5, today), 0);  // bad index
}

TEST(HabitsList, StreakAcrossLeapDayAndYearBoundary) {
  HabitList l;
  l.add("x");
  for (Day d = dayFromDate(2024, 2, 27); d <= dayFromDate(2024, 3, 2); ++d) {
    l.markDone(0, d);
  }
  EXPECT_EQ(l.streak(0, dayFromDate(2024, 3, 2)), 5);  // 27, 28, 29, 1, 2
  l.add("y");
  for (Day d = dayFromDate(2025, 12, 30); d <= dayFromDate(2026, 1, 2); ++d) {
    l.markDone(1, d);
  }
  EXPECT_EQ(l.streak(1, dayFromDate(2026, 1, 2)), 4);
  EXPECT_EQ(l.streak(1, dayFromDate(2026, 1, 3)), 4);
  EXPECT_EQ(l.streak(1, dayFromDate(2026, 1, 4)), 0);
}

TEST(HabitsList, BestStreakAndDoneInLast) {
  HabitList l;
  l.add("x");
  EXPECT_EQ(l.bestStreak(0), 0);
  for (Day d : {10, 11, 12, 20, 21, 22, 23, 24, 30}) {
    l.markDone(0, static_cast<Day>(d));
  }
  EXPECT_EQ(l.bestStreak(0), 5);
  EXPECT_EQ(l.streak(0, 30), 1);
  EXPECT_EQ(l.doneInLast(0, 30, 1), 1);
  EXPECT_EQ(l.doneInLast(0, 30, 7), 2);   // 24..30
  EXPECT_EQ(l.doneInLast(0, 30, 11), 6);  // 20..30
  EXPECT_EQ(l.doneInLast(0, 30, 0), 0);
  EXPECT_EQ(l.doneInLast(0, 31, 1), 0);
  EXPECT_EQ(l.doneInLast(0, 5, 100), 0);  // window clamped at day 0
  EXPECT_EQ(l.doneInLast(0, 12, 100), 3);
  EXPECT_EQ(l.doneInLast(0, 65535, 65535), 9);
  EXPECT_EQ(l.bestStreak(3), 0);
}

// ---- file ----

TEST(HabitsFile, SerializeParseRoundTrip) {
  HabitList l;
  l.add("Read");
  l.add("Walk the dog");
  l.add("Empty one");
  for (Day d : {5, 6, 7, 100, 2496}) {
    l.markDone(0, static_cast<Day>(d));
  }
  l.markDone(1, 0);
  char buf[512];
  const size_t n = serialize(l, buf, sizeof(buf));
  ASSERT_LT(n, sizeof(buf));
  EXPECT_EQ(std::strlen(buf), n);
  EXPECT_EQ(std::string(buf), "habits v1\nRead|5,6,7,100,2496\nWalk the dog|0\nEmpty one|\n");

  HabitList back;
  ASSERT_TRUE(parse(buf, n, back));
  EXPECT_EQ(back.count, 3);
  EXPECT_STREQ(back.habits[0].name, "Read");
  EXPECT_STREQ(back.habits[1].name, "Walk the dog");
  EXPECT_STREQ(back.habits[2].name, "Empty one");
  EXPECT_EQ(days(back, 0), (std::vector<Day>{5, 6, 7, 100, 2496}));
  EXPECT_EQ(days(back, 1), (std::vector<Day>{0}));
  EXPECT_EQ(back.totalDone(2), 0);
}

TEST(HabitsFile, SerializeCountsWhenBufferIsSmall) {
  HabitList l;
  l.add("Read");
  l.markDone(0, 12345);
  const size_t full = serialize(l, nullptr, 0);
  EXPECT_EQ(full, std::strlen("habits v1\nRead|12345\n"));
  char small[12];
  const size_t n = serialize(l, small, sizeof(small));
  EXPECT_EQ(n, full);
  EXPECT_EQ(std::strlen(small), sizeof(small) - 1);
  EXPECT_EQ(std::string(small), "habits v1\nR");
}

TEST(HabitsFile, FullFileFitsCapacity) {
  HabitList l;
  for (size_t i = 0; i < HabitList::kMax; ++i) {
    ASSERT_TRUE(l.add(std::string(kNameCapacity - 1, static_cast<char>('A' + i)).c_str()));
    // 65136..65535: the uint16_t wraps to 0 after the last day.
    for (Day d = static_cast<Day>(65536 - kDaysPerHabit); d != 0; ++d) {
      l.markDone(i, d);
    }
    ASSERT_EQ(l.totalDone(i), kDaysPerHabit);
  }
  static char buf[kFileCapacity];
  const size_t n = serialize(l, buf, sizeof(buf));
  EXPECT_LT(n, kFileCapacity);
  HabitList back;
  ASSERT_TRUE(parse(buf, n, back));
  EXPECT_EQ(back.count, HabitList::kMax);
  EXPECT_EQ(back.totalDone(HabitList::kMax - 1), kDaysPerHabit);
  EXPECT_TRUE(back.isDone(0, 65535));
}

TEST(HabitsFile, ParseToleratesCorruptInput) {
  const std::string text =
      "habits v1\r\n"
      "\n"
      "no bar on this line\n"
      "|no name\n"
      "   \t|only spaces\n"
      "Read|5,abc,7,,99999,-3,8,1e3,6\n"
      "Read|1\n"        // duplicate name: ignored
      "Walk|3,3,3,2\n"  // repeats and unsorted input
      "Tooooooooooooooooooooooooooooooooooooooooooooooooo long|1\n"
      "weird|1,2,";
  HabitList l;
  EXPECT_TRUE(parse(text.c_str(), text.size(), l));
  ASSERT_EQ(l.count, 4);
  EXPECT_STREQ(l.habits[0].name, "Read");
  EXPECT_EQ(days(l, 0), (std::vector<Day>{5, 6, 7, 8}));
  EXPECT_EQ(days(l, 1), (std::vector<Day>{2, 3}));
  EXPECT_EQ(std::strlen(l.habits[2].name), kNameCapacity - 1);
  EXPECT_EQ(days(l, 3), (std::vector<Day>{1, 2}));
}

TEST(HabitsFile, ParseRejectsMissingOrWrongHeaderButKeepsData) {
  HabitList l;
  l.add("stale");
  const char* noHeader = "Read|1,2\n";
  EXPECT_FALSE(parse(noHeader, std::strlen(noHeader), l));
  EXPECT_EQ(l.count, 1);  // cleared first, then the readable line kept
  EXPECT_STREQ(l.habits[0].name, "Read");

  const char* wrong = "habits v9\nRead|1\n";
  EXPECT_FALSE(parse(wrong, std::strlen(wrong), l));
  EXPECT_EQ(l.count, 1);

  EXPECT_FALSE(parse(nullptr, 0, l));
  EXPECT_EQ(l.count, 0);
  EXPECT_FALSE(parse("", 0, l));

  const char binary[] = "habits v1\nok|1\n\x00\xff\xfe|9\nafter|2\n";
  EXPECT_TRUE(parse(binary, sizeof(binary) - 1, l));
  EXPECT_EQ(l.count, 1);  // stops at the NUL
  EXPECT_STREQ(l.habits[0].name, "ok");
}

TEST(HabitsFile, ParseDropsHabitsPastMax) {
  std::string text = "habits v1\n";
  for (int i = 0; i < 20; ++i) {
    text += "h" + std::to_string(i) + "|" + std::to_string(i) + "\n";
  }
  HabitList l;
  EXPECT_TRUE(parse(text.c_str(), text.size(), l));
  EXPECT_EQ(l.count, HabitList::kMax);
  EXPECT_STREQ(l.habits[HabitList::kMax - 1].name, "h15");
}
