#include "HomeRowsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/VaultInkHomeUi.h"

namespace fui = freeink::ui;

void HomeRowsActivity::onEnter() {
  UiListActivity::onEnter();
  order = homerows::decode(SETTINGS.homeRowOrder);
  picked = -1;
}

void HomeRowsActivity::onExit() {
  const uint32_t stored = homerows::encode(order);
  if (stored != SETTINGS.homeRowOrder) {
    SETTINGS.homeRowOrder = stored;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

const char* HomeRowsActivity::headerTitle() const { return tr(STR_HOME_ROWS); }

void HomeRowsActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  if (index == RESET_ROW) {
    order = homerows::DEFAULT_ORDER;
    picked = -1;
  } else if (picked < 0) {
    picked = index;
  } else {
    homerows::move(order, picked, index);  // tapping the picked row again just puts it down
    nav.selected = index;
    picked = -1;
  }
  requestUpdate();
}

void HomeRowsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const bool plugins = SETTINGS.pluginsEnabled != 0;
  for (int i = 0; i < homerows::COUNT; ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].label = VaultInkHomeUi::rowLabel(static_cast<VaultInkHomeUi::Row>(order[i]), plugins);
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
  props.count = homerows::COUNT + 1;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
