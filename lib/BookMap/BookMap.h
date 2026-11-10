#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Book map: the book as one strip, a segment per chapter, sized by the bytes that count (hidden chapters are
// left out, as in lib/HiddenFlows), with marks for bookmarks and clippings. Pure functions, no Arduino: the
// screen (EpubReaderBookMapActivity) only draws what these work out.
//
// `cumulative[i]` is the size of the book through spine item i and `hidden[i]` is non-zero when item i is
// hidden, exactly as lib/HiddenFlows takes them. A "fraction" is a share of the counted book, 0..1: the number
// Epub::visibleProgress() gives and jumpToBookFraction() takes.
namespace bookmap {

inline constexpr int MIN_WIDTH = 3;        // the narrowest a chapter is drawn
inline constexpr int HIDDEN_WIDTH = 6;     // every hidden chapter: a hatched sliver, outside the scale
inline constexpr int HIT_WIDTH = 24;       // a narrower segment is still this wide for a finger
inline constexpr int TICK_TOLERANCE = 12;  // a tap this near a mark picks the mark

struct Group {
  int16_t firstSpine = 0;
  int16_t lastSpine = 0;
  int16_t tocIndex = -1;  // the contents entry it stands for, -1 when it has none
  bool hidden = false;    // every item in it is hidden (and hidden chapters count: hiddenflows::active)
  uint32_t start = 0;     // counted bytes before it
  uint32_t bytes = 0;     // counted bytes in it; 0 when hidden
};

// One group per contents entry, by hiddenflows::chapterRange, made into a partition of the spine: entries that
// share a spine item or point backwards are skipped, items before the first entry are a group of their own
// (tocIndex -1) and other items no entry covers join the group before them. Without usable contents entries
// every spine item is a group. `tocSpine[i]` is the spine index of contents entry i, -1 when it has none.
std::vector<Group> groupChapters(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden,
                                 const std::vector<int16_t>& tocSpine);
// At most `maxCount` groups: neighbours are merged evenly (hidden only when all of them are). The strip has
// room for width / HIDDEN_WIDTH groups, see maxGroupsFor().
std::vector<Group> mergeGroups(const std::vector<Group>& groups, size_t maxCount);
size_t maxGroupsFor(int width);

struct Segment : Group {
  int x = 0;
  int width = 0;
};

struct Layout {
  int x = 0;
  int width = 0;
  uint32_t total = 0;  // counted bytes of the book
  std::vector<Segment> segments;
};

// Lays the groups out from `x` over `width` pixels, with no gaps. A hidden group is hiddenWidth wide; the rest
// share what is left by their bytes, none narrower than minWidth (the width comes off the larger ones). With
// more groups than fit, both widths shrink: merge first.
Layout layout(const std::vector<Group>& groups, int x, int width, int minWidth = MIN_WIDTH,
              int hiddenWidth = HIDDEN_WIDTH);

// The segment drawn at pixel x (clamped to the strip), -1 for an empty layout.
int segmentContaining(const Layout& layout, int x);
// The segment a finger at x means: one narrower than HIT_WIDTH is hit up to HIT_WIDTH / 2 from its centre, the
// nearest centre first; otherwise the one drawn there.
int segmentAt(const Layout& layout, int x);
// The segment that holds spine item `spineIndex`, -1 when none does.
int segmentOfSpine(const Layout& layout, int spineIndex);

// The share of the book at pixel x of segment `index` (x is clamped into it): always inside the segment, never
// on its border, which would belong to the chapter before; the border itself for a hidden or empty one. The
// jump takes whole bytes, so a chapter of a few bytes can still give its neighbour.
float fractionIn(const Layout& layout, int index, int x);
// The same for the segment a finger at x means: fractionIn(segmentAt(x), x).
float fractionAt(const Layout& layout, int x);
// The pixel for a share of the book; the inverse of fractionAt() on a shown segment.
int xAt(const Layout& layout, float fraction);
// The pixel for a place in the book: the middle of the sliver when the place is in a hidden chapter (there the
// fraction stands still), xAt(fraction) otherwise.
int xAtPlace(const Layout& layout, int spineIndex, float fraction);

struct Tick {
  float fraction = 0.0f;  // Epub::visibleProgress() of the place
  int16_t spineIndex = 0;
  int16_t ref = 0;   // the caller's index (which bookmark)
  int16_t x = 0;     // set by placeTicks()
  uint8_t kind = 0;  // the caller's kind (bookmark, clipping)
};

void placeTicks(const Layout& layout, std::vector<Tick>& ticks);
// The mark nearest to x and no further than `tolerance` pixels (the first of equals), -1 when there is none.
int nearestTick(const std::vector<Tick>& ticks, int x, int tolerance = TICK_TOLERANCE);

struct Hit {
  enum class Kind : uint8_t { None, Tick, Segment };
  Kind kind = Kind::None;
  int index = -1;         // which tick or segment
  float fraction = 0.0f;  // the tick's, or fractionAt(x)
};
// A tap at x: a mark within reach beats the segment under it.
Hit hitTest(const Layout& layout, const std::vector<Tick>& ticks, int x, int tolerance = TICK_TOLERANCE);

// The estimated book page for a share of the book, as the status bar counts it: `first` is where chapter 1
// starts and `scale` the pages of the whole book per unit of progress. 0 when there is no scale.
int pageAt(float fraction, float first, float scale);
// The page a chapter starts on, when `fraction` is the start of the chapter: one more than the pages before it.
int chapterStartPage(float fraction, float first, float scale);

}  // namespace bookmap
