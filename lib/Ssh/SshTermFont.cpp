#include "SshTermFont.h"

#include <algorithm>
#include <iterator>

namespace sshfont {

namespace {

const Font FONTS[SIZE_COUNT] = {
    {6, 12, 1, data::ROWS_12},
    {8, 14, 1, data::ROWS_14},
    {8, 16, 1, data::ROWS_16},
    {10, 18, 2, data::ROWS_18},
};

struct Substitute {
  uint16_t from;
  uint16_t to;
};
// Sorted by `from`. Characters the font lacks, drawn as one it has.
constexpr Substitute SUBSTITUTES[] = {
    {0x2023, '>'},     // triangular bullet
    {0x2024, '.'},     // one dot leader
    {0x2025, '.'},     // two dot leader
    {0x2027, 0x00B7},  // hyphenation point
    {0x2043, '-'},     // hyphen bullet
    {0x2504, 0x2500},  // dashed box lines as the plain ones
    {0x2505, 0x2501}, {0x2506, 0x2502}, {0x2507, 0x2503}, {0x254C, 0x2500}, {0x254D, 0x2501},
    {0x254E, 0x2502}, {0x254F, 0x2503}, {0x2594, 0x2580},  // upper eighth block as the upper half
    {0x2595, 0x2590},                                      // right eighth block as the right half
    {0x25A1, 0x25A0},                                      // squares
    {0x25AA, 0x25A0}, {0x25AB, 0x25A0}, {0x25B3, 0x25B2},  // triangles
    {0x25B4, '^'},    {0x25B5, '^'},    {0x25B7, 0x25B6}, {0x25B8, '>'},    {0x25B9, '>'},
    {0x25BB, 0x25BA}, {0x25BD, 0x25BC}, {0x25BE, 'v'},    {0x25BF, 'v'},    {0x25C1, 0x25C0},
    {0x25C2, '<'},    {0x25C3, '<'},    {0x25C5, 0x25C4}, {0x25C7, 0x25C6},                    // diamonds and circles
    {0x25C9, 0x25CF}, {0x25CC, 0x25CB}, {0x25E6, 'o'},    {0x25EF, 0x25CB}, {0x2611, 0x2713},  // ballot boxes and marks
    {0x2612, 0x2717}, {0x2715, 0x2717}, {0x2716, 0x2718}, {0x276E, '<'},  // heavy angle quotation marks (shell prompts)
    {0x276F, '>'},    {0x2794, 0x2192},                                   // heavy arrows
    {0x279C, 0x2192}, {0x27A4, '>'},    {0x27F5, 0x2190},                 // long arrows
    {0x27F6, 0x2192}, {0x2B06, 0x2191}, {0x2B07, 0x2193}, {0x3000, ' '},  // ideographic space
};

}  // namespace

const Font& fontForHeight(const uint8_t height) { return FONTS[(snapHeight(height) - MIN_HEIGHT) / HEIGHT_STEP]; }

uint8_t nextHeight(const uint8_t height) {
  const uint8_t current = snapHeight(height);
  return current >= MAX_HEIGHT ? MIN_HEIGHT : static_cast<uint8_t>(current + HEIGHT_STEP);
}

int glyphIndex(const uint32_t cp) {
  if (cp > 0xFFFF) return -1;
  const uint16_t* begin = data::CODEPOINTS;
  const uint16_t* end = begin + data::GLYPH_COUNT;
  const uint16_t* found = std::lower_bound(begin, end, static_cast<uint16_t>(cp));
  if (found == end || *found != cp) return -1;
  return static_cast<int>(found - begin);
}

uint32_t fallbackFor(const uint32_t cp) {
  if (cp <= 0xFFFF) {
    const auto* end = std::end(SUBSTITUTES);
    const auto* found =
        std::lower_bound(std::begin(SUBSTITUTES), end, cp,
                         [](const Substitute& entry, const uint32_t value) { return entry.from < value; });
    if (found != end && found->from == cp) return found->to;
  }
  if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return ' ';  // control characters take a blank cell
  if (cp >= 0x2500 && cp <= 0x257F) return '+';
  if (cp >= 0x2580 && cp <= 0x259F) return '#';
  return '?';
}

int resolveGlyph(const uint32_t cp) {
  int glyph = glyphIndex(cp);
  if (glyph < 0) glyph = glyphIndex(fallbackFor(cp));
  if (glyph < 0) glyph = glyphIndex('?');
  return glyph < 0 ? 0 : glyph;
}

uint16_t rowBits(const Font& font, const int glyph, const int row) {
  if (glyph < 0 || glyph >= data::GLYPH_COUNT || row < 0 || row >= font.cellH) return 0;
  const uint8_t* at = font.rows + (static_cast<size_t>(glyph) * font.cellH + row) * font.rowBytes;
  const uint16_t bits =
      font.rowBytes == 1 ? static_cast<uint16_t>(at[0] << 8) : static_cast<uint16_t>(at[0] << 8 | at[1]);
  // Only the cell's own columns: nothing outside the cell is ever drawn.
  return static_cast<uint16_t>(bits & (0xFFFFu << (16 - font.cellW)));
}

bool usesFirstColumn(const Font& font, const int glyph) {
  for (int row = 0; row < font.cellH; ++row) {
    if (rowBits(font, glyph, row) & 0x8000) return true;
  }
  return false;
}

uint16_t boldRowBits(const Font& font, const uint16_t bits, const bool firstColumnUsed) {
  uint16_t mask = static_cast<uint16_t>(0xFFFFu << (16 - font.cellW));
  if (firstColumnUsed) mask = static_cast<uint16_t>(mask << 1);  // the last column stays clear
  return static_cast<uint16_t>(bits | ((bits >> 1) & mask));
}

bool boldAllowed(const Font& font, const uint32_t cp) { return font.cellW >= 8 && !(cp >= 0x2500 && cp <= 0x259F); }

Grid gridFor(const int width, const int height, const Font& font, const uint16_t maxCols, const uint16_t maxRows) {
  Grid grid;
  grid.cols = static_cast<uint16_t>(std::clamp(width / font.cellW, 20, std::max<int>(20, maxCols)));
  grid.rows = static_cast<uint16_t>(std::clamp(height / font.cellH, 4, std::max<int>(4, maxRows)));
  grid.left = std::max(0, (width - grid.cols * font.cellW) / 2);
  return grid;
}

}  // namespace sshfont
