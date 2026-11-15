#include <gtest/gtest.h>

#include "DitherUtils.h"

namespace {

// Pixels of a flat 16x16 area that a black-and-white pass leaves white: there every level below 3
// is drawn black (DirectPixelWriter::writePixel, GfxRenderer::BW).
int whiteOf256(const uint8_t gray, uint8_t (*dither)(uint8_t, int, int)) {
  int white = 0;
  for (int y = 0; y < 16; y++) {
    for (int x = 0; x < 16; x++) white += dither(gray, x, y) == 3 ? 1 : 0;
  }
  return white;
}

}  // namespace

// Why a Gallery thumbnail could be a black box: the grid has no gray passes, and with the 4-level
// dither a picture has to be lighter than gray 156 to get a single white pixel.
TEST(DitherUtils, FourLevelsInOneBlackAndWhitePassAreBlackBelow157) {
  for (int gray = 0; gray < 157; gray++) EXPECT_EQ(whiteOf256(static_cast<uint8_t>(gray), applyBayerDither4Level), 0);
  EXPECT_GT(whiteOf256(157, applyBayerDither4Level), 0);
}

TEST(DitherUtils, OneBitGivesOnlyBlackOrWhite) {
  for (int gray = 0; gray < 256; gray++) {
    for (int y = 0; y < 4; y++) {
      for (int x = 0; x < 4; x++) {
        const uint8_t level = applyBayerDither1Bit(static_cast<uint8_t>(gray), x, y);
        EXPECT_TRUE(level == 0 || level == 3) << gray;
      }
    }
  }
}

TEST(DitherUtils, OneBitWhiteShareFollowsTheGrayValue) {
  EXPECT_EQ(whiteOf256(0, applyBayerDither1Bit), 0);
  EXPECT_EQ(whiteOf256(255, applyBayerDither1Bit), 256);
  EXPECT_EQ(whiteOf256(128, applyBayerDither1Bit), 128);
  int before = 0;
  for (int gray = 0; gray < 256; gray++) {
    const int white = whiteOf256(static_cast<uint8_t>(gray), applyBayerDither1Bit);
    EXPECT_GE(white, before) << gray;
    // Within one of the 16 steps of the share the gray value asks for.
    EXPECT_NEAR(white, gray * 256 / 255, 16) << gray;
    before = white;
  }
  // The dark and middle grays that were a black box before.
  EXPECT_GT(whiteOf256(40, applyBayerDither1Bit), 0);
  EXPECT_GT(whiteOf256(100, applyBayerDither1Bit), 64);
}

// The same position always gets the same answer, so a cached thumbnail equals a fresh decode.
TEST(DitherUtils, OneBitRepeatsEveryFourPixels) {
  for (int gray = 0; gray < 256; gray += 5) {
    EXPECT_EQ(applyBayerDither1Bit(static_cast<uint8_t>(gray), 1, 2),
              applyBayerDither1Bit(static_cast<uint8_t>(gray), 5, 10));
  }
}
