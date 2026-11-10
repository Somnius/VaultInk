#pragma once

// Auto warmth and night mode by the clock (KOReader parity milestone). Platform-free so it is host-tested
// (test/auto_schedule). The window runs from `startHour` to `endHour` on the local clock and may cross midnight;
// start == end means the window is empty (never night).

#include <cstdint>

namespace autoschedule {

// True when `hour` (0-23) is inside the night window [startHour, endHour).
inline bool inNightWindow(const uint8_t startHour, const uint8_t endHour, const uint8_t hour) {
  if (startHour > 23 || endHour > 23 || hour > 23 || startHour == endHour) return false;
  if (startHour < endHour) return hour >= startHour && hour < endHour;
  return hour >= startHour || hour < endHour;  // crosses midnight, e.g. 20 to 7
}

// What the caller should do after looking at the clock. `applied` is the state last applied (0 day, 1 night).
enum class Step : uint8_t { None, EnterNight, EnterDay };

inline Step next(const bool enabled, const uint8_t startHour, const uint8_t endHour, const uint8_t hour,
                 const uint8_t applied) {
  if (!enabled) return Step::None;
  const bool night = inNightWindow(startHour, endHour, hour);
  if (night && applied == 0) return Step::EnterNight;
  if (!night && applied != 0) return Step::EnterDay;
  return Step::None;
}

}  // namespace autoschedule
