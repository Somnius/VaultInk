#include "SearchWordSettingsActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

const char* SearchWordSettingsActivity::headerTitle() const {
  return typeOnly ? tr(STR_SEARCH_TYPE) : tr(STR_SEARCH_WORD);
}

void SearchWordSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  app.clearTapFlash();
  if (typeOnly) {
    SETTINGS.searchType = static_cast<uint8_t>(index);
  } else if (index == 0) {
    SETTINGS.searchInMoreMenu = SETTINGS.searchInMoreMenu ? 0 : 1;
  } else if (index == 1) {
    SETTINGS.searchScope = SETTINGS.searchScope ? 0 : 1;
  } else if (index == 3) {
    SETTINGS.searchIndex = SETTINGS.searchIndex ? 0 : 1;
  } else {
    SETTINGS.searchType = SETTINGS.searchType ? 0 : 1;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

void SearchWordSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const char* typeName = SETTINGS.searchType ? tr(STR_SEARCH_TYPE_EXACT) : tr(STR_SEARCH_TYPE_PARTIAL);
  for (int i = 0; i < listCount(); ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  if (typeOnly) {
    // Two choices, the chosen one carries the switch.
    rowItems[0].label = tr(STR_SEARCH_TYPE_PARTIAL);
    rowItems[1].label = tr(STR_SEARCH_TYPE_EXACT);
    for (int i = 0; i < 2; ++i) {
      rowItems[i].toggle = true;
      rowItems[i].toggleChecked = SETTINGS.searchType == i;
    }
  } else {
    rowItems[0].label = tr(STR_SEARCH_IN_MORE_MENU);
    rowItems[0].toggle = true;
    rowItems[0].toggleChecked = SETTINGS.searchInMoreMenu != 0;
    rowItems[1].label = tr(STR_SEARCH_SCOPE);
    rowItems[1].value = SETTINGS.searchScope ? tr(STR_SEARCH_SCOPE_TO_HERE) : tr(STR_SEARCH_SCOPE_BOOK);
    rowItems[2].label = tr(STR_SEARCH_TYPE);
    rowItems[2].value = typeName;
    rowItems[3].label = tr(STR_SEARCH_INDEX);
    rowItems[3].toggle = true;
    rowItems[3].toggleChecked = SETTINGS.searchIndex != 0;
  }
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
