#pragma once

// Customisable reader gestures and the Quick Menu (KOReader parity milestone). Platform-free so it is host-tested
// (test/gestures): the slots, the actions, the old settings' behaviour as a function, and the resolver.
//
// Each slot stores one byte. 0 is Default: do what the older settings say (Next Page / Previous Page gestures, Tap
// Zones, Show Reader Menu), so a table of zeros changes nothing. A touch that belongs to no slot (a tap outside the
// centre and the corners, a swipe that starts in an edge band) always follows the older settings. Hardware buttons
// and the global gestures never pass through here, so no mapping can lock the reader.

#include <cstdint>

namespace gestures {

// Stored in settings.json by name (slotKey), so the order may change; the count may only grow.
enum Slot : uint8_t {
  CornerTL = 0,
  CornerTR,
  CornerBL,
  CornerBR,
  Centre,
  SwipeLeft,
  SwipeRight,
  SwipeUp,
  SwipeDown,
  kSlotCount
};
constexpr int kNoSlot = -1;

// Stored in settings.json by value: only ever append.
enum Action : uint8_t {
  Default = 0,  // follow the older settings; in the Quick Menu: an empty place
  None,
  NextPage,
  PrevPage,
  NextChapter,
  PrevChapter,
  ReaderMenu,
  QuickMenu,
  ToggleNight,
  ToggleFrontlight,
  LightPanel,
  Bookmark,
  Refresh,
  GoHome,
  Screenshot,
  kActionCount
};

constexpr int kQuickMenuSize = 6;

// The values of CrossPointSettings::PAGE_TURN_GESTURE and SHOW_READER_MENU (ReaderUtils.h asserts they match).
constexpr uint8_t kTapAndSwipe = 0;
constexpr uint8_t kTapOnly = 1;
constexpr uint8_t kSwipeOnly = 2;
constexpr uint8_t kInvertedTap = 3;
constexpr uint8_t kMenuTap = 1;

// The older settings a Default slot follows.
struct Legacy {
  uint8_t nextGesture = kSwipeOnly;  // pageTurnGesture
  uint8_t prevGesture = kSwipeOnly;  // previousPageGesture
  uint8_t tapZones = 0;              // 1: only the four corners turn pages
  uint8_t showReaderMenu = kMenuTap;
  bool rtlBook = false;
};

enum class Swipe : uint8_t { None, Left, Right, Up, Down };

inline bool allowsSwipe(const uint8_t gesture) { return gesture == kTapAndSwipe || gesture == kSwipeOnly; }
inline bool allowsTap(const uint8_t gesture) {
  return gesture == kTapAndSwipe || gesture == kTapOnly || gesture == kInvertedTap;
}

// The middle third of the screen both ways.
inline bool inCentre(const int x, const int y, const int width, const int height) {
  return x >= width / 3 && x < width - width / 3 && y >= height / 3 && y < height - height / 3;
}

// The corner square (a sixth of the short side) a tap is in, else kNoSlot.
inline int cornerAt(const int x, const int y, const int width, const int height) {
  const int corner = (width < height ? width : height) / 6;
  const bool left = x < corner;
  const bool right = x >= width - corner;
  const bool top = y < corner;
  const bool bottom = y >= height - corner;
  if ((!left && !right) || (!top && !bottom)) return kNoSlot;
  if (top) return left ? CornerTL : CornerTR;
  return left ? CornerBL : CornerBR;
}

// The slot a tap belongs to. The corner slots exist only with Tap Zones on "Corners only".
inline int tapSlot(const int x, const int y, const int width, const int height, const uint8_t tapZones) {
  if (inCentre(x, y, width, height)) return Centre;
  return tapZones == 1 ? cornerAt(x, y, width, height) : kNoSlot;
}

// The slot a swipe belongs to. A swipe that starts in the top, left or bottom edge band and moves away from it
// belongs to the global gestures (light panel or menu, Back, Home), never to a slot.
inline int swipeSlot(const Swipe dir, const bool edgeSwipe) {
  if (edgeSwipe) return kNoSlot;
  switch (dir) {
    case Swipe::Left:
      return SwipeLeft;
    case Swipe::Right:
      return SwipeRight;
    case Swipe::Up:
      return SwipeUp;
    case Swipe::Down:
      return SwipeDown;
    default:
      return kNoSlot;
  }
}

// What a tap does with every slot at Default: the centre opens the reader menu when Show Reader Menu is Tap,
// otherwise the page-turn zones of ReaderUtils::detectTouchPageTurn apply.
inline Action legacyTap(const Legacy& s, const int x, const int y, const int width, const int height) {
  if (s.showReaderMenu == kMenuTap && inCentre(x, y, width, height)) return ReaderMenu;
  const bool nextTaps = allowsTap(s.nextGesture);
  const bool prevTaps = allowsTap(s.prevGesture);
  if (!nextTaps && !prevTaps) return None;
  if (s.tapZones == 1 && cornerAt(x, y, width, height) == kNoSlot) return None;
  // The whole page goes to the only direction that takes taps. When both do, the split is at the left third; a
  // right-to-left book and Inverted Tap each turn it round.
  const bool inverted = (s.nextGesture == kInvertedTap || s.prevGesture == kInvertedTap) != s.rtlBook;
  const bool nextZone = inverted ? x < (width * 2) / 3 : x >= width / 3;
  if (nextTaps && (!prevTaps || nextZone)) return NextPage;
  if (prevTaps && (!nextTaps || !nextZone)) return PrevPage;
  return None;
}

// What a swipe does with every slot at Default: left and right turn pages, up and down do nothing.
inline Action legacySwipe(const Legacy& s, const Swipe dir) {
  const Swipe nextDir = s.rtlBook ? Swipe::Right : Swipe::Left;
  const Swipe prevDir = s.rtlBook ? Swipe::Left : Swipe::Right;
  if (dir == nextDir && allowsSwipe(s.nextGesture)) return NextPage;
  if (dir == prevDir && allowsSwipe(s.prevGesture)) return PrevPage;
  return None;
}

// A value read from settings.json: anything unknown (a newer version's action) becomes Default.
inline uint8_t sanitizeSlot(const uint8_t stored) {
  return stored < kActionCount ? stored : static_cast<uint8_t>(Default);
}

inline Action resolve(const uint8_t stored, const Action legacy) {
  const uint8_t value = sanitizeSlot(stored);
  return value == Default ? legacy : static_cast<Action>(value);
}

// True when at least one slot is not Default.
inline bool anyCustom(const uint8_t* table) {
  for (int i = 0; i < kSlotCount; ++i) {
    if (sanitizeSlot(table[i]) != Default) return true;
  }
  return false;
}

inline Action tapAction(const uint8_t* table, const Legacy& s, const int x, const int y, const int width,
                        const int height) {
  const int slot = tapSlot(x, y, width, height, s.tapZones);
  return resolve(slot == kNoSlot ? static_cast<uint8_t>(Default) : table[slot], legacyTap(s, x, y, width, height));
}

inline Action swipeAction(const uint8_t* table, const Legacy& s, const Swipe dir, const bool edgeSwipe) {
  const int slot = swipeSlot(dir, edgeSwipe);
  return resolve(slot == kNoSlot ? static_cast<uint8_t>(Default) : table[slot], legacySwipe(s, dir));
}

inline void reset(uint8_t* table, uint8_t* quickMenu) {
  for (int i = 0; i < kSlotCount; ++i) table[i] = Default;
  for (int i = 0; i < kQuickMenuSize; ++i) quickMenu[i] = Default;
}

inline const char* slotKey(const int slot) {
  static constexpr const char* kKeys[kSlotCount] = {
      "gesture.cornerTL",  "gesture.cornerTR",   "gesture.cornerBL", "gesture.cornerBR",  "gesture.centre",
      "gesture.swipeLeft", "gesture.swipeRight", "gesture.swipeUp",  "gesture.swipeDown",
  };
  return slot >= 0 && slot < kSlotCount ? kKeys[slot] : "";
}

inline const char* quickMenuKey(const int index) {
  static constexpr const char* kKeys[kQuickMenuSize] = {
      "quickMenu.1", "quickMenu.2", "quickMenu.3", "quickMenu.4", "quickMenu.5", "quickMenu.6",
  };
  return index >= 0 && index < kQuickMenuSize ? kKeys[index] : "";
}

// --- Quick Menu -----------------------------------------------------------------------------------------------

// What a Quick Menu place can hold: Default is an empty place; None and the Quick Menu itself make no sense there.
constexpr Action kQuickChoices[] = {Default,    NextPage,         PrevPage,   NextChapter, PrevChapter,
                                    ReaderMenu, ToggleNight,      LightPanel, Bookmark,    Refresh,
                                    GoHome,     ToggleFrontlight, Screenshot};
constexpr int kQuickChoiceCount = static_cast<int>(sizeof(kQuickChoices) / sizeof(kQuickChoices[0]));

// The list shown while every place is empty.
constexpr Action kDefaultQuickMenu[kQuickMenuSize] = {ToggleNight, ToggleFrontlight, LightPanel,
                                                      Bookmark,    Refresh,          GoHome};

inline uint8_t sanitizeQuick(const uint8_t stored) {
  for (int i = 0; i < kQuickChoiceCount; ++i) {
    if (kQuickChoices[i] == stored) return stored;
  }
  return Default;
}

// Fills `out` (room for kQuickMenuSize) with the Quick Menu's actions in order and returns how many: the chosen
// ones, or the default six when none is chosen.
inline int quickMenuItems(const uint8_t* stored, Action* out) {
  int count = 0;
  for (int i = 0; i < kQuickMenuSize; ++i) {
    const uint8_t value = sanitizeQuick(stored[i]);
    if (value != Default) out[count++] = static_cast<Action>(value);
  }
  if (count > 0) return count;
  for (int i = 0; i < kQuickMenuSize; ++i) out[i] = kDefaultQuickMenu[i];
  return kQuickMenuSize;
}

}  // namespace gestures
