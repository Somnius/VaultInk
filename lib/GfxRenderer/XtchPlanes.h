#pragma once

#include <cstddef>
#include <cstdint>

// XTCH (2-bit) pages store each plane column by column, right column first, 8 pixels per byte
// top to bottom, MSB first. In Portrait on the 800x480 panel a panel row is exactly one such
// column, so a pass over the page is a byte-wise combine of the two planes (Phase 1c.2) instead
// of 384,000 drawPixel calls. Pixel value v = (plane1 bit << 1) | plane2 bit; 0 is white.
namespace xtchplanes {

enum class Pass : uint8_t {
  Black,    // the BW frame: black wherever v >= 1 (a clear bit is black)
  GrayLsb,  // the LSB gray plane: set where v == 1
  GrayMsb,  // the MSB gray plane: set where v == 1 or v == 2
};

inline void combineRow(const uint8_t* plane1, const uint8_t* plane2, uint8_t* out, const size_t bytes,
                       const Pass pass) {
  switch (pass) {
    case Pass::Black:
      for (size_t i = 0; i < bytes; ++i) out[i] = static_cast<uint8_t>(~(plane1[i] | plane2[i]));
      break;
    case Pass::GrayLsb:
      for (size_t i = 0; i < bytes; ++i) out[i] = static_cast<uint8_t>(~plane1[i] & plane2[i]);
      break;
    case Pass::GrayMsb:
      for (size_t i = 0; i < bytes; ++i) out[i] = static_cast<uint8_t>(plane1[i] ^ plane2[i]);
      break;
  }
}

}  // namespace xtchplanes
