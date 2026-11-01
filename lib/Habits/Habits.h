#pragma once

#include <cstddef>
#include <cstdint>

// Platform-free daily habit tracker for VaultInk: up to 16 habits, each remembering the last 400
// days it was done. Days are counted from 2020-01-01 (day 0) so they fit a uint16_t until 2199.
// Civil-date maths is done here (no libc time), so the same numbers come out on the device and in
// the tests. No heap, no exceptions, no Arduino/ESP includes.
namespace habits {

// ---- days ----

using Day = uint16_t;

constexpr int EPOCH_YEAR = 2020;
constexpr int64_t SECONDS_PER_DAY = 86400;
constexpr int64_t EPOCH_DAY_OFFSET = 18262;  // 2020-01-01 in days since 1970-01-01

struct Date {
  int year = EPOCH_YEAR;
  uint8_t month = 1;  // 1..12
  uint8_t day = 1;    // 1..31
};

bool isLeapYear(int year);
uint8_t daysInMonth(int year, uint8_t month);
// Days since 2020-01-01; dates before that give 0, dates past 2199 saturate. Month/day are clamped.
Day dayFromDate(int year, uint8_t month, uint8_t day);
inline Day dayFromDate(const Date& d) { return dayFromDate(d.year, d.month, d.day); }
Date dateFromDay(Day day);
// Floor division by 86400 (so negative epochs round down), then the same clamping as dayFromDate.
Day dayFromEpoch(int64_t epochSeconds);
Date epochToDate(int64_t epochSeconds);
// Monday = 0 .. Sunday = 6.
uint8_t dayOfWeek(Day day);

// ---- habits ----

constexpr size_t kNameCapacity = 32;  // bytes incl. the NUL
constexpr size_t kDaysPerHabit = 400;

struct Habit {
  char name[kNameCapacity] = {};
  Day done[kDaysPerHabit] = {};  // sorted ascending, no repeats
  uint16_t count = 0;
};

struct HabitList {
  static constexpr size_t kMax = 16;

  Habit habits[kMax] = {};
  uint8_t count = 0;

  void clear() { count = 0; }

  // Trims, replaces '|' and line breaks with spaces, cuts to kNameCapacity-1 bytes on a UTF-8
  // boundary. False (nothing added) on an empty result, a duplicate name or a full list.
  bool add(const char* name);
  bool remove(size_t index);
  // Inserts the day in order; when the habit holds kDaysPerHabit days the oldest is dropped.
  // False on a bad index or when the day was already there.
  bool markDone(size_t index, Day day);
  bool unmark(size_t index, Day day);
  // Returns the new state (false on a bad index).
  bool toggle(size_t index, Day day);
  bool isDone(size_t index, Day day) const;
  // Consecutive done days ending today, or ending yesterday when today is not done yet.
  uint16_t streak(size_t index, Day today) const;
  uint16_t bestStreak(size_t index) const;
  // Done days within the last n days ending today (today, today-1, ... today-n+1).
  uint16_t doneInLast(size_t index, Day today, uint16_t n) const;
  uint16_t totalDone(size_t index) const;
};

// Looks the name up (exact match); -1 when absent.
int findHabit(const HabitList& list, const char* name);

// ---- file format ----
//
//   habits v1
//   name|d1,d2,d3
//
// one line per habit, day numbers ascending, no ranges.

constexpr uint8_t kFileVersion = 1;
// Header + kMax lines of a 31-byte name, '|', 400 five-digit days with commas and '\n'.
constexpr size_t kFileCapacity = 16 + HabitList::kMax * (kNameCapacity + 1 + kDaysPerHabit * 6 + 1);

// snprintf-style: the bytes the full text needs (without the NUL); the buffer gets as much as fits,
// always NUL-terminated when cap > 0. A result >= cap means the output was cut.
size_t serialize(const HabitList& list, char* buf, size_t cap);
// Fills `out` (cleared first). Lines without '|' or with an empty name are ignored, bad numbers
// are skipped, over-long names are cut, habits past kMax are dropped. Returns false when the
// header is missing or names another version; the habits it could read are kept anyway.
bool parse(const char* data, size_t size, HabitList& out);

}  // namespace habits
