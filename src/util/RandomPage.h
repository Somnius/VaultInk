#pragma once

#include <Arduino.h>

#include <atomic>
#include <cstdint>

#include "util/Entropy.h"

// "Random page!" on the home (kos25k #15): the home picks a book and asks the reader to open it on a
// random page, as a look that saves nothing (the return arrow goes back to the reader's own place).
namespace randompage {

inline std::atomic<bool>& pending() {
  static std::atomic<bool> value{false};
  return value;
}

// The home sets it just before it opens the book; the reader takes it once.
inline void request() { pending().store(true); }
inline bool take() { return pending().exchange(false); }

// Every random pick in the reader (Random page!, the dice) takes its seed from src/util/Entropy.h: the chip's
// generator mixed with the date and time and the battery percentage and voltage.
inline uint32_t random32() { return entropy::seed(); }

}  // namespace randompage
