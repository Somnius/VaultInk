#pragma once

#include <FreeInkApp.h>

#include <cstdint>

// The Settings root list has two group headings ("Reading", "Device"), each with props.sectionGap
// of padding above it. The list draws a row that does not fit whole as a clipped preview with no
// touch zone (freeink list(): partialTrailingRow). On a 480x800 touch panel the 8 rows came to
// 694 px in a 690 px body, so the last row (System) was drawn 4 px short and ignored a tap until
// the list was scrolled.
namespace settingslist {

// The padding above each of `headings` group headings with which a list `naturalHeight` px tall
// (measured with `gap`) fits `available` px. `gap` when it fits already, or when fitting would
// take more than half of the padding (the list then scrolls as before).
inline int16_t fittedSectionGap(const int naturalHeight, const int available, const int headings, const int16_t gap) {
  if (naturalHeight <= available || headings <= 0 || gap <= 0) return gap;
  const int cut = (naturalHeight - available + headings - 1) / headings;
  return cut > gap / 2 ? gap : static_cast<int16_t>(gap - cut);
}

// Shrinks props.sectionGap so that every row fits the screen's body, when a little is enough.
// `props` must be resolved (after syncListViewport); `paddedHeadings` counts the headings that get
// the padding, which are the ones not on the first row.
template <typename Screen>
void fitSectionGap(Screen& screen, freeink::ui::ListProps& props, const int paddedHeadings) {
  freeink::ui::Rect unbounded = screen.body();
  const int available = unbounded.height;
  unbounded.height = INT16_MAX;  // listContentHeight stops measuring at the rect's height
  const int natural = freeink::ui::listContentHeight(screen.target(), screen.frame().assets(), unbounded, props);
  props.sectionGap = fittedSectionGap(natural, available, paddedHeadings, props.sectionGap);
}

}  // namespace settingslist
