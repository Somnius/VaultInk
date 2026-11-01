#include "PaperCycleActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "PaperContrastActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void PaperCycleActivity::onEnter() {
  UiListActivity::onEnter();
  workingMask = paper_texture::cycleMask(SETTINGS.paperCycle);
  for (int i = 0; i < paper_texture::STYLE_COUNT; ++i) {
    rowItems[i + 1].label = I18N.get(paper_texture::NAMES[i]);
    rowItems[i + 1].actionValue = static_cast<int16_t>(i + 1);
  }
  rowItems[0].label = tr(STR_PAPER_CONTRAST);
  rowItems[0].actionValue = 0;
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
  if (index == 0) {
    if (auto contrast = makeUniqueNoThrow<PaperContrastActivity>(renderer, mappedInput)) {
      startActivityForResult(std::move(contrast), [this](const ActivityResult&) { requestUpdate(); });
    }
    return;
  }
  const uint16_t next = static_cast<uint16_t>(workingMask ^ (1u << (index - 1)));
  if (next != 0) workingMask = next;  // the last checked one stays
  requestUpdate();
}

void PaperCycleActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  snprintf(contrastText, sizeof(contrastText), "%d%%", SETTINGS.paperContrast);
  rowItems[0].value = contrastText;
  for (int i = 0; i < paper_texture::STYLE_COUNT; ++i) {
    GUI.setCheckboxRow(rowItems[i + 1], (workingMask & (1u << i)) != 0);
  }
  fui::ListProps props;
  props.items = rowItems;
  props.count = paper_texture::STYLE_COUNT + 1;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
