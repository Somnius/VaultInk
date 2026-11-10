#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <string>

#include "Gestures.h"

using namespace gestures;

namespace {

// The reader's touch decoding as it was before the gesture table (ReaderUtils::detectTouchPageTurn and
// isTouchMenuTap, 0.73.1), copied here with the settings as plain fields. The table's Default must match it.
struct Old {
  bool next = false;
  bool prev = false;
  bool menu = false;
};

bool oldAllowsSwipe(const uint8_t g) { return g == 0 || g == 2; }
bool oldAllowsTap(const uint8_t g) { return g == 0 || g == 1 || g == 3; }

Old oldSwipe(const Legacy& s, const Swipe dir) {
  Old r;
  if (dir == Swipe::None) return r;
  r.next = dir == (s.rtlBook ? Swipe::Right : Swipe::Left) && oldAllowsSwipe(s.nextGesture);
  r.prev = dir == (s.rtlBook ? Swipe::Left : Swipe::Right) && oldAllowsSwipe(s.prevGesture);
  return r;
}

Old oldTap(const Legacy& s, const int x, const int y, const int width, const int height) {
  Old r;
  // isTouchMenuTap: checked by the reader before the page turn.
  const int zoneWidth = width / 3;
  const int zoneHeight = height / 3;
  r.menu =
      s.showReaderMenu == 1 && x >= zoneWidth && x < width - zoneWidth && y >= zoneHeight && y < height - zoneHeight;

  const bool nextTaps = oldAllowsTap(s.nextGesture);
  const bool prevTaps = oldAllowsTap(s.prevGesture);
  if (!nextTaps && !prevTaps) return r;
  if (s.showReaderMenu == 1 && x >= width / 3 && x < width - width / 3 && y >= height / 3 && y < height - height / 3) {
    return r;
  }
  if (s.tapZones == 1) {
    const int corner = std::min(width, height) / 6;
    const bool left = x < corner;
    const bool right = x >= width - corner;
    const bool topOrBottom = y < corner || y >= height - corner;
    if (!topOrBottom || (!left && !right)) return r;
  }
  const bool inverted = (s.nextGesture == 3 || s.prevGesture == 3) != s.rtlBook;
  const bool nextZone = inverted ? x < (width * 2) / 3 : x >= width / 3;
  r.next = nextTaps && (!prevTaps || nextZone);
  r.prev = prevTaps && (!nextTaps || !nextZone);
  return r;
}

Action expected(const Old& old) {
  if (old.menu) return ReaderMenu;  // the menu wins in the reader's loop
  if (old.next) return NextPage;
  if (old.prev) return PrevPage;
  return None;
}

// Every combination of the older settings, for both book directions.
template <typename Fn>
void forEveryLegacy(Fn&& fn) {
  for (uint8_t next = 0; next <= 4; ++next) {
    for (uint8_t prev = 0; prev <= 4; ++prev) {
      for (uint8_t zones = 0; zones <= 1; ++zones) {
        for (uint8_t menu = 0; menu <= 2; ++menu) {
          for (int rtl = 0; rtl <= 1; ++rtl) {
            Legacy s;
            s.nextGesture = next;
            s.prevGesture = prev;
            s.tapZones = zones;
            s.showReaderMenu = menu;
            s.rtlBook = rtl != 0;
            fn(s);
          }
        }
      }
    }
  }
}

std::string describe(const Legacy& s) {
  return "next=" + std::to_string(s.nextGesture) + " prev=" + std::to_string(s.prevGesture) +
         " zones=" + std::to_string(s.tapZones) + " menu=" + std::to_string(s.showReaderMenu) +
         " rtl=" + std::to_string(s.rtlBook);
}

struct Screen {
  int width;
  int height;
};
constexpr Screen kScreens[] = {{480, 800}, {800, 480}, {528, 792}, {7, 5}};
constexpr Swipe kSwipes[] = {Swipe::None, Swipe::Left, Swipe::Right, Swipe::Up, Swipe::Down};

}  // namespace

TEST(GesturesLegacy, OldNeverTurnsBothWays) {
  forEveryLegacy([](const Legacy& s) {
    for (int x = 0; x < 480; x += 7) {
      const Old old = oldTap(s, x, 10, 480, 800);
      EXPECT_FALSE(old.next && old.prev) << describe(s);
    }
    for (const Swipe dir : kSwipes) {
      const Old old = oldSwipe(s, dir);
      EXPECT_FALSE(old.next && old.prev) << describe(s);
    }
  });
}

TEST(GesturesLegacy, DefaultTableTapsMatchTheOldReader) {
  const uint8_t table[kSlotCount] = {};
  forEveryLegacy([&](const Legacy& s) {
    for (const Screen& screen : kScreens) {
      const int stepX = std::max(1, screen.width / 61);
      const int stepY = std::max(1, screen.height / 67);
      for (int y = 0; y < screen.height; y += stepY) {
        for (int x = 0; x < screen.width; x += stepX) {
          const Action want = expected(oldTap(s, x, y, screen.width, screen.height));
          ASSERT_EQ(legacyTap(s, x, y, screen.width, screen.height), want)
              << describe(s) << " at " << x << "," << y << " on " << screen.width << "x" << screen.height;
          ASSERT_EQ(tapAction(table, s, x, y, screen.width, screen.height), want) << describe(s);
        }
      }
      // The exact edges of the thirds and of the corner squares.
      const int corner = std::min(screen.width, screen.height) / 6;
      const int xs[] = {0,
                        corner - 1,
                        corner,
                        screen.width / 3 - 1,
                        screen.width / 3,
                        screen.width * 2 / 3 - 1,
                        screen.width * 2 / 3,
                        screen.width - screen.width / 3 - 1,
                        screen.width - screen.width / 3,
                        screen.width - corner - 1,
                        screen.width - corner,
                        screen.width - 1};
      const int ys[] = {0,
                        corner - 1,
                        corner,
                        screen.height / 3 - 1,
                        screen.height / 3,
                        screen.height - screen.height / 3 - 1,
                        screen.height - screen.height / 3,
                        screen.height - corner - 1,
                        screen.height - corner,
                        screen.height - 1};
      for (const int y : ys) {
        for (const int x : xs) {
          if (x < 0 || y < 0 || x >= screen.width || y >= screen.height) continue;
          ASSERT_EQ(tapAction(table, s, x, y, screen.width, screen.height),
                    expected(oldTap(s, x, y, screen.width, screen.height)))
              << describe(s) << " at " << x << "," << y << " on " << screen.width << "x" << screen.height;
        }
      }
    }
  });
}

TEST(GesturesLegacy, DefaultTableSwipesMatchTheOldReader) {
  const uint8_t table[kSlotCount] = {};
  forEveryLegacy([&](const Legacy& s) {
    for (const Swipe dir : kSwipes) {
      const Action want = expected(oldSwipe(s, dir));
      EXPECT_EQ(legacySwipe(s, dir), want) << describe(s);
      EXPECT_EQ(swipeAction(table, s, dir, false), want) << describe(s);
      EXPECT_EQ(swipeAction(table, s, dir, true), want) << describe(s);
    }
  });
}

TEST(GesturesLegacy, UpAndDownDidNothing) {
  forEveryLegacy([](const Legacy& s) {
    EXPECT_EQ(legacySwipe(s, Swipe::Up), None);
    EXPECT_EQ(legacySwipe(s, Swipe::Down), None);
    EXPECT_EQ(legacySwipe(s, Swipe::None), None);
  });
}

TEST(GesturesSlots, CentreAndCorners) {
  EXPECT_EQ(tapSlot(240, 400, 480, 800, 0), Centre);
  EXPECT_EQ(tapSlot(240, 400, 480, 800, 1), Centre);
  EXPECT_EQ(tapSlot(159, 400, 480, 800, 0), kNoSlot);
  EXPECT_EQ(tapSlot(160, 266, 480, 800, 0), Centre);
  EXPECT_EQ(tapSlot(319, 533, 480, 800, 0), Centre);
  EXPECT_EQ(tapSlot(320, 400, 480, 800, 0), kNoSlot);
  // Corner squares are 80 px on a 480x800 screen, and only exist with Tap Zones on corners.
  EXPECT_EQ(tapSlot(10, 10, 480, 800, 0), kNoSlot);
  EXPECT_EQ(tapSlot(10, 10, 480, 800, 1), CornerTL);
  EXPECT_EQ(tapSlot(79, 79, 480, 800, 1), CornerTL);
  EXPECT_EQ(tapSlot(80, 79, 480, 800, 1), kNoSlot);
  EXPECT_EQ(tapSlot(79, 80, 480, 800, 1), kNoSlot);
  EXPECT_EQ(tapSlot(400, 0, 480, 800, 1), CornerTR);
  EXPECT_EQ(tapSlot(399, 0, 480, 800, 1), kNoSlot);
  EXPECT_EQ(tapSlot(0, 720, 480, 800, 1), CornerBL);
  EXPECT_EQ(tapSlot(0, 719, 480, 800, 1), kNoSlot);
  EXPECT_EQ(tapSlot(479, 799, 480, 800, 1), CornerBR);
  // Landscape: still a sixth of the short side.
  EXPECT_EQ(tapSlot(79, 79, 800, 480, 1), CornerTL);
  EXPECT_EQ(tapSlot(720, 400, 800, 480, 1), CornerBR);
  EXPECT_EQ(tapSlot(719, 400, 800, 480, 1), kNoSlot);
}

TEST(GesturesSlots, CentreNeverOverlapsACorner) {
  for (const Screen& screen : kScreens) {
    for (int y = 0; y < screen.height; ++y) {
      for (int x = 0; x < screen.width; ++x) {
        if (inCentre(x, y, screen.width, screen.height)) {
          ASSERT_EQ(cornerAt(x, y, screen.width, screen.height), kNoSlot) << x << "," << y;
        }
      }
    }
  }
}

TEST(GesturesSlots, SwipesAndEdges) {
  EXPECT_EQ(swipeSlot(Swipe::Left, false), SwipeLeft);
  EXPECT_EQ(swipeSlot(Swipe::Right, false), SwipeRight);
  EXPECT_EQ(swipeSlot(Swipe::Up, false), SwipeUp);
  EXPECT_EQ(swipeSlot(Swipe::Down, false), SwipeDown);
  EXPECT_EQ(swipeSlot(Swipe::None, false), kNoSlot);
  for (const Swipe dir : kSwipes) EXPECT_EQ(swipeSlot(dir, true), kNoSlot);
}

TEST(GesturesResolve, DefaultFollowsLegacyAndCustomWins) {
  EXPECT_EQ(resolve(Default, NextPage), NextPage);
  EXPECT_EQ(resolve(Default, None), None);
  EXPECT_EQ(resolve(None, NextPage), None);
  EXPECT_EQ(resolve(GoHome, NextPage), GoHome);
  EXPECT_EQ(resolve(kActionCount, PrevPage), PrevPage);  // unknown value: Default
  EXPECT_EQ(resolve(255, ReaderMenu), ReaderMenu);
}

TEST(GesturesResolve, CentreHasOneOwner) {
  uint8_t table[kSlotCount] = {};
  Legacy s;
  s.nextGesture = kTapAndSwipe;
  s.prevGesture = kTapAndSwipe;
  s.showReaderMenu = kMenuTap;
  EXPECT_EQ(tapAction(table, s, 240, 400, 480, 800), ReaderMenu);
  table[Centre] = QuickMenu;
  EXPECT_EQ(tapAction(table, s, 240, 400, 480, 800), QuickMenu);
  table[Centre] = None;
  EXPECT_EQ(tapAction(table, s, 240, 400, 480, 800), None);
  // Outside the centre the older zones still apply.
  EXPECT_EQ(tapAction(table, s, 400, 400, 480, 800), NextPage);
  EXPECT_EQ(tapAction(table, s, 10, 400, 480, 800), PrevPage);
  // With the menu tap off, a Default centre is part of the page-turn zones again.
  table[Centre] = Default;
  s.showReaderMenu = 0;
  EXPECT_EQ(tapAction(table, s, 240, 400, 480, 800), NextPage);
}

TEST(GesturesResolve, CornersOnlyWithCornerZones) {
  uint8_t table[kSlotCount] = {};
  table[CornerTL] = Bookmark;
  table[CornerBR] = None;
  Legacy s;
  s.nextGesture = kTapOnly;
  s.prevGesture = kTapOnly;
  s.tapZones = 0;
  EXPECT_EQ(tapAction(table, s, 5, 5, 480, 800), PrevPage);  // whole-page zones: the corner slots are inert
  EXPECT_EQ(tapAction(table, s, 475, 795, 480, 800), NextPage);
  s.tapZones = 1;
  EXPECT_EQ(tapAction(table, s, 5, 5, 480, 800), Bookmark);
  EXPECT_EQ(tapAction(table, s, 475, 795, 480, 800), None);
  EXPECT_EQ(tapAction(table, s, 475, 5, 480, 800), NextPage);  // a corner left at Default
  EXPECT_EQ(tapAction(table, s, 5, 795, 480, 800), PrevPage);
  EXPECT_EQ(tapAction(table, s, 5, 300, 480, 800), None);  // not a corner
}

TEST(GesturesResolve, EdgeSwipesStayWithTheOldSettings) {
  uint8_t table[kSlotCount] = {};
  table[SwipeRight] = GoHome;
  table[SwipeDown] = ToggleNight;
  table[SwipeUp] = QuickMenu;
  Legacy s;  // swipe only, both ways
  EXPECT_EQ(swipeAction(table, s, Swipe::Right, false), GoHome);
  EXPECT_EQ(swipeAction(table, s, Swipe::Right, true), PrevPage);  // from the left edge: as before
  EXPECT_EQ(swipeAction(table, s, Swipe::Down, false), ToggleNight);
  EXPECT_EQ(swipeAction(table, s, Swipe::Down, true), None);  // from the top edge: the global gesture's
  EXPECT_EQ(swipeAction(table, s, Swipe::Up, true), None);
  EXPECT_EQ(swipeAction(table, s, Swipe::Left, false), NextPage);
}

TEST(GesturesResolve, NoneEverywhereIsValid) {
  uint8_t table[kSlotCount];
  for (uint8_t& value : table) value = None;
  EXPECT_TRUE(anyCustom(table));
  forEveryLegacy([&](const Legacy& s) {
    for (const Swipe dir : kSwipes) EXPECT_EQ(swipeAction(table, s, dir, false), None);
    EXPECT_EQ(tapAction(table, s, 240, 400, 480, 800), None);
    if (s.tapZones == 1) {
      EXPECT_EQ(tapAction(table, s, 5, 5, 480, 800), None);
    }
  });
}

TEST(GesturesTable, AnyCustomAndReset) {
  uint8_t table[kSlotCount] = {};
  uint8_t quick[kQuickMenuSize] = {};
  EXPECT_FALSE(anyCustom(table));
  table[SwipeUp] = 200;  // unknown: counts as Default
  EXPECT_FALSE(anyCustom(table));
  table[SwipeUp] = QuickMenu;
  quick[2] = GoHome;
  EXPECT_TRUE(anyCustom(table));
  reset(table, quick);
  EXPECT_FALSE(anyCustom(table));
  for (const uint8_t value : quick) EXPECT_EQ(value, Default);
}

TEST(GesturesTable, KeysAreDistinctAndNamed) {
  std::set<std::string> keys;
  for (int i = 0; i < kSlotCount; ++i) {
    const std::string key = slotKey(i);
    EXPECT_EQ(key.rfind("gesture.", 0), 0u) << key;
    keys.insert(key);
  }
  for (int i = 0; i < kQuickMenuSize; ++i) keys.insert(quickMenuKey(i));
  EXPECT_EQ(keys.size(), static_cast<size_t>(kSlotCount + kQuickMenuSize));
  EXPECT_STREQ(slotKey(CornerTL), "gesture.cornerTL");
  EXPECT_STREQ(slotKey(SwipeDown), "gesture.swipeDown");
  EXPECT_STREQ(quickMenuKey(0), "quickMenu.1");
  EXPECT_STREQ(slotKey(-1), "");
  EXPECT_STREQ(slotKey(kSlotCount), "");
  EXPECT_STREQ(quickMenuKey(kQuickMenuSize), "");
}

TEST(GesturesTable, StoredValuesNeverMove) {
  // settings.json holds these numbers.
  EXPECT_EQ(Default, 0);
  EXPECT_EQ(None, 1);
  EXPECT_EQ(NextPage, 2);
  EXPECT_EQ(PrevPage, 3);
  EXPECT_EQ(NextChapter, 4);
  EXPECT_EQ(PrevChapter, 5);
  EXPECT_EQ(ReaderMenu, 6);
  EXPECT_EQ(QuickMenu, 7);
  EXPECT_EQ(ToggleNight, 8);
  EXPECT_EQ(ToggleFrontlight, 9);
  EXPECT_EQ(LightPanel, 10);
  EXPECT_EQ(Bookmark, 11);
  EXPECT_EQ(Refresh, 12);
  EXPECT_EQ(GoHome, 13);
  EXPECT_EQ(Screenshot, 14);
}

TEST(GesturesTable, SanitizeSlot) {
  for (int value = 0; value < 256; ++value) {
    const uint8_t clean = sanitizeSlot(static_cast<uint8_t>(value));
    EXPECT_EQ(clean, value < kActionCount ? value : Default);
  }
}

TEST(GesturesQuickMenu, EmptyShowsTheDefaultSix) {
  const uint8_t quick[kQuickMenuSize] = {};
  Action items[kQuickMenuSize];
  ASSERT_EQ(quickMenuItems(quick, items), kQuickMenuSize);
  EXPECT_EQ(items[0], ToggleNight);
  EXPECT_EQ(items[1], ToggleFrontlight);
  EXPECT_EQ(items[2], LightPanel);
  EXPECT_EQ(items[3], Bookmark);
  EXPECT_EQ(items[4], Refresh);
  EXPECT_EQ(items[5], GoHome);
}

TEST(GesturesQuickMenu, ChosenOnesInOrderWithoutGaps) {
  const uint8_t quick[kQuickMenuSize] = {Default, GoHome, Default, Screenshot, NextChapter, Default};
  Action items[kQuickMenuSize];
  ASSERT_EQ(quickMenuItems(quick, items), 3);
  EXPECT_EQ(items[0], GoHome);
  EXPECT_EQ(items[1], Screenshot);
  EXPECT_EQ(items[2], NextChapter);
}

TEST(GesturesQuickMenu, RejectsNoneItselfAndUnknown) {
  EXPECT_EQ(sanitizeQuick(None), Default);
  EXPECT_EQ(sanitizeQuick(QuickMenu), Default);
  EXPECT_EQ(sanitizeQuick(kActionCount), Default);
  EXPECT_EQ(sanitizeQuick(255), Default);
  EXPECT_EQ(sanitizeQuick(Bookmark), Bookmark);
  const uint8_t quick[kQuickMenuSize] = {None, QuickMenu, 99, Default, Default, Default};
  Action items[kQuickMenuSize];
  EXPECT_EQ(quickMenuItems(quick, items), kQuickMenuSize);  // nothing valid: the default six
  EXPECT_EQ(items[0], ToggleNight);
}

TEST(GesturesQuickMenu, ChoicesCoverEveryActionButNoneAndItself) {
  std::set<int> choices(std::begin(kQuickChoices), std::end(kQuickChoices));
  EXPECT_EQ(static_cast<int>(choices.size()), kQuickChoiceCount);
  EXPECT_EQ(kQuickChoices[0], Default);
  for (int action = 0; action < kActionCount; ++action) {
    const bool want = action != None && action != QuickMenu;
    EXPECT_EQ(choices.count(action) == 1, want) << action;
  }
  for (const Action action : kDefaultQuickMenu) EXPECT_EQ(sanitizeQuick(action), action);
}
