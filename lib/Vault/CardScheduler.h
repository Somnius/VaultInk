#pragma once

#include <cstdint>

// Spaced repetition for flashcards (Phase 10): SM-2 with four answers, in whole days, integer math
// only. A card's state is five small numbers; the day is `time / 86400`.
namespace cardsched {

enum class Answer : uint8_t { Again, Hard, Good, Easy };

struct CardState {
  uint32_t due = 0;       // day number the card is due on
  uint32_t last = 0;      // day of the last review; 0 = never reviewed (a new card)
  uint16_t interval = 0;  // days between the last two reviews, as scheduled
  uint16_t ease = 250;    // percent: 250 = the interval grows 2.5 times on Good
  uint8_t reps = 0;       // answers in a row that weren't Again
  uint8_t lapses = 0;     // times a known card was forgotten (stops at 255)

  bool isNew() const { return last == 0; }
  bool isDue(uint32_t today) const { return isNew() || due <= today; }
};

struct Config {
  uint16_t maxInterval = 365;
};

constexpr uint16_t MIN_EASE = 130;
constexpr uint16_t MAX_EASE = 400;

// The state after answering. `seed` (the card id's low bits) spreads cards that would fall due on
// the same day by up to 5% of the interval, the same way every time.
CardState review(const CardState& state, Answer answer, uint32_t today, const Config& config, uint32_t seed = 0);

// Days from today until the card shows again if answered like this (0 = again this session), for
// the labels under the answer buttons.
uint32_t daysUntilDue(const CardState& state, Answer answer, uint32_t today, const Config& config, uint32_t seed = 0);

// Day number of a Unix time; 0 for an unset clock (before 2001).
uint32_t dayOf(int64_t unixSeconds);

}  // namespace cardsched
