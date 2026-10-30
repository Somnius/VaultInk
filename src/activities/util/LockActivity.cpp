#include "LockActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "fontIds.h"
#include "util/ScreenLock.h"

namespace {
constexpr int PIN_LENGTH = 4;
constexpr int KEYPAD_TOP = 330;
constexpr int KEY_H = 96;
constexpr int KEY_GAP = 12;
constexpr int SIDE = 36;
constexpr int CORNER = 110;  // how close to a corner a long press must be
constexpr int KEY_DELETE = 11;
constexpr int KEY_ZERO = 10;
constexpr int KEY_CANCEL = 9;
}  // namespace

LockActivity::LockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Mode mode,
                           std::function<void()> onUnlock)
    : Activity("Lock", renderer, mappedInput), mode(mode), onUnlock(std::move(onUnlock)) {}

void LockActivity::onEnter() {
  Activity::onEnter();
  cornerMode = mode == Mode::Unlock && SETTINGS.lockMode == 2;
  if (mode == Mode::Unlock) screenlock::setPending(true);
  requestUpdate(true);
}

bool LockActivity::keyRect(const int key, int& x, int& y, int& w, int& h) const {
  if (key < 0 || key >= KEYS) return false;
  // Unlocking has no cancel key: the only way past is the PIN.
  if (key == KEY_CANCEL && mode == Mode::Unlock) return false;
  const int width = renderer.getScreenWidth() - 2 * SIDE;
  w = (width - 2 * KEY_GAP) / 3;
  h = KEY_H;
  x = SIDE + (key % 3) * (w + KEY_GAP);
  y = KEYPAD_TOP + (key / 3) * (h + KEY_GAP);
  return true;
}

void LockActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();

  // Whose reader this is, for whoever finds it (kos25k #22). Top centre, clear of the corner marks.
  if (mode == Mode::Unlock) {
    int y = 30;
    if (SETTINGS.ownerName[0]) {
      renderer.drawCenteredText(
          UI_12_FONT_ID, y,
          renderer.truncatedText(UI_12_FONT_ID, SETTINGS.ownerName, width - 2 * 70, EpdFontFamily::BOLD).c_str(), true,
          EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_12_FONT_ID) + 4;
    }
    if (SETTINGS.ownerPhone[0]) {
      renderer.drawCenteredText(UI_10_FONT_ID, y,
                                renderer.truncatedText(UI_10_FONT_ID, SETTINGS.ownerPhone, width - 2 * 70).c_str());
    }
  }

  if (cornerMode) {
    // A mark on the corner (or corners) that open it.
    const int m = 18;
    const int len = 40;
    const uint8_t only = SETTINGS.lockCorner;
    const auto mark = [&](const bool right, const bool bottom) {
      const int x = right ? width - m - len : m;
      const int y = bottom ? height - m - 4 : m;
      renderer.fillRect(x, y, len, 4);
      renderer.fillRect(right ? width - m - 4 : m, bottom ? height - m - len : m, 4, len);
    };
    if (only == 0 || only == 1) mark(false, false);
    if (only == 0 || only == 2) mark(true, false);
    if (only == 0 || only == 3) mark(false, true);
    if (only == 0 || only == 4) mark(true, true);
    renderer.drawCenteredText(UI_12_FONT_ID, height / 2 - 30, tr(STR_LOCKED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, height / 2 + 10,
                              SETTINGS.lockCorner == 0 ? tr(STR_LOCK_HOLD_CORNER) : tr(STR_LOCK_HOLD_THIS_CORNER));
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  const char* title = mode == Mode::Unlock ? tr(STR_LOCK_ENTER_PIN)
                      : firstPin.empty()   ? tr(STR_LOCK_NEW_PIN)
                                           : tr(STR_LOCK_REPEAT_PIN);
  renderer.drawCenteredText(UI_12_FONT_ID, 110, title, true, EpdFontFamily::BOLD);

  // Four dots, filled as digits come in.
  const int dot = 26;
  const int gap = 28;
  const int dotsX = (width - (PIN_LENGTH * dot + (PIN_LENGTH - 1) * gap)) / 2;
  for (int i = 0; i < PIN_LENGTH; ++i) {
    const int x = dotsX + i * (dot + gap);
    if (i < static_cast<int>(digits.size())) {
      renderer.fillRoundedRect(x, 190, dot, dot, dot / 2, Color::Black);
    } else {
      renderer.drawRoundedRect(x, 190, dot, dot, 2, dot / 2, true);
    }
  }
  if (message) renderer.drawCenteredText(UI_10_FONT_ID, 250, message);

  for (int key = 0; key < KEYS; ++key) {
    int x, y, w, h;
    if (!keyRect(key, x, y, w, h)) continue;
    renderer.drawRoundedRect(x, y, w, h, 2, 14, true);
    char label[4] = {};
    const char* text = label;
    if (key < 9) {
      label[0] = static_cast<char>('1' + key);
    } else if (key == KEY_ZERO) {
      label[0] = '0';
    } else if (key == KEY_DELETE) {
      text = tr(STR_DELETE);
    } else {
      text = tr(STR_CANCEL);
    }
    const int font = key < 9 || key == KEY_ZERO ? UI_12_FONT_ID : UI_10_FONT_ID;
    const int tw = renderer.getTextWidth(font, text, EpdFontFamily::BOLD);
    renderer.drawText(font, x + (w - tw) / 2, y + (h - renderer.getLineHeight(font)) / 2, text, true,
                      EpdFontFamily::BOLD);
  }
  if (mode == Mode::SetPin) {
    renderer.drawCenteredText(SMALL_FONT_ID, height - 30, tr(STR_LOCK_FORGOT_HINT));
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void LockActivity::pressKey(const int key) {
  if (key == KEY_CANCEL) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }
  if (key == KEY_DELETE) {
    if (!digits.empty()) digits.pop_back();
    requestUpdate();
    return;
  }
  if (digits.size() >= PIN_LENGTH) return;
  digits += key == KEY_ZERO ? '0' : static_cast<char>('1' + key);
  message = nullptr;
  if (digits.size() == PIN_LENGTH) {
    digitsComplete();
  }
  requestUpdate();
}

void LockActivity::digitsComplete() {
  if (mode == Mode::Unlock) {
    if (screenlock::checkPin(digits)) {
      unlocked();
    } else {
      LOG_INF("LCK", "Wrong PIN");
      message = tr(STR_LOCK_WRONG_PIN);
      digits.clear();
    }
    return;
  }
  if (firstPin.empty()) {
    firstPin = digits;
    digits.clear();
    return;
  }
  if (digits != firstPin) {
    message = tr(STR_LOCK_PINS_DIFFER);
    firstPin.clear();
    digits.clear();
    return;
  }
  ActivityResult res;
  res.isCancelled = !screenlock::savePin(digits);
  setResult(std::move(res));
  finish();
}

void LockActivity::unlocked() {
  if (done) return;  // a finger still on the corner keeps calling until the screen is gone
  done = true;
  screenlock::setPending(false);
  LOG_INF("LCK", "Unlocked");
  auto next = std::move(onUnlock);
  if (next) {
    next();  // the boot routing replaces this screen
  } else {
    finish();
  }
}

bool LockActivity::inUnlockCorner(const int x, const int y) const {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const bool left = x < CORNER;
  const bool right = x > width - CORNER;
  const bool top = y < CORNER;
  const bool bottom = y > height - CORNER;
  switch (SETTINGS.lockCorner) {
    case 1:
      return left && top;
    case 2:
      return right && top;
    case 3:
      return left && bottom;
    case 4:
      return right && bottom;
    default:
      return (left || right) && (top || bottom);
  }
}

void LockActivity::loop() {
  int x = 0;
  int y = 0;
  if (cornerMode) {
    // A finger on the chosen corner, held for Corner Hold seconds (0: the touch itself).
    if (mappedInput.isScreenTouchHeld(x, y) && inUnlockCorner(x, y)) {
      if (!holding) {
        holding = true;
        holdSince = millis();
      }
      if (millis() - holdSince >= SETTINGS.lockHoldSeconds * 1000UL) unlocked();
    } else {
      holding = false;
    }
    return;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    for (int key = 0; key < KEYS; ++key) {
      int kx, ky, kw, kh;
      if (keyRect(key, kx, ky, kw, kh) && x >= kx && x < kx + kw && y >= ky && y < ky + kh) {
        pressKey(key);
        return;
      }
    }
  }
}
