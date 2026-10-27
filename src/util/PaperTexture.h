#pragma once

#include <I18n.h>

#include <cstdint>

class GfxRenderer;

// Page backgrounds for the reader: sparse black dots drawn into the B/W frame before the text.
// Every pattern is computed from the pixel position, so it costs no memory and stays put from
// page to page (no shimmer on fast refreshes).
namespace paper_texture {

enum Style : uint8_t {
  PLAIN = 0,
  PAPER,
  SANDPAPER,
  PAPYRUS_SOFT,
  PAPYRUS_MEDIUM,
  PAPYRUS_HARD,
  TOILET_PAPER,
  KITCHEN_PAPER,
  BATH_TOWEL,
  STYLE_COUNT
};

// Setting values in order, for the settings list and the quick-settings tile.
inline constexpr StrId NAMES[STYLE_COUNT] = {
    StrId::STR_PAPER_PLAIN,        StrId::STR_PAPER_PAPER,          StrId::STR_PAPER_SANDPAPER,
    StrId::STR_PAPER_PAPYRUS_SOFT, StrId::STR_PAPER_PAPYRUS_MEDIUM, StrId::STR_PAPER_PAPYRUS_HARD,
    StrId::STR_PAPER_TOILET,       StrId::STR_PAPER_KITCHEN,        StrId::STR_PAPER_TOWEL};

// Short forms for the quick-settings tile label, which fits about ten characters.
inline constexpr StrId SHORT_NAMES[STYLE_COUNT] = {
    StrId::STR_PAPER_PLAIN,          StrId::STR_PAPER_PAPER,          StrId::STR_PAPER_SANDPAPER,
    StrId::STR_PAPER_SHORT_PAPYRUS1, StrId::STR_PAPER_SHORT_PAPYRUS2, StrId::STR_PAPER_SHORT_PAPYRUS3,
    StrId::STR_PAPER_SHORT_TOILET,   StrId::STR_PAPER_SHORT_KITCHEN,  StrId::STR_PAPER_SHORT_TOWEL};

inline constexpr uint16_t ALL_STYLES = (1u << STYLE_COUNT) - 1;

// The styles the quick-settings tile cycles through; a stored 0 means all of them.
constexpr uint16_t cycleMask(const uint16_t stored) {
  const uint16_t mask = stored & ALL_STYLES;
  return mask ? mask : ALL_STYLES;
}

// The style after `current` in the cycle (wrapping); `current` itself need not be in it.
constexpr uint8_t nextInCycle(const uint8_t current, const uint16_t stored) {
  const uint16_t mask = cycleMask(stored);
  for (int step = 1; step <= STYLE_COUNT; ++step) {
    const int style = (current + step) % STYLE_COUNT;
    if (mask & (1u << style)) return static_cast<uint8_t>(style);
  }
  return current;
}

// Draws `style` over the whole logical screen. PLAIN draws nothing.
void draw(const GfxRenderer& renderer, uint8_t style);

}  // namespace paper_texture
