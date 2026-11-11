#include "BookMap.h"

#include <HiddenFlows.h>

#include <algorithm>
#include <cstdlib>

namespace bookmap {

namespace {

int16_t toInt16(const int value) { return static_cast<int16_t>(std::clamp(value, -32768, 32767)); }

Group makeGroup(const int first, const int last, const int tocIndex) {
  Group group;
  group.firstSpine = toInt16(first);
  group.lastSpine = toInt16(last);
  group.tocIndex = toInt16(tocIndex);
  return group;
}

bool shown(const Segment& segment) { return !segment.hidden && segment.bytes > 0; }

// Twice the distance from x to the middle of the segment, so a half pixel stays a whole number.
int centreDistance2(const Segment& segment, const int x) {
  return std::abs(2 * x - (2 * segment.x + segment.width - 1));
}

}  // namespace

std::vector<Group> groupChapters(const std::vector<uint32_t>& cumulative, const std::vector<uint8_t>& hidden,
                                 const std::vector<int16_t>& tocSpine) {
  std::vector<Group> groups;
  const int spineCount = static_cast<int>(std::min<size_t>(cumulative.size(), 32767));
  if (spineCount == 0) return groups;

  const int tocCount = static_cast<int>(std::min<size_t>(tocSpine.size(), 32767));
  const auto spineOf = [&tocSpine](const int i) { return static_cast<int>(tocSpine[static_cast<size_t>(i)]); };
  int covered = -1;  // the last spine item a group holds so far
  for (int toc = 0; toc < tocCount; toc++) {
    int first = 0, last = 0;
    if (!hiddenflows::chapterRange(toc, tocCount, spineCount, spineOf, first, last)) continue;
    if (first <= covered) continue;  // the same item as the entry before, or one that points backwards
    if (first > covered + 1) {
      if (groups.empty()) {
        groups.push_back(makeGroup(0, first - 1, -1));
      } else {
        groups.back().lastSpine = toInt16(first - 1);
      }
    }
    last = std::max(last, first);
    groups.push_back(makeGroup(first, last, toc));
    covered = last;
  }
  if (groups.empty()) {
    groups.reserve(static_cast<size_t>(spineCount));
    for (int i = 0; i < spineCount; i++) groups.push_back(makeGroup(i, i, -1));
  } else if (covered < spineCount - 1) {
    groups.back().lastSpine = toInt16(spineCount - 1);
  }

  // The flags only count while something is hidden and something is left, as the percent does.
  const bool useHidden = hiddenflows::active(cumulative, hidden);
  uint32_t start = 0;
  for (auto& group : groups) {
    bool allHidden = useHidden;
    uint32_t bytes = 0;
    for (int i = group.firstSpine; i <= group.lastSpine; i++) {
      if (useHidden && hiddenflows::isHidden(hidden, i)) continue;
      allHidden = false;
      bytes += hiddenflows::itemSize(cumulative, i);
    }
    group.hidden = allHidden;
    group.start = start;
    group.bytes = bytes;
    start += bytes;
  }
  return groups;
}

std::vector<Group> mergeGroups(const std::vector<Group>& groups, const size_t maxCount) {
  if (maxCount == 0 || groups.size() <= maxCount) return groups;
  std::vector<Group> merged;
  merged.reserve(maxCount);
  size_t bucket = SIZE_MAX;
  for (size_t i = 0; i < groups.size(); i++) {
    const size_t target = i * maxCount / groups.size();
    const Group& group = groups[i];
    if (target != bucket) {
      bucket = target;
      merged.push_back(group);
      continue;
    }
    Group& into = merged.back();
    into.lastSpine = group.lastSpine;
    if (into.tocIndex < 0) into.tocIndex = group.tocIndex;
    into.hidden = into.hidden && group.hidden;
    into.bytes += group.bytes;
  }
  return merged;
}

size_t maxGroupsFor(const int width) {
  return static_cast<size_t>(std::max(1, width / std::max(MIN_WIDTH, HIDDEN_WIDTH)));
}

Layout layout(const std::vector<Group>& groups, const int x, const int width, int minWidth, int hiddenWidth) {
  Layout result;
  result.x = x;
  result.width = std::max(width, 0);
  const int count = static_cast<int>(groups.size());
  if (count == 0 || result.width == 0) return result;

  minWidth = std::max(minWidth, 1);
  hiddenWidth = std::max(hiddenWidth, 1);
  int hiddenCount =
      static_cast<int>(std::count_if(groups.begin(), groups.end(), [](const Group& g) { return g.hidden; }));
  if (hiddenCount == count) hiddenCount = 0;  // nothing left to scale against: draw them all as shown
  const int shownCount = count - hiddenCount;
  const bool slivers = hiddenCount > 0;
  if (hiddenCount * hiddenWidth + shownCount * minWidth > result.width) {
    // More groups than pixels for them (the caller should merge first): everything gets as thin as it takes.
    minWidth = std::max(1, result.width / count);
    hiddenWidth = minWidth;
  }

  std::vector<int> widths(static_cast<size_t>(count), 0);
  std::vector<uint8_t> locked(static_cast<size_t>(count), 0);  // at the minimum, or a hidden sliver
  int freeWidth = result.width;
  int freeCount = shownCount;
  uint64_t freeBytes = 0;
  for (int i = 0; i < count; i++) {
    const Group& group = groups[static_cast<size_t>(i)];
    if (slivers && group.hidden) {
      widths[static_cast<size_t>(i)] = hiddenWidth;
      locked[static_cast<size_t>(i)] = 1;
      freeWidth -= hiddenWidth;
    } else {
      freeBytes += group.bytes;
    }
  }
  // Give the minimum to every group whose share would be less, and share the rest again, until it settles.
  for (bool changed = true; changed && freeCount > 0;) {
    changed = false;
    for (int i = 0; i < count; i++) {
      if (locked[static_cast<size_t>(i)]) continue;
      const uint64_t bytes = groups[static_cast<size_t>(i)].bytes;
      if (freeBytes != 0 &&
          bytes * static_cast<uint64_t>(std::max(freeWidth, 0)) >= static_cast<uint64_t>(minWidth) * freeBytes) {
        continue;
      }
      widths[static_cast<size_t>(i)] = minWidth;
      locked[static_cast<size_t>(i)] = 1;
      freeWidth -= minWidth;
      freeBytes -= bytes;
      freeCount--;
      changed = true;
    }
  }
  freeWidth = std::max(freeWidth, 0);

  std::vector<int> order;  // who gets the pixels left over by rounding down
  int used = 0;
  if (freeCount > 0) {
    std::vector<uint64_t> remainder(static_cast<size_t>(count), 0);
    for (int i = 0; i < count; i++) {
      if (locked[static_cast<size_t>(i)]) continue;
      const uint64_t scaled = groups[static_cast<size_t>(i)].bytes * static_cast<uint64_t>(freeWidth);
      widths[static_cast<size_t>(i)] = static_cast<int>(scaled / freeBytes);
      remainder[static_cast<size_t>(i)] = scaled % freeBytes;
      used += widths[static_cast<size_t>(i)];
      order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&remainder](const int a, const int b) {
      return remainder[static_cast<size_t>(a)] > remainder[static_cast<size_t>(b)];
    });
  } else {
    // Every shown group is at the minimum: what is left is spread over them (over all, when none is shown).
    for (int i = 0; i < count; i++) {
      if (!(slivers && groups[static_cast<size_t>(i)].hidden)) order.push_back(i);
    }
  }
  if (!order.empty()) {
    int left = freeWidth - used;
    for (size_t i = 0; left > 0; i = (i + 1) % order.size()) {
      widths[static_cast<size_t>(order[i])]++;
      left--;
    }
  }

  result.segments.reserve(static_cast<size_t>(count));
  int at = x;
  uint32_t total = 0;
  for (int i = 0; i < count; i++) {
    Segment segment;
    static_cast<Group&>(segment) = groups[static_cast<size_t>(i)];
    segment.hidden = slivers && segment.hidden;
    segment.x = at;
    segment.width = widths[static_cast<size_t>(i)];
    at += segment.width;
    total += segment.bytes;
    result.segments.push_back(segment);
  }
  result.total = total;
  return result;
}

int segmentContaining(const Layout& layout, const int x) {
  const int count = static_cast<int>(layout.segments.size());
  if (count == 0) return -1;
  int last = -1;
  for (int i = 0; i < count; i++) {
    const Segment& segment = layout.segments[static_cast<size_t>(i)];
    if (segment.width <= 0) continue;
    last = i;
    if (x < segment.x + segment.width) return i;  // the first one: x left of the strip is its start
  }
  return last >= 0 ? last : 0;
}

int segmentAt(const Layout& layout, const int x) {
  int best = segmentContaining(layout, x);
  if (best < 0) return -1;
  const int clamped = std::clamp(x, layout.x, layout.x + std::max(layout.width, 1) - 1);
  const Segment& under = layout.segments[static_cast<size_t>(best)];
  int bestDistance = under.width < HIT_WIDTH ? centreDistance2(under, clamped) : INT32_MAX;
  for (int i = 0; i < static_cast<int>(layout.segments.size()); i++) {
    const Segment& segment = layout.segments[static_cast<size_t>(i)];
    if (segment.width <= 0 || segment.width >= HIT_WIDTH) continue;
    const int distance = centreDistance2(segment, clamped);
    if (distance > HIT_WIDTH || distance >= bestDistance) continue;
    best = i;
    bestDistance = distance;
  }
  return best;
}

int segmentOfSpine(const Layout& layout, const int spineIndex) {
  for (int i = 0; i < static_cast<int>(layout.segments.size()); i++) {
    const Segment& segment = layout.segments[static_cast<size_t>(i)];
    if (spineIndex >= segment.firstSpine && spineIndex <= segment.lastSpine) return i;
  }
  return -1;
}

float fractionAt(const Layout& layout, const int x) { return fractionIn(layout, segmentAt(layout, x), x); }

float fractionIn(const Layout& layout, const int index, const int x) {
  if (index < 0 || index >= static_cast<int>(layout.segments.size()) || layout.total == 0) return 0.0f;
  const Segment& segment = layout.segments[static_cast<size_t>(index)];
  const double total = static_cast<double>(layout.total);
  if (!shown(segment) || segment.width <= 0) return static_cast<float>(static_cast<double>(segment.start) / total);
  const int inside = std::clamp(x, segment.x, segment.x + segment.width - 1) - segment.x;
  const double within = (static_cast<double>(inside) + 0.5) / static_cast<double>(segment.width);
  const double bytes = static_cast<double>(segment.start) + within * static_cast<double>(segment.bytes);
  return static_cast<float>(std::clamp(bytes / total, 0.0, 1.0));
}

int xAt(const Layout& layout, const float fraction) {
  if (layout.total == 0) return layout.x;
  const double bytes = std::clamp(static_cast<double>(fraction), 0.0, 1.0) * static_cast<double>(layout.total);
  const Segment* lastShown = nullptr;
  for (const Segment& segment : layout.segments) {
    if (!shown(segment) || segment.width <= 0) continue;
    lastShown = &segment;
    if (bytes >= static_cast<double>(segment.start) + static_cast<double>(segment.bytes)) continue;
    const double within =
        std::max(0.0, bytes - static_cast<double>(segment.start)) / static_cast<double>(segment.bytes);
    return segment.x + std::min(segment.width - 1, static_cast<int>(within * static_cast<double>(segment.width)));
  }
  return lastShown ? lastShown->x + lastShown->width - 1 : layout.x;
}

int xAtPlace(const Layout& layout, const int spineIndex, const float fraction) {
  const int index = segmentOfSpine(layout, spineIndex);
  if (index >= 0) {
    const Segment& segment = layout.segments[static_cast<size_t>(index)];
    if (segment.hidden && segment.width > 0) return segment.x + segment.width / 2;
  }
  return xAt(layout, fraction);
}

void placeTicks(const Layout& layout, std::vector<Tick>& ticks) {
  for (Tick& tick : ticks) tick.x = toInt16(xAtPlace(layout, tick.spineIndex, tick.fraction));
}

int nearestTick(const std::vector<Tick>& ticks, const int x, const int tolerance) {
  int best = -1;
  int bestDistance = INT32_MAX;
  for (int i = 0; i < static_cast<int>(ticks.size()); i++) {
    const int distance = std::abs(static_cast<int>(ticks[static_cast<size_t>(i)].x) - x);
    if (distance > tolerance || distance >= bestDistance) continue;
    best = i;
    bestDistance = distance;
  }
  return best;
}

Hit hitTest(const Layout& layout, const std::vector<Tick>& ticks, const int x, const int tolerance) {
  Hit hit;
  const int tick = nearestTick(ticks, x, tolerance);
  if (tick >= 0) {
    hit.kind = Hit::Kind::Tick;
    hit.index = tick;
    hit.fraction = ticks[static_cast<size_t>(tick)].fraction;
    return hit;
  }
  const int segment = segmentAt(layout, x);
  if (segment < 0) return hit;
  hit.kind = Hit::Kind::Segment;
  hit.index = segment;
  hit.fraction = fractionAt(layout, x);
  return hit;
}

int pageAt(const float fraction, const float first, const float scale) {
  if (scale <= 0.0f) return 0;
  return std::max(1, static_cast<int>((fraction - first) * scale + 0.5f));
}

int chapterStartPage(const float fraction, const float first, const float scale) {
  if (scale <= 0.0f) return 0;
  return std::max(1, static_cast<int>((fraction - first) * scale + 0.5f) + 1);
}

}  // namespace bookmap
