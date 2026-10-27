#include "PaperCycleActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void PaperCycleActivity::onEnter() {
  UiListActivity::onEnter();
  workingMask = paper_texture::cycleMask(SETTINGS.paperCycle);
  for (int i = 0; i < paper_texture::STYLE_COUNT; ++i) {
    rowItems[i].label = I18N.get(paper_texture::NAMES[i]);
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
}

void PaperCycleActivity::onExit() {
  // All checked is stored as 0, so new backgrounds join the cycle.
  const uint16_t stored = workingMask == paper_texture::ALL_STYLES ? 0 : workingMask;
  if (stored != SETTINGS.paperCycle) {
    SETTINGS.paperCycle = stored;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

const char* PaperCycleActivity::headerTitle() const { return tr(STR_PAPER_CYCLE); }

void PaperCycleActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  const uint16_t next = static_cast<uint16_t>(workingMask ^ (1u << index));
  if (next != 0) workingMask = next;  // the last checked one stays
  requestUpdate();
}

void PaperCycleActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < paper_texture::STYLE_COUNT; ++i) {
    GUI.setCheckboxRow(rowItems[i], (workingMask & (1u << i)) != 0);
  }
  fui::ListProps props;
  props.items = rowItems;
  props.count = paper_texture::STYLE_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
