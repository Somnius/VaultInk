#include "CbzChapterActivity.h"

#include <I18n.h>

#include "activities/ActivityResult.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void CbzChapterActivity::onEnter() {
  UiListActivity::onEnter();
  rows.clear();
  directionLabel = std::string(tr(STR_CBZ_RIGHT_TO_LEFT)) + ": " + (rightToLeft ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
  fui::ListItem direction;
  direction.label = directionLabel.c_str();
  direction.actionValue = 0;
  rows.push_back(direction);
  int current = 0;
  for (size_t i = 0; i < chapters.size(); ++i) {
    fui::ListItem item;
    item.label = chapters[i].title.empty() ? tr(STR_UNNAMED) : chapters[i].title.c_str();
    item.actionValue = static_cast<int16_t>(i + 1);
    if (i == 0) item.sectionHeading = tr(STR_SELECT_CHAPTER);
    rows.push_back(item);
    if (chapters[i].firstPage <= currentPage) current = static_cast<int>(i + 1);
  }
  nav.selected = current;
  requestUpdate();
}

const char* CbzChapterActivity::headerTitle() const { return tr(STR_CBZ_COMIC); }

void CbzChapterActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  setResult(PageResult{index == 0 ? TOGGLE_RIGHT_TO_LEFT
                                  : static_cast<uint32_t>(chapters[static_cast<size_t>(index - 1)].firstPage)});
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
