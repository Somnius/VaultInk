#include "CardScheduler.h"

#include <algorithm>

namespace cardsched {
namespace {

uint16_t clampEase(int value) { return static_cast<uint16_t>(std::clamp<int>(value, MIN_EASE, MAX_EASE)); }

// Grow `interval` by `percent`/100, and by at least one day.
uint32_t grow(uint32_t interval, uint32_t percent) {
  return std::max<uint32_t>(interval + 1, static_cast<uint32_t>((static_cast<uint64_t>(interval) * percent) / 100));
}

// -5% .. +5% of the interval, from the seed; short intervals stay as they are.
uint32_t fuzz(uint32_t interval, uint32_t seed) {
  if (interval < 7) return interval;
  const int pct = static_cast<int>(seed % 11) - 5;
  return static_cast<uint32_t>(static_cast<int>(interval) + static_cast<int>(interval) * pct / 100);
}

}  // namespace

CardState review(const CardState& state, const Answer answer, const uint32_t today, const Config& config,
                 const uint32_t seed) {
  CardState next = state;
  next.last = today;
  uint32_t interval = 0;
  if (state.isNew()) {
    switch (answer) {
      case Answer::Again:
        next.reps = 0;
        next.due = today;
        next.interval = 0;
        return next;
      case Answer::Hard:
      case Answer::Good:
        interval = 1;
        break;
      case Answer::Easy:
        interval = 4;
        break;
    }
    next.reps = 1;
  } else {
    switch (answer) {
      case Answer::Again:
        next.reps = 0;
        if (next.lapses < 255) ++next.lapses;
        next.ease = clampEase(static_cast<int>(state.ease) - 20);
        next.interval = 1;
        next.due = today;
        return next;
      case Answer::Hard:
        interval = grow(state.interval, 120);
        next.ease = clampEase(static_cast<int>(state.ease) - 15);
        break;
      case Answer::Good:
        interval = grow(state.interval, state.ease);
        break;
      case Answer::Easy:
        interval = grow(state.interval, state.ease * 130u / 100u);
        next.ease = clampEase(static_cast<int>(state.ease) + 15);
        break;
    }
    if (next.reps < 255) ++next.reps;
  }
  interval = std::clamp<uint32_t>(fuzz(interval, seed), 1, std::max<uint16_t>(config.maxInterval, 1));
  next.interval = static_cast<uint16_t>(interval);
  next.due = today + interval;
  return next;
}

uint32_t daysUntilDue(const CardState& state, const Answer answer, const uint32_t today, const Config& config,
                      const uint32_t seed) {
  return review(state, answer, today, config, seed).due - today;
}

uint32_t dayOf(const int64_t unixSeconds) {
  constexpr int64_t YEAR_2001 = 978307200;
  return unixSeconds < YEAR_2001 ? 0 : static_cast<uint32_t>(unixSeconds / 86400);
}

}  // namespace cardsched
