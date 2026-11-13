#include "ReadingPresetsActivity.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void ReadingPresetsActivity::onEnter() {
  UiListActivity::onEnter();
  READING_PRESET_STORE.ensureLoaded();
  rowItems_[0].label = tr(STR_SAVE_PRESET);
  rowItems_[0].actionValue = 0;
  for (int i = 0; i < ReadingPresetsStore::SLOTS; i++) {
    rowItems_[i + 1].actionValue = static_cast<int16_t>(i + 1);
  }
}

const char* ReadingPresetsActivity::headerTitle() const { return tr(STR_READING_PRESETS); }

void ReadingPresetsActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = !applied_;
  setResult(std::move(result));
  finish();
}

void ReadingPresetsActivity::askGesturesAndSave(const int slot) {
  // Gestures are a touch feature; a board without touch saves the layout only, as before.
  if (!BoardConfig::hasTouch()) {
    askNameAndSave(slot, false);
    return;
  }
  // The popup has already handed its callback over, so the next one can be shown from inside it.
  static constexpr StrId kNoYes[] = {StrId::STR_NO, StrId::STR_YES};
  optionPopup.show(StrId::STR_PRESET_INCLUDE_GESTURES, kNoYes, 2, 0,
                   [this, slot](int idx) { askNameAndSave(slot, idx == 1); });
  requestUpdate();
}

void ReadingPresetsActivity::askNameAndSave(const int slot, const bool includeGestures) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PRESET_NAME),
                                                           READING_PRESET_STORE.slot(slot).name,
                                                           readingpresets::kNameCapacity - 1, InputType::Text);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this, slot, includeGestures](const ActivityResult& result) {
    if (!result.isCancelled) {
      READING_PRESET_STORE.saveCurrent(slot, std::get<KeyboardResult>(result.data).text, includeGestures);
      nav.selected = slot + 1;
    }
    requestUpdate();
  });
}

void ReadingPresetsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  app.clearTapFlash();
  if (index == 0) {
    // Which slot: its name, with "Empty" after the unused ones.
    std::vector<std::string> options;
    options.reserve(ReadingPresetsStore::SLOTS);
    for (int i = 0; i < ReadingPresetsStore::SLOTS; i++) {
      const auto& preset = READING_PRESET_STORE.slot(i);
      options.push_back(preset.saved ? std::string(preset.name)
                                     : std::string(preset.name) + " (" + tr(STR_PRESET_EMPTY) + ")");
    }
    const int current = READING_PRESET_STORE.currentSlot();
    optionPopup.show(StrId::STR_SAVE_PRESET, options, current < 0 ? 0 : current,
                     [this](int idx) { askGesturesAndSave(idx); });
    requestUpdate();
    return;
  }
  bool gesturesSet = false;
  if (READING_PRESET_STORE.apply(index - 1, &gesturesSet)) applied_ = true;
  if (gesturesSet) {
    {
      RenderLock lock(*this);
      GUI.drawPopup(renderer, tr(STR_PRESET_GESTURES_SET));
    }
    delay(1200);
  }
  requestUpdate();
}

bool ReadingPresetsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void ReadingPresetsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const int current = READING_PRESET_STORE.currentSlot();
  for (int i = 0; i < ReadingPresetsStore::SLOTS; i++) {
    const auto& preset = READING_PRESET_STORE.slot(i);
    rowItems_[i + 1].label = preset.name;
    rowItems_[i + 1].value = !preset.saved ? tr(STR_PRESET_EMPTY)
                             : preset.hasGestures
                                 ? (i == current ? tr(STR_PRESET_IN_USE_GESTURES) : tr(STR_PRESET_WITH_GESTURES))
                             : i == current ? tr(STR_PRESET_IN_USE)
                                            : nullptr;
  }
  fui::ListProps props;
  props.items = rowItems_;
  props.count = ITEM_COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void ReadingPresetsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  renderer.clearScreen();
  drawChrome();
  renderUi();
  drawFooter();
  renderer.displayBuffer();
}
