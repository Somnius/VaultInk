#pragma once

#include <algorithm>

// How the VaultInk home fits its rows (up to 10) into the space under the Continue card. The list
// draws a row at least as tall as its icon and its two text lines, so a row height below that makes
// the rows grow again and the last ones fall off the screen. This picks the tallest layout whose
// rows really fit: first only the height shrinks, then the gap, then Books loses its bigger icon
// and title font, then the status lines go.
namespace homelayout {

inline constexpr int ROW_HEIGHT = 68;      // every home tile has this height when there is room
inline constexpr int ROW_GAP = 8;          // gap between tiles, up to 9 rows
inline constexpr int COMPACT_GAP = 3;      // gap with 10 rows, or when the rows have to shrink further
inline constexpr int COMPACT_ROWS = 10;    // from this many rows the gap is always the compact one
inline constexpr int MIN_ROW_HEIGHT = 44;  // the touch minimum: rows never get shorter than this
inline constexpr int ICON_SIZE = 32;
inline constexpr int BOOKS_ICON_SIZE = 48;  // the Books tile's icon, bigger than the others (kos25k #14)

// Line heights of the three UI fonts.
struct Fonts {
  int title;
  int body;
  int small;
};

struct Layout {
  int rowHeight = ROW_HEIGHT;
  int rowGap = ROW_GAP;
  bool bigPrimary = true;  // Books and Vaults in the title font, Books with the 48 px icon
  bool subtitles = true;   // the status line under each label
  bool fits = true;        // false: even the smallest layout is taller than the space (rows are cut)
};

// Tallest row (at most ROW_HEIGHT) that lets `rows` rows with `gap` between them fit `space`.
inline int rowHeightFor(const int space, const int rows, const int gap) {
  if (rows <= 0) return ROW_HEIGHT;
  return std::min(ROW_HEIGHT, (space + gap) / rows - gap);
}

// The height the list really gives a row: never less than its icon or its text.
inline int rowNeed(const Fonts& fonts, const bool bigPrimary, const bool subtitles) {
  const int sub = subtitles ? fonts.small : 0;
  int need = std::max(MIN_ROW_HEIGHT, fonts.body + sub);
  if (bigPrimary) need = std::max({need, BOOKS_ICON_SIZE, fonts.title + sub});
  return need;
}

// Height of `count` rows with the gaps between them (none after the last).
inline int listHeight(const Layout& layout, const int count) {
  return count > 0 ? count * (layout.rowHeight + layout.rowGap) - layout.rowGap : 0;
}

inline Layout compute(const int space, const int rows, const Fonts& fonts) {
  Layout out;
  if (rows <= 0) return out;
  const int firstGap = rows >= COMPACT_ROWS ? COMPACT_GAP : ROW_GAP;
  struct Step {
    int gap;
    bool bigPrimary;
    bool subtitles;
  };
  const Step steps[] = {
      {firstGap, true, true}, {COMPACT_GAP, true, true}, {COMPACT_GAP, false, true}, {COMPACT_GAP, false, false}};
  for (const Step& step : steps) {
    out.rowGap = step.gap;
    out.bigPrimary = step.bigPrimary;
    out.subtitles = step.subtitles;
    // The list grows a row to its content, so that is the height to count with.
    out.rowHeight = std::max(rowHeightFor(space, rows, step.gap), rowNeed(fonts, step.bigPrimary, step.subtitles));
    if (listHeight(out, rows) <= space) return out;
  }
  // Nothing fits (a short landscape screen with many rows): the smallest layout, cut at the bottom.
  out.fits = false;
  return out;
}

}  // namespace homelayout
