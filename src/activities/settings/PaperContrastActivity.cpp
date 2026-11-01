#include "PaperContrastActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "components/UiSliderDialog.h"
#include "fontIds.h"
#include "util/PaperTexture.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_SLIDER = 1;
constexpr fui::ActionId ACTION_STEP = 2;
constexpr fui::ActionId ACTION_OK = 4;
constexpr fui::ActionId ACTION_CHROME = 5;
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;

// The preview: two rows of five tiles above the dialog.
constexpr int kTileColumns = 5;
constexpr int kTileGap = 6;
constexpr int kTileHeight = 70;
constexpr int kLabelGap = 3;
}  // namespace

PaperContrastActivity::PaperContrastActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("PaperContrast", renderer, mappedInput), UiAppHost(renderer), contrast(SETTINGS.paperContrast) {}

void PaperContrastActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_SLIDER, &PaperContrastActivity::onSliderEvent, this);
  app.on(ACTION_STEP, &PaperContrastActivity::onStepEvent, this);
  app.on(ACTION_OK, &PaperContrastActivity::onOkEvent, this);
  app.setScreen(&PaperContrastActivity::contrastScreen, this);
  requestUpdate();
}

void PaperContrastActivity::setContrast(const int value) {
  const int clamped = std::clamp(value, 0, 100);
  if (clamped == contrast) return;
  contrast = clamped;
  requestUpdate();
}

void PaperContrastActivity::onSliderEvent(const fui::ActionEvent& event, void* user) {
  if (event.dragPermille < 0) return;
  static_cast<PaperContrastActivity*>(user)->setContrast((static_cast<int>(event.dragPermille) * 100 + 500) / 1000);
}

void PaperContrastActivity::onStepEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<PaperContrastActivity*>(user);
  self->setContrast(self->contrast + event.value * kSmallStep);
}

void PaperContrastActivity::onOkEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<PaperContrastActivity*>(user);
  self->app.clearTapFlash();
  self->confirm();
}

void PaperContrastActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void PaperContrastActivity::confirm() {
  if (SETTINGS.paperContrast != contrast) {
    SETTINGS.paperContrast = static_cast<uint8_t>(contrast);
    SETTINGS.saveToFile();
  }
  ActivityResult result;
  setResult(std::move(result));
  finish();
}

void PaperContrastActivity::loop() {
  const auto route = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) {
    if (route.event.dragPermille >= 0) draggingSlider = true;
    return;
  }
  if (routingReady() && draggingSlider) {
    if (!route.snap.touchHeld) draggingSlider = false;
    return;
  }
  if (route.routed && route.snap.touchReleased && route.snap.touchX >= 0) {
    cancel();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Right) {
    setContrast(contrast + kLargeStep);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Left) {
    setContrast(contrast - kLargeStep);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirm();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left},
                                       [this] { setContrast(contrast - kSmallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right},
                                       [this] { setContrast(contrast + kSmallStep); });
  // Edge-button boards: BTN_UP is the left button there (same rule as the percent dialog).
  const int upDelta = gpio.hasEdgeSideButtons() ? -kLargeStep : kLargeStep;
  const int downDelta = gpio.hasEdgeSideButtons() ? kLargeStep : -kLargeStep;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up},
                                       [this, upDelta] { setContrast(contrast + upDelta); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this, downDelta] { setContrast(contrast + downDelta); });
}

void PaperContrastActivity::contrastScreen(UiScreen& screen, void* user) {
  static_cast<PaperContrastActivity*>(user)->buildContrastScreen(screen);
}

void PaperContrastActivity::buildContrastScreen(UiScreen& screen) {
  char readout[16];
  snprintf(readout, sizeof(readout), "%d%%", contrast);
  char hint1[64];
  snprintf(hint1, sizeof(hint1), "%s %d%%", I18N.get(StrId::STR_STEP_HINT_FRONT), kSmallStep);
  char hint2[64];
  snprintf(hint2, sizeof(hint2), "%s %d%%", I18N.get(StrId::STR_STEP_HINT_SIDE), kLargeStep);

  UiSliderDialogSpec spec;
  spec.title = tr(STR_PAPER_CONTRAST);
  spec.readout = readout;
  spec.value = contrast;
  spec.max = 100;
  spec.minLabel = "0%";
  spec.maxLabel = "100%";
  spec.sliderAction = ACTION_SLIDER;
  spec.stepAction = ACTION_STEP;
  spec.okAction = ACTION_OK;
  spec.chromeAction = ACTION_CHROME;
  spec.hintLine1 = hint1;
  spec.hintLine2 = hint2;
  buildSliderDialogScreen(screen, uiTarget, mappedInput, spec);
}

// A tile of every background at the current contrast, in a band at the top; the dialog sits below it.
void PaperContrastActivity::drawPreview() const {
  const int width = renderer.getScreenWidth();
  const int tileWidth = (width - kTileGap * (kTileColumns + 1)) / kTileColumns;
  const int labelHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int rows = (paper_texture::STYLE_COUNT + kTileColumns - 1) / kTileColumns;
  const int rowHeight = kTileHeight + kLabelGap + labelHeight + kTileGap;
  const int top = UITheme::getInstance().getMetrics().topPadding + kTileGap;
  renderer.fillRect(0, top - kTileGap, width, rows * rowHeight + kTileGap, false);
  for (int i = 0; i < paper_texture::STYLE_COUNT; ++i) {
    const int x = kTileGap + (i % kTileColumns) * (tileWidth + kTileGap);
    const int y = top + (i / kTileColumns) * rowHeight;
    paper_texture::drawIn(renderer, static_cast<uint8_t>(i), static_cast<uint8_t>(contrast), x, y, tileWidth,
                          kTileHeight);
    renderer.drawRect(x, y, tileWidth, kTileHeight);
    const char* name = I18N.get(paper_texture::SHORT_NAMES[i]);
    const int nameWidth = renderer.getTextWidth(SMALL_FONT_ID, name);
    renderer.drawText(SMALL_FONT_ID, x + std::max(0, (tileWidth - nameWidth) / 2), y + kTileHeight + kLabelGap, name);
  }
}

void PaperContrastActivity::render(RenderLock&&) {
  // The dialog is a popup over the screen it opened on; the tiles go on top of that screen's top band.
  drawPreview();
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
