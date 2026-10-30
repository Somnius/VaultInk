#include "CbzChapterActivity.h"

#include <I18n.h>

#include "activities/ActivityResult.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void CbzChapterActivity::onEnter() {
  UiListActivity::onEnter();
  rows.clear();
  int current = 0;
  for (size_t i = 0; i < chapters.size(); ++i) {
    fui::ListItem item;
    item.label = chapters[i].title.empty() ? tr(STR_UNNAMED) : chapters[i].title.c_str();
    item.actionValue = static_cast<int16_t>(i);
    rows.push_back(item);
    if (chapters[i].firstPage <= currentPage) current = static_cast<int>(i);
  }
  nav.selected = current;
  requestUpdate();
}

const char* CbzChapterActivity::headerTitle() const { return tr(STR_SELECT_CHAPTER); }

void CbzChapterActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(chapters.size())) return;
  app.clearTapFlash();
  setResult(PageResult{static_cast<uint32_t>(chapters[static_cast<size_t>(index)].firstPage)});
  finish();
}

void CbzChapterActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
