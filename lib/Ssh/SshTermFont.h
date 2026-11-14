#pragma once

#include <cstddef>
#include <cstdint>

// The SSH terminal's own font and its grid arithmetic. A fixed-cell 1-bit bitmap font in four
// sizes: every character is one cell, every cell has the same width and height, and a glyph is
// copied pixel for pixel (nothing is scaled, hinted or smoothed). The glyphs are a subset of
// Terminus Font (SIL OFL 1.1, lib/Ssh/fonts/OFL.TXT), written into SshTermFontData.cpp by
// scripts/gen_ssh_font.py: Latin, Greek, Cyrillic, arrows, mathematical signs, the box-drawing
// characters and the block elements. No platform code: tested on the computer.
namespace sshfont {

struct Font {
  uint8_t cellW;
  uint8_t cellH;
  uint8_t rowBytes;     // bytes for one row of a glyph
  const uint8_t* rows;  // glyph after glyph, cellH * rowBytes bytes each
};

// The sizes, smallest first. The setting stores the cell height in pixels.
constexpr uint8_t SIZE_COUNT = 4;
constexpr uint8_t DEFAULT_HEIGHT = 16;
constexpr uint8_t MIN_HEIGHT = 12;
constexpr uint8_t MAX_HEIGHT = 18;
constexpr uint8_t HEIGHT_STEP = 2;

// A stored value as one of the four heights: an odd value between them is the next one up (13 is
// 14), anything outside 12 to 18 is the default. The setting is passed through this wherever it
// comes in (the settings file, the web settings page), so it only ever holds 12, 14, 16 or 18.
constexpr uint8_t snapHeight(const uint8_t height) {
  if (height < MIN_HEIGHT || height > MAX_HEIGHT) return DEFAULT_HEIGHT;
  return static_cast<uint8_t>(height + (height - MIN_HEIGHT) % HEIGHT_STEP);
}
// The font for a height: 12 (6 x 12), 14 (8 x 14), 16 (8 x 16) or 18 (10 x 18); any other value
// is snapped first.
const Font& fontForHeight(uint8_t height);
// The next height in the cycle 12 -> 14 -> 16 -> 18 -> 12, counted from the snapped value, so a
// tap always changes the size.
uint8_t nextHeight(uint8_t height);

// The glyph number of a code point, or -1 when the font does not have it.
int glyphIndex(uint32_t cp);
// What to draw in place of a character the font does not have: a similar character it does have
// (a dashed box line as the plain one, an eighth block as the half block, a small triangle as
// '>'), '+' for any other box-drawing character, '#' for any other block element, a blank for a
// control character, else '?'.
uint32_t fallbackFor(uint32_t cp);
// The glyph number to draw for a code point: its own, or its fallback's. Never negative.
int resolveGlyph(uint32_t cp);

// Row `row` of a glyph, the cell's first column in bit 15.
uint16_t rowBits(const Font& font, int glyph, int row);
// True when any row of the glyph has ink in the cell's first column.
bool usesFirstColumn(const Font& font, int glyph);
// The row drawn bold: every pixel once more one column to the right, cut at the cell's edge; when
// the glyph also has ink in the first column the last column stays clear, so two bold letters side
// by side do not join.
uint16_t boldRowBits(const Font& font, uint16_t bits, bool firstColumnUsed);
// Bold is drawn for cells at least 8 pixels wide, and never for box-drawing characters and block
// elements (their lines must keep meeting the next cell's).
bool boldAllowed(const Font& font, uint32_t cp);

// Calls plot(dx, dy) for every inked pixel of the cell that shows `cp`: 0 <= dx < cellW and
// 0 <= dy < cellH, whatever the character.
template <typename Plot>
void drawCell(const Font& font, const uint32_t cp, const bool bold, Plot&& plot) {
  const int glyph = resolveGlyph(cp);
  const bool heavy = bold && boldAllowed(font, cp);
  const bool first = heavy && usesFirstColumn(font, glyph);
  for (int dy = 0; dy < font.cellH; ++dy) {
    uint16_t bits = rowBits(font, glyph, dy);
    if (heavy) bits = boldRowBits(font, bits, first);
    for (int dx = 0; bits != 0 && dx < font.cellW; ++dx, bits = static_cast<uint16_t>(bits << 1)) {
      if (bits & 0x8000) plot(dx, dy);
    }
  }
}

// The grid that fits an area: whole cells only, so nothing is cut at an edge, and the unused
// pixels of the width split left and right.
struct Grid {
  uint16_t cols = 0;
  uint16_t rows = 0;
  int left = 0;  // x of the first column, from the area's left edge
};
// `width` and `height` are the area in pixels. The counts are held between 20 x 4 and
// maxCols x maxRows; an area too small for 20 x 4 cells still reports 20 x 4.
Grid gridFor(int width, int height, const Font& font, uint16_t maxCols, uint16_t maxRows);

namespace data {
extern const uint16_t GLYPH_COUNT;
extern const uint16_t CODEPOINTS[];
extern const uint8_t ROWS_12[];
extern const uint8_t ROWS_14[];
extern const uint8_t ROWS_16[];
extern const uint8_t ROWS_18[];
}  // namespace data

}  // namespace sshfont
