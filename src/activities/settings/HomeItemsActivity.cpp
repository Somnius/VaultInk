#include "HomeItemsActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "components/VaultInkHomeUi.h"

namespace fui = freeink::ui;

void HomeItemsActivity::onEnter() {
  UiListActivity::onEnter();
  hidden = SETTINGS.homeRowsHidden;
  count = 0;
  for (const uint8_t id : homerows::decode(SETTINGS.homeRowOrder)) {
    if (homerows::canHide(id)) ids[count++] = id;
  }
}

void HomeItemsActivity::onExit() {
  if (hidden != SETTINGS.homeRowsHidden) {
    SETTINGS.homeRowsHidden = hidden;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

const char* HomeItemsActivity::headerTitle() const { return tr(STR_HOME_ITEMS); }

void HomeItemsActivity::activateIndex(const int index) {
  if (index < 0 || index >= count) return;
  nav.selected = index;
  app.clearTapFlash();
  hidden ^= static_cast<uint16_t>(1u << ids[index]);
  requestUpdate();
}

void HomeItemsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const bool plugins = SETTINGS.pluginsEnabled != 0;
  for (int i = 0; i < count; ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].label = VaultInkHomeUi::rowLabel(static_cast<VaultInkHomeUi::Row>(ids[i]), plugins);
    rowItems[i].toggle = true;
    rowItems[i].toggleChecked = !homerows::hidden(hidden, ids[i]);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
