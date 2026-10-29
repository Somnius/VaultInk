#include "BookInfoActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookInfo.h"

void BookInfoActivity::onEnter() {
  Activity::onEnter();
  text = bookinfo::text(path);
  requestUpdate();
}

void BookInfoActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void BookInfoActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_BOOK_INFO), nullptr);
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 3;
  for (const auto& line : renderer.wrappedText(UI_12_FONT_ID, text.c_str(), width - 48, 12)) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, line.c_str());
    y += lineHeight;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
