#pragma once

#include <algorithm>

// Where the file browser draws its three header buttons (refresh, gear, search from the left) and
// where the folder name beside them has to end. Numbers only, so computer tests cover it
// (test/header_buttons_layout).
namespace headerbuttons {

constexpr int GAP = 22;        // between two buttons
constexpr int TITLE_GAP = 8;   // between the title's end and the first button
constexpr int EDGE_INSET = 8;  // from the content's side padding to the last button

constexpr int size(const int headerHeight) { return headerHeight - 24; }

constexpr int searchX(const int screenWidth, const int sidePadding, const int headerHeight) {
  return screenWidth - sidePadding - EDGE_INSET - size(headerHeight);
}

constexpr int gearX(const int screenWidth, const int sidePadding, const int headerHeight) {
  return searchX(screenWidth, sidePadding, headerHeight) - (size(headerHeight) + GAP);
}

constexpr int refreshX(const int screenWidth, const int sidePadding, const int headerHeight) {
  return searchX(screenWidth, sidePadding, headerHeight) - 2 * (size(headerHeight) + GAP);
}

// Screen x the title has to end before.
constexpr int titleMaxRight(const int screenWidth, const int sidePadding, const int headerHeight) {
  return refreshX(screenWidth, sidePadding, headerHeight) - TITLE_GAP;
}

// Width to take off the right of a header's text, which would otherwise run to `textRight`, so it
// ends at `maxRight`. 0 when maxRight is 0 (no limit) or already right of the text.
constexpr int titleReserve(const int textRight, const int maxRight) {
  return maxRight > 0 ? std::max(0, textRight - maxRight) : 0;
}

}  // namespace headerbuttons
