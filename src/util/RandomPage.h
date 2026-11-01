#pragma once

#include <Arduino.h>

#include <atomic>
#include <cstdint>

#if defined(ESP_PLATFORM)
#include <esp_random.h>
#endif

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

inline uint32_t random32() {
#if defined(ESP_PLATFORM)
  return esp_random();
#else
  return static_cast<uint32_t>(millis()) * 2654435761u ^ static_cast<uint32_t>(micros());
#endif
}

}  // namespace randompage
