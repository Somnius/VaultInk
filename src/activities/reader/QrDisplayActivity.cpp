#include "QrDisplayActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QrUtils.h"

void QrDisplayActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void QrDisplayActivity::onExit() { Activity::onExit(); }

void QrDisplayActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
    return;
  }
}

void QrDisplayActivity::render(RenderLock&&) {
  renderer.clearScreen();
  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 title ? title : tr(STR_DISPLAY_QR), nullptr);

  // Room for the caption under the code, when there is one.
  const int captionHeight = caption ? renderer.getLineHeight(UI_12_FONT_ID) * 6 + 20 : 0;
  const int availableWidth = pageWidth - 40;
  const int availableHeight =
      pageHeight - metrics.topPadding - metrics.headerHeight - metrics.verticalSpacing * 2 - 40 - captionHeight;
  const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  const Rect qrBounds(20, startY, availableWidth, availableHeight);
  QrUtils::drawQrCode(renderer, qrBounds, textPayload);

  if (caption) {
    const int captionY = startY + availableHeight + 10;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    int y = captionY;
    for (const auto& line : renderer.wrappedText(UI_12_FONT_ID, caption, availableWidth, 6)) {
      renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str());
      y += lineHeight;
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
