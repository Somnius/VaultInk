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

// Draws `style` over the whole logical screen. PLAIN draws nothing.
void draw(const GfxRenderer& renderer, uint8_t style);

}  // namespace paper_texture
