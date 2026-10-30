#pragma once

// Tap rect of the house icon at the top centre of the header's status strip (kos25k #17), drawn
// on touch boards when Settings > Display > Home Button at the Top is on. Like HeaderBackTapTarget:
// recorded at draw time by the header drawers, consumed by ActivityManager (go home) and excluded
// from MappedInputManager's screen taps. `hidden` is set while the home screen is up.
namespace HeaderHomeTapTarget {
inline int x = 0;
inline int y = 0;
inline int w = 0;
inline int h = 0;
inline bool hidden = false;

inline void set(const int newX, const int newY, const int newW, const int newH) {
  x = newX;
  y = newY;
  w = newW;
  h = newH;
}

inline void clear() { w = 0; }

inline bool contains(const int tx, const int ty) { return w > 0 && tx >= x && tx < x + w && ty >= y && ty < y + h; }
}  // namespace HeaderHomeTapTarget
