#include "Habits.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace habits {

namespace {

constexpr int64_t MAX_DAY = UINT16_MAX;

// Howard Hinnant's days_from_civil: days since 1970-01-01 for a proleptic Gregorian date.
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// Inverse of daysFromCivil (civil_from_days).
Date civilFromDays(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp + (mp < 10 ? 3 : -9);
  Date out;
  out.year = static_cast<int>(y + (m <= 2 ? 1 : 0));
  out.month = static_cast<uint8_t>(m);
  out.day = static_cast<uint8_t>(d);
  return out;
}

Day clampDay(int64_t sinceEpoch) {
  return static_cast<Day>(std::min<int64_t>(std::max<int64_t>(sinceEpoch, 0), MAX_DAY));
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

bool validIndex(const HabitList& list, size_t index) { return index < list.count; }

const Day* findDay(const Habit& h, Day day) {
  const Day* end = h.done + h.count;
  const Day* it = std::lower_bound(h.done, end, day);
  return (it != end && *it == day) ? it : nullptr;
}

// Longest run of consecutive numbers in the sorted array.
uint16_t longestRun(const Habit& h) {
  uint16_t best = 0;
  uint16_t run = 0;
  for (uint16_t i = 0; i < h.count; ++i) {
    run = (i > 0 && h.done[i] == h.done[i - 1] + 1) ? static_cast<uint16_t>(run + 1) : 1;
    best = std::max(best, run);
  }
  return best;
}

// Consecutive done days ending at `last` (inclusive); 0 when `last` itself is not done.
uint16_t runEndingAt(const Habit& h, Day last) {
  const Day* it = findDay(h, last);
  if (it == nullptr) {
    return 0;
  }
  uint16_t n = 1;
  while (it != h.done && *(it - 1) + 1 == *it) {
    --it;
    ++n;
  }
  return n;
}

// Copies the cleaned name (see HabitList::add) into out; false when it ends up empty.
bool cleanName(const char* name, char* out) {
  if (name == nullptr) {
    return false;
  }
  const char* begin = name;
  while (*begin != '\0' && isSpace(*begin)) {
    ++begin;
  }
  const char* end = begin + std::strlen(begin);
  while (end != begin && isSpace(*(end - 1))) {
    --end;
  }
  size_t len = std::min(static_cast<size_t>(end - begin), kNameCapacity - 1);
  // Do not split a multi-byte UTF-8 sequence.
  while (len > 0 && static_cast<size_t>(end - begin) > len &&
         (static_cast<unsigned char>(begin[len]) & 0xC0u) == 0x80u) {
    --len;
  }
  std::memcpy(out, begin, len);
  out[len] = '\0';
  std::replace_if(out, out + len, [](char c) { return c == '|' || c == '\n' || c == '\r'; }, ' ');
  // Replacing may have left trailing spaces.
  while (len > 0 && isSpace(out[len - 1])) {
    out[--len] = '\0';
  }
  return len > 0;
}

}  // namespace

// ---- days ----

bool isLeapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

uint8_t daysInMonth(int year, uint8_t month) {
  constexpr uint8_t table[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 31;
  }
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return table[month - 1];
}

Day dayFromDate(int year, uint8_t month, uint8_t day) {
  const uint8_t m = static_cast<uint8_t>(std::min<int>(std::max<int>(month, 1), 12));
  const uint8_t d = static_cast<uint8_t>(std::min<int>(std::max<int>(day, 1), daysInMonth(year, m)));
  return clampDay(daysFromCivil(year, m, d) - EPOCH_DAY_OFFSET);
}

Date dateFromDay(Day day) { return civilFromDays(static_cast<int64_t>(day) + EPOCH_DAY_OFFSET); }

Day dayFromEpoch(int64_t epochSeconds) {
  int64_t days = epochSeconds / SECONDS_PER_DAY;
  if (epochSeconds < 0 && epochSeconds % SECONDS_PER_DAY != 0) {
    --days;
  }
  return clampDay(days - EPOCH_DAY_OFFSET);
}

Date epochToDate(int64_t epochSeconds) { return dateFromDay(dayFromEpoch(epochSeconds)); }

uint8_t dayOfWeek(Day day) {
  // 2020-01-01 was a Wednesday (2 with Monday = 0).
  return static_cast<uint8_t>((static_cast<uint32_t>(day) + 2u) % 7u);
}

// ---- HabitList ----

int findHabit(const HabitList& list, const char* name) {
  if (name == nullptr) {
    return -1;
  }
  const Habit* end = list.habits + list.count;
  const Habit* it = std::find_if(list.habits, end, [name](const Habit& h) { return std::strcmp(h.name, name) == 0; });
  return it == end ? -1 : static_cast<int>(it - list.habits);
}

bool HabitList::add(const char* name) {
  if (count >= kMax) {
    return false;
  }
  char clean[kNameCapacity];
  if (!cleanName(name, clean) || findHabit(*this, clean) >= 0) {
    return false;
  }
  Habit& h = habits[count];
  h = Habit{};
  std::memcpy(h.name, clean, sizeof(clean));
  ++count;
  return true;
}

bool HabitList::remove(size_t index) {
  if (!validIndex(*this, index)) {
    return false;
  }
  std::move(habits + index + 1, habits + count, habits + index);
  --count;
  habits[count] = Habit{};
  return true;
}

bool HabitList::markDone(size_t index, Day day) {
  if (!validIndex(*this, index)) {
    return false;
  }
  Habit& h = habits[index];
  Day* end = h.done + h.count;
  Day* pos = std::lower_bound(h.done, end, day);
  if (pos != end && *pos == day) {
    return false;
  }
  if (h.count >= kDaysPerHabit) {
    // Drop the oldest; a day older than everything kept is simply not recorded.
    if (pos == h.done) {
      return false;
    }
    // Shift the smaller days down one slot; the freed slot before `pos` takes the new day.
    std::move(h.done + 1, pos, h.done);
    *(pos - 1) = day;
    return true;
  }
  std::move_backward(pos, end, end + 1);
  *pos = day;
  ++h.count;
  return true;
}

bool HabitList::unmark(size_t index, Day day) {
  if (!validIndex(*this, index)) {
    return false;
  }
  Habit& h = habits[index];
  Day* end = h.done + h.count;
  Day* pos = std::lower_bound(h.done, end, day);
  if (pos == end || *pos != day) {
    return false;
  }
  std::move(pos + 1, end, pos);
  --h.count;
  return true;
}

bool HabitList::toggle(size_t index, Day day) {
  if (!validIndex(*this, index)) {
    return false;
  }
  if (isDone(index, day)) {
    unmark(index, day);
    return false;
  }
  return markDone(index, day);
}

bool HabitList::isDone(size_t index, Day day) const {
  return validIndex(*this, index) && findDay(habits[index], day) != nullptr;
}

uint16_t HabitList::streak(size_t index, Day today) const {
  if (!validIndex(*this, index)) {
    return 0;
  }
  const Habit& h = habits[index];
  const uint16_t ending = runEndingAt(h, today);
  if (ending > 0 || today == 0) {
    return ending;
  }
  return runEndingAt(h, static_cast<Day>(today - 1));
}

uint16_t HabitList::bestStreak(size_t index) const { return validIndex(*this, index) ? longestRun(habits[index]) : 0; }

uint16_t HabitList::doneInLast(size_t index, Day today, uint16_t n) const {
  if (!validIndex(*this, index) || n == 0) {
    return 0;
  }
  const Habit& h = habits[index];
  const Day first = today + 1 >= n ? static_cast<Day>(today + 1 - n) : 0;
  const Day* lo = std::lower_bound(h.done, h.done + h.count, first);
  const Day* hi = std::upper_bound(h.done, h.done + h.count, today);
  return hi > lo ? static_cast<uint16_t>(hi - lo) : 0;
}

uint16_t HabitList::totalDone(size_t index) const { return validIndex(*this, index) ? habits[index].count : 0; }

// ---- file format ----

size_t serialize(const HabitList& list, char* buf, size_t cap) {
  size_t need = 0;
  // Appends `n` formatted bytes, writing only what fits.
  const auto put = [&](const char* fmt, auto... args) {
    const size_t room = need < cap ? cap - need : 0;
    const int n = std::snprintf(room > 0 ? buf + need : nullptr, room, fmt, args...);
    if (n > 0) {
      need += static_cast<size_t>(n);
    }
  };
  if (cap > 0) {
    buf[0] = '\0';
  }
  put("habits v%u\n", static_cast<unsigned>(kFileVersion));
  for (uint8_t i = 0; i < list.count; ++i) {
    const Habit& h = list.habits[i];
    put("%s|", h.name);
    for (uint16_t j = 0; j < h.count; ++j) {
      put(j == 0 ? "%u" : ",%u", static_cast<unsigned>(h.done[j]));
    }
    put("\n");
  }
  if (cap > 0 && need >= cap) {
    buf[cap - 1] = '\0';
  }
  return need;
}

bool parse(const char* data, size_t size, HabitList& out) {
  out.clear();
  if (data == nullptr) {
    return false;
  }
  bool headerOk = false;
  size_t pos = 0;
  while (pos < size) {
    const char* line = data + pos;
    const size_t rest = size - pos;
    const char* nl = static_cast<const char*>(std::memchr(line, '\n', rest));
    size_t len = nl != nullptr ? static_cast<size_t>(nl - line) : rest;
    pos += len + 1;
    if (len > 0 && line[len - 1] == '\r') {
      --len;
    }
    if (len == 0) {
      continue;
    }
    if (std::memchr(line, '\0', len) != nullptr) {
      break;  // binary garbage: stop here, keep what was read
    }
    if (len >= 9 && std::memcmp(line, "habits v", 8) == 0) {
      headerOk = len == 9 && line[8] == '0' + kFileVersion;
      continue;
    }
    const char* bar = static_cast<const char*>(std::memchr(line, '|', len));
    if (bar == nullptr) {
      continue;
    }
    char name[kNameCapacity];
    const size_t nameLen = std::min(static_cast<size_t>(bar - line), kNameCapacity - 1);
    std::memcpy(name, line, nameLen);
    name[nameLen] = '\0';
    if (!out.add(name)) {
      continue;
    }
    const size_t index = out.count - 1u;
    // Comma-separated numbers; anything that is not a clean 1..5 digit number is skipped.
    const char* p = bar + 1;
    const char* lineEnd = line + len;
    while (p < lineEnd) {
      const char* comma = static_cast<const char*>(std::memchr(p, ',', static_cast<size_t>(lineEnd - p)));
      const char* tokEnd = comma != nullptr ? comma : lineEnd;
      const size_t tokLen = static_cast<size_t>(tokEnd - p);
      const bool digits =
          tokEnd > p && tokLen <= 5 && std::all_of(p, tokEnd, [](char c) { return c >= '0' && c <= '9'; });
      if (digits) {
        uint32_t v = 0;
        for (const char* c = p; c != tokEnd; ++c) {
          v = v * 10u + static_cast<uint32_t>(*c - '0');
        }
        if (v <= MAX_DAY) {
          out.markDone(index, static_cast<Day>(v));
        }
      }
      p = tokEnd + 1;
    }
  }
  return headerOk;
}

}  // namespace habits
