#include "GesturesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <utility>
#include <vector>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "util/GestureLabels.h"

namespace fui = freeink::ui;

void GesturesActivity::onEnter() {
  UiListActivity::onEnter();
  changed = false;
  for (int i = 0; i < gestures::kQuickMenuSize; ++i) {
    quickLabels[i] = std::string(tr(STR_QUICK_MENU)) + " " + std::to_string(i + 1);
  }
}

void GesturesActivity::onExit() {
  if (changed) SETTINGS.saveToFile();
  changed = false;
  Activity::onExit();
}

const char* GesturesActivity::headerTitle() const { return tr(STR_GESTURES); }

void GesturesActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  app.clearTapFlash();
  if (index == ROW_RESET) {
    gestures::reset(SETTINGS.gestureAction, SETTINGS.quickMenu);
    changed = true;
  } else if (index >= 0 && index < FIRST_QUICK) {
    // Every action, in the order of the stored value.
    std::vector<std::string> options;
    options.reserve(gestures::kActionCount);
    for (int action = 0; action < gestures::kActionCount; ++action) {
      options.emplace_back(I18N.get(gesturelabels::action(static_cast<uint8_t>(action))));
    }
    optionPopup.show(gesturelabels::slot(index), options, gestures::sanitizeSlot(SETTINGS.gestureAction[index]),
                     [this, index](const int picked) {
                       if (picked < 0 || picked >= gestures::kActionCount) return;
                       SETTINGS.gestureAction[index] = static_cast<uint8_t>(picked);
                       changed = true;
                     });
  } else if (index >= FIRST_QUICK && index < ROW_RESET) {
    const int place = index - FIRST_QUICK;
    std::vector<std::string> options;
    options.reserve(gestures::kQuickChoiceCount);
    int current = 0;
    for (int i = 0; i < gestures::kQuickChoiceCount; ++i) {
      const gestures::Action choice = gestures::kQuickChoices[i];
      options.emplace_back(choice == gestures::Default ? tr(STR_QUICK_MENU_EMPTY)
                                                       : I18N.get(gesturelabels::action(choice)));
      if (choice == SETTINGS.quickMenu[place]) current = i;
    }
    optionPopup.show(StrId::STR_QUICK_MENU, options, current, [this, place](const int picked) {
      if (picked < 0 || picked >= gestures::kQuickChoiceCount) return;
      SETTINGS.quickMenu[place] = gestures::kQuickChoices[picked];
      changed = true;
    });
  }
  requestUpdate();
}

bool GesturesActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void GesturesActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < ROW_COUNT; ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  for (int slot = 0; slot < gestures::kSlotCount; ++slot) {
    rowItems[slot].label = I18N.get(gesturelabels::slot(slot));
    rowItems[slot].value = I18N.get(gesturelabels::action(SETTINGS.gestureAction[slot]));
    // The corner slots only exist with Tap Zones on "Corners only": say so while they are not in use.
    if (slot <= gestures::CornerBR && SETTINGS.tapZones != 1) rowItems[slot].subtitle = tr(STR_GESTURE_NEEDS_CORNERS);
  }
  for (int place = 0; place < gestures::kQuickMenuSize; ++place) {
    const uint8_t stored = gestures::sanitizeQuick(SETTINGS.quickMenu[place]);
    rowItems[FIRST_QUICK + place].label = quickLabels[place].c_str();
    rowItems[FIRST_QUICK + place].value =
        stored == gestures::Default ? tr(STR_QUICK_MENU_EMPTY) : I18N.get(gesturelabels::action(stored));
  }
  rowItems[ROW_RESET].label = tr(STR_GESTURE_RESET);

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(ROW_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  syncListViewport(screen, props);
  screen.list(props);
}

void GesturesActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}
