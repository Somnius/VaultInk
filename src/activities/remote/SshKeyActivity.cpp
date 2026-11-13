#include "SshKeyActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/SshDevice.h"
#include "util/QrUtils.h"

void SshKeyActivity::onEnter() {
  Activity::onEnter();
  load();
  requestUpdate();
}

void SshKeyActivity::load() {
  RenderLock lock(*this);
  failed_ = !sshdevice::publicKeyText(line_, fingerprint_);
  if (failed_) {
    line_.clear();
    fingerprint_.clear();
  }
}

void SshKeyActivity::askNewKey() {
  auto dialog =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_SSH_NEW_KEY), tr(STR_SSH_NEW_KEY_BODY));
  if (!dialog) return;
  startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    sshdevice::createKey();
    load();
  });
}

void SshKeyActivity::loop() {
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    askNewKey();
    return;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    if (y >= newKeyTop_ && y < newKeyBottom_) {
      askNewKey();
    } else {
      finish();
    }
  }
}

void SshKeyActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SSH_PUBLIC_KEY),
                 nullptr);

  const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  const int uiLine = renderer.getLineHeight(UI_10_FONT_ID);
  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;

  if (failed_) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2, tr(STR_SSH_KEY_FAILED));
  } else {
    // The key has no spaces to wrap at: cut it into pieces that fit the width.
    const int textWidth = pageWidth - 40;
    std::vector<std::string> pieces;
    std::string piece;
    for (const char c : line_) {
      piece += c;
      if (renderer.getTextWidth(SMALL_FONT_ID, piece.c_str()) > textWidth) {
        piece.pop_back();
        pieces.push_back(piece);
        piece.assign(1, c);
      }
    }
    if (!piece.empty()) pieces.push_back(piece);

    const int textHeight = static_cast<int>(pieces.size() + 1) * lineHeight + uiLine * 3 + 30;
    const int qrSide = std::min(pageWidth - 40, pageHeight - y - textHeight - metrics.buttonHintsHeight - uiLine * 2);
    if (qrSide > 80) {
      QrUtils::drawQrCode(renderer, Rect{(pageWidth - qrSide) / 2, y, qrSide, qrSide}, line_);
      y += qrSide + 10;
    }
    for (const std::string& text : renderer.wrappedText(UI_10_FONT_ID, tr(STR_SSH_KEY_CAPTION), textWidth, 3)) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, text.c_str());
      y += uiLine;
    }
    y += 6;
    for (const std::string& text : pieces) {
      renderer.drawText(SMALL_FONT_ID, 20, y, text.c_str());
      y += lineHeight;
    }
    y += 6;
    renderer.drawText(SMALL_FONT_ID, 20, y, fingerprint_.c_str());
    y += lineHeight + 10;
  }

  // "Make a new key": a band to tap, and Confirm on boards with buttons.
  newKeyTop_ = std::min(y, pageHeight - metrics.buttonHintsHeight - uiLine * 2);
  newKeyBottom_ = newKeyTop_ + uiLine * 2;
  renderer.drawRect(20, newKeyTop_, pageWidth - 40, uiLine * 2 - 4);
  renderer.drawCenteredText(UI_10_FONT_ID, newKeyTop_ + uiLine / 2 - 2, tr(STR_SSH_NEW_KEY));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SSH_NEW_KEY), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
