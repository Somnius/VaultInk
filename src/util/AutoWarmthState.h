#pragma once

#include "CrossPointSettings.h"

// Which warmth is in effect with Auto Warmth by Clock: the night value while the schedule has put the light in night
// mode, the saved day value otherwise. `autoWarmthApplied` bit 0 is the night side, bit 1 says the schedule itself
// turned night mode on (so only then does it turn it off again).
namespace autowarmth {

constexpr uint8_t NIGHT_BIT = 1;
constexpr uint8_t INVERTED_BIT = 2;

inline bool nightActive() { return SETTINGS.autoWarmth != 0 && (SETTINGS.autoWarmthApplied & NIGHT_BIT) != 0; }

inline uint8_t effectiveWarmth() { return nightActive() ? SETTINGS.autoWarmthNight : SETTINGS.frontlightWarmth; }

// A warmth chosen by hand goes to the night value at night and to the day value otherwise.
inline void storeWarmth(const uint8_t warmth) {
  if (nightActive()) {
    SETTINGS.autoWarmthNight = warmth;
  } else {
    SETTINGS.frontlightWarmth = warmth;
  }
}

}  // namespace autowarmth
