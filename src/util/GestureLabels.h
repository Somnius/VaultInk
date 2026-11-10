#pragma once

#include <Gestures.h>

#include "I18nKeys.h"

// The names of the reader gesture actions and slots (lib/Gestures), shared by Settings > Reader > Gestures and
// the reader's Quick Menu.
namespace gesturelabels {

inline StrId action(const uint8_t value) {
  switch (gestures::sanitizeSlot(value)) {
    case gestures::None:
      return StrId::STR_NONE_OPT;
    case gestures::NextPage:
      return StrId::STR_NEXT_PAGE;
    case gestures::PrevPage:
      return StrId::STR_PREV_PAGE;
    case gestures::NextChapter:
      return StrId::STR_BT_NEXT_CHAPTER;
    case gestures::PrevChapter:
      return StrId::STR_BT_PREV_CHAPTER;
    case gestures::ReaderMenu:
      return StrId::STR_READER_MENU;
    case gestures::QuickMenu:
      return StrId::STR_QUICK_MENU;
    case gestures::ToggleNight:
      return StrId::STR_NIGHT_MODE;
    case gestures::ToggleFrontlight:
      return StrId::STR_FRONTLIGHT;
    case gestures::LightPanel:
      return StrId::STR_GESTURE_LIGHT_PANEL;
    case gestures::Bookmark:
      return StrId::STR_TOGGLE_BOOKMARK;
    case gestures::Refresh:
      return StrId::STR_GESTURE_REFRESH;
    case gestures::GoHome:
      return StrId::STR_GO_HOME_BUTTON;
    case gestures::Screenshot:
      return StrId::STR_SCREENSHOT_BUTTON;
    case gestures::Default:
    default:
      return StrId::STR_DEFAULT_VALUE;
  }
}

inline StrId slot(const int slot) {
  switch (slot) {
    case gestures::CornerTL:
      return StrId::STR_GESTURE_CORNER_TL;
    case gestures::CornerTR:
      return StrId::STR_GESTURE_CORNER_TR;
    case gestures::CornerBL:
      return StrId::STR_GESTURE_CORNER_BL;
    case gestures::CornerBR:
      return StrId::STR_GESTURE_CORNER_BR;
    case gestures::Centre:
      return StrId::STR_GESTURE_CENTRE;
    case gestures::SwipeLeft:
      return StrId::STR_GESTURE_SWIPE_LEFT;
    case gestures::SwipeRight:
      return StrId::STR_GESTURE_SWIPE_RIGHT;
    case gestures::SwipeUp:
      return StrId::STR_GESTURE_SWIPE_UP;
    case gestures::SwipeDown:
    default:
      return StrId::STR_GESTURE_SWIPE_DOWN;
  }
}

}  // namespace gesturelabels
