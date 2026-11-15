#pragma once

#include <stdint.h>

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

// Apply Bayer dithering and quantize to 4 levels (0-3)
// Stateless - works correctly with any pixel processing order
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  int bayer = bayer4x4[y & 3][x & 3];
  int dither = (bayer - 8) * 5;  // Scale to +/-40 (half of quantization step 85)

  int adjusted = gray + dither;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  if (adjusted < 64) return 0;
  if (adjusted < 128) return 1;
  if (adjusted < 192) return 2;
  return 3;
}

// Two levels only (0 black, 3 white) for a picture that is drawn in one black-and-white pass and
// never gets the gray passes on top. In that pass every level below 3 is drawn black, so the
// 4-level dither above turns any area darker than gray 157 into solid black: its -40..+35 offset only
// reaches across the top threshold (192). Here the 16 thresholds span the whole range (8, 24, ...
// 248), so the share of white pixels follows the gray value.
inline uint8_t applyBayerDither1Bit(uint8_t gray, int x, int y) {
  const int threshold = bayer4x4[y & 3][x & 3] * 16 + 8;
  return gray >= threshold ? 3 : 0;
}
