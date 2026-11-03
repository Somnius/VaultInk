#include "ExperimentalSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId kNames[ExperimentalSettingsActivity::ITEM_COUNT] = {
    StrId::STR_TEXT_LIGATURES, StrId::STR_TEXT_KERNING, StrId::STR_TEXT_WIDOW_ORPHAN, StrId::STR_TEXT_KEEP_HEADINGS,
    StrId::STR_TEXT_HANGING};

uint8_t* fieldFor(const int index) {
  switch (index) {
    case 0:
      return &SETTINGS.textLigatures;
    case 1:
      return &SETTINGS.textKerning;
    case 2:
      return &SETTINGS.textWidowOrphan;
    case 3:
      return &SETTINGS.textKeepHeadings;
    case 4:
      return &SETTINGS.textHanging;
    default:
      return nullptr;
  }
}
}  // namespace

void ExperimentalSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  for (int i = 0; i < ITEM_COUNT; i++) {
    rowItems_[i].label = I18N.get(kNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

void ExperimentalSettingsActivity::activateIndex(const int index) {
  uint8_t* field = fieldFor(index);
  if (field == nullptr) return;
  nav.selected = index;
  app.clearTapFlash();
  *field = *field ? 0 : 1;
  SETTINGS.saveToFile();
  requestUpdate();
}

void ExperimentalSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < ITEM_COUNT; i++) GUI.setCheckboxRow(rowItems_[i], *fieldFor(i));
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

void ExperimentalSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_EXPERIMENTAL));
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
