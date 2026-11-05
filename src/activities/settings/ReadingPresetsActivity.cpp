#include "ReadingPresetsActivity.h"

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

void ReadingPresetsActivity::askNameAndSave(const int slot) {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PRESET_NAME),
                                                           READING_PRESET_STORE.slot(slot).name,
                                                           readingpresets::kNameCapacity - 1, InputType::Text);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this, slot](const ActivityResult& result) {
    if (!result.isCancelled) {
      READING_PRESET_STORE.saveCurrent(slot, std::get<KeyboardResult>(result.data).text);
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
                     [this](int idx) { askNameAndSave(idx); });
    requestUpdate();
    return;
  }
  if (READING_PRESET_STORE.apply(index - 1)) applied_ = true;
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
    rowItems_[i + 1].value = !preset.saved ? tr(STR_PRESET_EMPTY) : i == current ? tr(STR_PRESET_IN_USE) : nullptr;
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
