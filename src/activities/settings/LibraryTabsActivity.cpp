#include "LibraryTabsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
const char* tabName(const uint8_t id) {
  switch (id) {
    case 0:
      return tr(STR_LIBRARY_TAB_RECENT);
    case 1:
      return tr(STR_LIBRARY_TAB_TITLE);
    case 2:
      return tr(STR_LIBRARY_TAB_AUTHOR);
    case 3:
      return tr(STR_LIBRARY_TAB_FAVOURITES);
    default:
      return tr(STR_LIBRARY_TAB_FOLDERS);
  }
}
}  // namespace

void LibraryTabsActivity::onEnter() {
  UiListActivity::onEnter();
  order = libtabs::decode(SETTINGS.libraryTabOrder);
  picked = -1;
}

void LibraryTabsActivity::onExit() {
  const uint32_t stored = libtabs::encode(order);
  if (stored != SETTINGS.libraryTabOrder) {
    SETTINGS.libraryTabOrder = stored;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

const char* LibraryTabsActivity::headerTitle() const { return tr(STR_LIBRARY_TAB_ORDER); }

void LibraryTabsActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  if (index == RESET_ROW) {
    order = libtabs::DEFAULT_ORDER;
    picked = -1;
  } else if (picked < 0) {
    picked = index;
  } else {
    libtabs::move(order, picked, index);  // tapping the picked row again just puts it down
    nav.selected = index;
    picked = -1;
  }
  requestUpdate();
}

void LibraryTabsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < libtabs::COUNT; ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].label = tabName(order[i]);
    rowItems[i].actionValue = static_cast<int16_t>(i);
    if (i == picked) {
      rowItems[i].subtitle = tr(STR_HOME_ROWS_PICKED);
    } else if (picked < 0 && i == 0) {
      rowItems[i].subtitle = tr(STR_HOME_ROWS_HINT);
    }
  }
  rowItems[RESET_ROW] = fui::ListItem{};
  rowItems[RESET_ROW].label = tr(STR_HOME_ROWS_RESET);
  rowItems[RESET_ROW].actionValue = static_cast<int16_t>(RESET_ROW);

  fui::ListProps props;
  props.items = rowItems;
  props.count = libtabs::COUNT + 1;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
