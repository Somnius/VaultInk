#pragma once

#include <cstdint>

// The seed of a dice roll (0.63.0). The chip's random number generator is the main source; the rest is "noise" the
// reader has at hand, mixed in so that a roll never depends on one source alone: the date and time of the roll
// (yyyy-mm-dd hh:mm:ss), the battery percentage and the battery voltage, the clock's own jitter (micros, millis)
// and a counter so two rolls in the same instant still differ. The mix is a hash (MurmurHash3's body and
// finaliser), so a little changing noise spreads over all 32 bits of the seed.
//
// Platform-free and deterministic (the same Noise gives the same seed), so it is tested on the computer; the
// firmware gathers the real values in src/util/Entropy.cpp.
namespace dice {
namespace entropy {

struct Noise {
  uint32_t hardware = 0;      // the chip's random number generator
  uint64_t dateTime = 0;      // yyyymmddhhmmss as a number, e.g. 20261101111428
  uint32_t epochSeconds = 0;  // seconds since 1970, low 32 bits
  uint32_t micros = 0;
  uint32_t millis = 0;
  uint32_t batteryPercent = 0;     // 0..100
  uint32_t batteryMillivolts = 0;  // e.g. 3870
  uint32_t counter = 0;            // rises with every roll
};

constexpr uint32_t rotl(const uint32_t x, const int r) { return (x << r) | (x >> (32 - r)); }

constexpr uint32_t mix(uint32_t h, uint32_t v) {
  v *= 0xCC9E2D51u;
  v = rotl(v, 15);
  v *= 0x1B873593u;
  h ^= v;
  h = rotl(h, 13);
  return h * 5u + 0xE6546B64u;
}

constexpr uint32_t finish(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85EBCA6Bu;
  h ^= h >> 13;
  h *= 0xC2B2AE35u;
  h ^= h >> 16;
  return h;
}

constexpr uint32_t seed(const Noise& n) {
  uint32_t h = 0x9E3779B9u;
  h = mix(h, n.hardware);
  h = mix(h, static_cast<uint32_t>(n.dateTime));
  h = mix(h, static_cast<uint32_t>(n.dateTime >> 32));
  h = mix(h, n.epochSeconds);
  h = mix(h, n.micros);
  h = mix(h, n.millis);
  h = mix(h, n.batteryPercent);
  h = mix(h, n.batteryMillivolts);
  h = mix(h, n.counter);
  return finish(h);
}

}  // namespace entropy
}  // namespace dice
