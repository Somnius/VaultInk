// The byte-wise XTCH passes (Phase 1c.2) must match the per-pixel loops in XtcReaderActivity:
// same pixel decode, same Portrait rotation, same panel bit order.
#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "XtchPlanes.h"

namespace {

constexpr int kPageWidth = 480;   // logical Portrait width = panel height
constexpr int kPageHeight = 800;  // logical Portrait height = panel width
constexpr int kColBytes = kPageHeight / 8;
constexpr size_t kPlane = static_cast<size_t>(kPageWidth) * kColBytes;

uint8_t pixelValue(const std::vector<uint8_t>& p1, const std::vector<uint8_t>& p2, int x, int y) {
  const size_t at = static_cast<size_t>(kPageWidth - 1 - x) * kColBytes + y / 8;
  const int bit = 7 - (y % 8);
  return static_cast<uint8_t>((((p1[at] >> bit) & 1) << 1) | ((p2[at] >> bit) & 1));
}

// drawPixel in Portrait: phyX = y, phyY = panelHeight - 1 - x; a clear bit is black.
void drawPixel(std::vector<uint8_t>& fb, int x, int y, bool black) {
  const int phyX = y;
  const int phyY = kPageWidth - 1 - x;
  const size_t at = static_cast<size_t>(phyY) * kColBytes + phyX / 8;
  const uint8_t mask = static_cast<uint8_t>(1 << (7 - phyX % 8));
  if (black)
    fb[at] &= static_cast<uint8_t>(~mask);
  else
    fb[at] |= mask;
}

std::vector<uint8_t> reference(const std::vector<uint8_t>& p1, const std::vector<uint8_t>& p2, xtchplanes::Pass pass) {
  std::vector<uint8_t> fb(kPlane, pass == xtchplanes::Pass::Black ? 0xFF : 0x00);
  for (int y = 0; y < kPageHeight; ++y) {
    for (int x = 0; x < kPageWidth; ++x) {
      const uint8_t v = pixelValue(p1, p2, x, y);
      if (pass == xtchplanes::Pass::Black && v >= 1) drawPixel(fb, x, y, true);
      if (pass == xtchplanes::Pass::GrayLsb && v == 1) drawPixel(fb, x, y, false);
      if (pass == xtchplanes::Pass::GrayMsb && (v == 1 || v == 2)) drawPixel(fb, x, y, false);
    }
  }
  return fb;
}

std::vector<uint8_t> fast(const std::vector<uint8_t>& p1, const std::vector<uint8_t>& p2, xtchplanes::Pass pass) {
  std::vector<uint8_t> fb(kPlane, 0x55);  // garbage: the fast path must overwrite every byte
  for (int row = 0; row < kPageWidth; ++row) {
    const size_t at = static_cast<size_t>(row) * kColBytes;
    xtchplanes::combineRow(p1.data() + at, p2.data() + at, fb.data() + at, kColBytes, pass);
  }
  return fb;
}

constexpr xtchplanes::Pass kPasses[] = {xtchplanes::Pass::Black, xtchplanes::Pass::GrayLsb, xtchplanes::Pass::GrayMsb};

}  // namespace

TEST(XtchPlanesTest, MatchesPerPixelDrawing) {
  std::mt19937 rng(1234);
  std::uniform_int_distribution<int> byte(0, 255);
  std::vector<uint8_t> p1(kPlane), p2(kPlane);
  for (size_t i = 0; i < kPlane; ++i) {
    p1[i] = static_cast<uint8_t>(byte(rng));
    p2[i] = static_cast<uint8_t>(byte(rng));
  }
  for (const auto pass : kPasses) EXPECT_EQ(fast(p1, p2, pass), reference(p1, p2, pass)) << int(pass);
}

TEST(XtchPlanesTest, AllWhiteAndAllBlackPages) {
  for (const uint8_t fill : {0x00, 0xFF}) {
    const std::vector<uint8_t> p1(kPlane, fill), p2(kPlane, fill);
    for (const auto pass : kPasses) EXPECT_EQ(fast(p1, p2, pass), reference(p1, p2, pass)) << int(pass);
  }
}
