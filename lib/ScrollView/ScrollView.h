#pragma once

// Scroll mode's viewport math (kos25k #30). Platform-free and header-only, host-tested in test/scroll_view.
//
// The EPUB reader keeps its page cache as it is: a chapter is a list of laid-out pages, every page a list of
// lines with a y position. Scroll mode draws a VIEW over that list: it starts at any line of a page and runs on
// into the pages after it until the screen is full. Nothing here knows about pages on disk; the caller hands in
// a page's rows through a callback.
//
// Words used below:
//   row   one scroll stop of a page: the elements that share a line (a text line, an image, a rule).
//   Pos   a page and a row in it: the view's top line. The reader saves the page, so everything that goes by
//         page (bookmarks, KOReader sync, the book map, hidden chapters) sees the page that holds the top line.
//   rowsOf(page)  the caller's lookup: a pointer to that page's rows, or nullptr when the chapter has no such
//         page (before page 0 or past the last one).

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace scrollview {

struct Row {
  int16_t top = 0;     // y of the row in its page
  int16_t height = 0;  // the row's own height (a line's height, an image's height), without the gap after it
};

// One thing on a page that the view can stop at.
struct Item {
  int16_t top = 0;
  int16_t height = 0;
};

// The rows of a page from its items. Items whose tops are within `mergeGap` px of a row's top join that row
// (table cells of one table row sit a few px apart); the row takes the tallest of them.
inline std::vector<Row> buildRows(std::vector<Item> items, const int mergeGap) {
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.top < b.top; });
  std::vector<Row> rows;
  rows.reserve(items.size());
  for (const Item& item : items) {
    if (!rows.empty() && item.top - rows.back().top <= mergeGap) {
      const int bottom = std::max(rows.back().top + rows.back().height, item.top + item.height);
      rows.back().height = static_cast<int16_t>(bottom - rows.back().top);
      continue;
    }
    rows.push_back({item.top, item.height});
  }
  return rows;
}

// The row that holds page y: the last row that starts at or above it (row 0 for anything above the first).
inline int rowAt(const std::vector<Row>& rows, const int y) {
  int found = 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].top <= y) found = static_cast<int>(i);
  }
  return found;
}

// Where a page's content ends: the next page's first line is drawn from here.
inline int contentHeight(const std::vector<Row>& rows) {
  return std::accumulate(rows.begin(), rows.end(), 0,
                         [](const int bottom, const Row& row) { return std::max(bottom, row.top + row.height); });
}

// From one row's top to the next one's: what a one-row scroll moves by. The last row of a page gives its height.
inline int rowPitch(const std::vector<Row>& rows, const int row) {
  if (row < 0 || row >= static_cast<int>(rows.size())) return 0;
  if (row + 1 < static_cast<int>(rows.size())) return std::max(1, rows[row + 1].top - rows[row].top);
  return std::max<int>(1, rows[row].height);
}

struct Pos {
  int page = 0;
  int row = 0;
};

inline bool operator==(const Pos& a, const Pos& b) { return a.page == b.page && a.row == b.row; }
inline bool operator!=(const Pos& a, const Pos& b) { return !(a == b); }
inline bool before(const Pos& a, const Pos& b) { return a.page < b.page || (a.page == b.page && a.row < b.row); }

// A run of rows of one page inside the view. An element of the page at y is drawn at y + yShift (from the
// view's top); the segment covers [top, bottom) of the view.
struct Segment {
  int page = 0;
  int firstRow = 0;
  int rowCount = 0;
  int yShift = 0;
  int top = 0;
  int bottom = 0;
};

struct View {
  std::vector<Segment> segments;
  bool reachesEnd = false;  // the chapter's last row is in the view
};

// A row index that exists in the page (0 for an empty page).
template <typename RowsOf>
Pos clampPos(Pos pos, RowsOf&& rowsOf) {
  if (pos.page < 0) return {0, 0};
  const std::vector<Row>* rows = rowsOf(pos.page);
  if (!rows || rows->empty()) return {pos.page, 0};
  pos.row = std::clamp(pos.row, 0, static_cast<int>(rows->size()) - 1);
  return pos;
}

// The view that starts at `top`: whole rows only, as many as fit in `viewHeight`, across page ends. The first
// row is always shown, even one taller than the view. The top row sits at y 0; a following page keeps the
// space above its first line, as its page had it.
template <typename RowsOf>
View compose(Pos top, const int viewHeight, RowsOf&& rowsOf) {
  View view;
  top = clampPos(top, rowsOf);
  int page = top.page;
  int firstRow = top.row;
  int y = 0;  // where the page being added starts
  bool firstPage = true;
  while (true) {
    const std::vector<Row>* rows = rowsOf(page);
    if (!rows) {
      view.reachesEnd = true;
      break;
    }
    const int count = static_cast<int>(rows->size());
    const int shift = (firstPage && firstRow < count) ? -(*rows)[firstRow].top : y;
    Segment segment{page, firstRow, 0, shift, 0, 0};
    bool full = false;
    for (int r = firstRow; r < count; ++r) {
      const Row& row = (*rows)[r];
      const bool veryFirst = view.segments.empty() && segment.rowCount == 0;
      if (!veryFirst && row.top + shift + row.height > viewHeight) {
        full = true;
        break;
      }
      if (segment.rowCount == 0) segment.top = row.top + shift;
      segment.bottom = std::max(segment.bottom, row.top + shift + row.height);
      ++segment.rowCount;
    }
    if (segment.rowCount > 0) view.segments.push_back(segment);
    if (full) break;
    y = shift + contentHeight(*rows);
    if (y < 0) y = 0;
    ++page;
    firstRow = 0;
    firstPage = false;
  }
  return view;
}

// `pos` moved by `rows` rows (negative = back), across pages, stopping at the chapter's first and last row.
// Pages without rows are stepped over.
template <typename RowsOf>
Pos advance(Pos pos, int rows, RowsOf&& rowsOf) {
  pos = clampPos(pos, rowsOf);
  while (rows > 0) {
    const std::vector<Row>* here = rowsOf(pos.page);
    const int count = here ? static_cast<int>(here->size()) : 0;
    if (pos.row + 1 < count) {
      ++pos.row;
      --rows;
      continue;
    }
    // The next page that has rows.
    int next = pos.page + 1;
    const std::vector<Row>* nextRows = rowsOf(next);
    while (nextRows && nextRows->empty()) nextRows = rowsOf(++next);
    if (!nextRows) break;
    pos = {next, 0};
    --rows;
  }
  while (rows < 0) {
    if (pos.row > 0) {
      --pos.row;
      ++rows;
      continue;
    }
    int prev = pos.page - 1;
    const std::vector<Row>* prevRows = prev >= 0 ? rowsOf(prev) : nullptr;
    while (prevRows && prevRows->empty()) prevRows = --prev >= 0 ? rowsOf(prev) : nullptr;
    if (!prevRows) break;
    pos = {prev, static_cast<int>(prevRows->size()) - 1};
    ++rows;
  }
  return pos;
}

// `pos` moved by about `pixels` of text (negative = back): as many whole rows as fit in that distance, one at
// least. Used for a swipe (its length) and for the steps that are a share of the screen.
template <typename RowsOf>
Pos advanceByPixels(Pos pos, const int pixels, RowsOf&& rowsOf) {
  pos = clampPos(pos, rowsOf);
  if (pixels == 0) return pos;
  const int wanted = pixels < 0 ? -pixels : pixels;
  const int dir = pixels < 0 ? -1 : 1;
  int moved = 0;
  bool first = true;
  while (true) {
    const Pos next = advance(pos, dir, rowsOf);
    if (next == pos) break;
    // Forward the row left behind is `pos`; back it is the row arrived at.
    const Pos crossed = dir > 0 ? pos : next;
    const std::vector<Row>* rows = rowsOf(crossed.page);
    const int pitch = rows ? rowPitch(*rows, crossed.row) : 1;
    if (!first && moved + pitch > wanted) break;
    moved += pitch;
    pos = next;
    first = false;
    if (moved >= wanted) break;
  }
  return pos;
}

// The earliest top from which the chapter's end is on screen, searched back from `from` (a position whose view
// already reaches the end, or the chapter's last row). Scrolling forward stops here, so the last screen of a
// chapter is a full one.
template <typename RowsOf>
Pos endStart(Pos from, const int viewHeight, RowsOf&& rowsOf) {
  // The chapter's last row.
  Pos pos = clampPos(from, rowsOf);
  while (true) {
    const Pos next = advance(pos, 1, rowsOf);
    if (next == pos) break;
    pos = next;
  }
  while (true) {
    const Pos prev = advance(pos, -1, rowsOf);
    if (prev == pos || !compose(prev, viewHeight, rowsOf).reachesEnd) break;
    pos = prev;
  }
  return pos;
}

// The row right under the view that starts at `top`: the furthest a forward scroll may go without passing a
// row that was never on screen. At the chapter's end it is the last row.
template <typename RowsOf>
Pos belowView(const Pos top, const int viewHeight, RowsOf&& rowsOf) {
  const View view = compose(top, viewHeight, rowsOf);
  if (view.segments.empty()) return clampPos(top, rowsOf);
  const Segment& last = view.segments.back();
  return advance({last.page, last.firstRow + last.rowCount - 1}, 1, rowsOf);
}

// The scroll step setting.
enum StepKind : uint8_t {
  kStep3Lines = 0,
  kStep5Lines = 1,
  kStep10Lines = 2,
  kStepHalfScreen = 3,
  kStepTwoThirds = 4,
  kStepScreen = 5,  // a screen less one line, so the last line read stays in sight
  kStepKindCount
};

struct Step {
  int rows = 0;    // move by this many rows, or
  int pixels = 0;  // by the rows that fit in this many pixels
};

inline Step stepFor(const uint8_t kind, const int viewHeight, const int lineHeight) {
  switch (kind) {
    case kStep3Lines:
      return {3, 0};
    case kStep5Lines:
      return {5, 0};
    case kStep10Lines:
      return {10, 0};
    case kStepTwoThirds:
      return {0, std::max(1, (viewHeight * 2) / 3)};
    case kStepScreen:
      return {0, std::max(1, viewHeight - std::max(0, lineHeight))};
    case kStepHalfScreen:
    default:
      return {0, std::max(1, viewHeight / 2)};
  }
}

// One scroll: `steps` steps of the setting (negative = back), or the swipe's length when `swipePixels` is not 0
// (its sign is ignored, the direction is that of `steps`). Forward it stops at endStart(), so it never scrolls
// past the chapter's last screen; back it stops at the chapter's first row.
template <typename RowsOf>
Pos scroll(const Pos from, const int steps, const Step step, const int swipePixels, const int viewHeight,
           RowsOf&& rowsOf) {
  if (steps == 0) return clampPos(from, rowsOf);
  const int dir = steps < 0 ? -1 : 1;
  const int count = steps < 0 ? -steps : steps;
  Pos pos = clampPos(from, rowsOf);
  for (int i = 0; i < count; ++i) {
    Pos next;
    if (swipePixels != 0) {
      next = advanceByPixels(pos, dir * (swipePixels < 0 ? -swipePixels : swipePixels), rowsOf);
    } else if (step.rows > 0) {
      next = advance(pos, dir * step.rows, rowsOf);
    } else {
      next = advanceByPixels(pos, dir * std::max(1, step.pixels), rowsOf);
    }
    // No row may be passed unseen: a step is cut to what the screen showed (an image can leave room for
    // fewer lines than the step is long).
    if (dir > 0) {
      const Pos limit = belowView(pos, viewHeight, rowsOf);
      if (before(limit, next)) next = limit;
    } else {
      while (before(next, pos) && before(belowView(next, viewHeight, rowsOf), pos)) next = advance(next, 1, rowsOf);
    }
    if (next == pos) break;
    pos = next;
  }
  if (dir > 0 && compose(pos, viewHeight, rowsOf).reachesEnd) {
    const Pos last = endStart(pos, viewHeight, rowsOf);
    // Never back past where the scroll started: a view that already showed the end stays put.
    pos = before(last, from) ? clampPos(from, rowsOf) : last;
  }
  return pos;
}

// The segment of the view at view y, or -1 (the gap between two pages belongs to the page above it).
inline int segmentAt(const View& view, const int y) {
  int found = -1;
  for (size_t i = 0; i < view.segments.size(); ++i) {
    if (y >= view.segments[i].top) found = static_cast<int>(i);
  }
  return found;
}

// Refresh scheduling: the reader counts page draws down to its next cleaning (HALF) refresh. In scroll mode
// every step redraws the whole frame with a FAST refresh, so the counter is held at `every` at most and the
// cleaning refresh comes after `every` scrolls. 0 or less leaves the reader's own count alone.
inline int capRefreshCounter(const int counter, const int every) {
  return every > 0 ? std::min(counter, every) : counter;
}

}  // namespace scrollview
