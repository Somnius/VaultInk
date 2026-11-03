#include "FlashlightActivity.h"

#include <HalFrontlight.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char kStatePath[] = "/.vaultink/flashlight.txt";
constexpr uint8_t kBrightnessChoices[] = {20, 40, 60, 80, 100};
constexpr uint8_t kWarmthChoices[] = {0, 25, 50, 75, 100};
constexpr uint16_t kUnitChoices[] = {100, 200, 300, 500, 800, 1000};
constexpr int kRowHeight = 70;
constexpr int kButtonHeight = 110;

// S.O.S. as steps of (light on?, units): ... --- ... then a gap before it repeats. A dot is 1 unit, a dash 3, the gap
// inside a letter 1, between letters 3, and after the word 7.
struct Step {
  bool on;
  uint8_t units;
};
constexpr Step kSos[] = {{true, 1}, {false, 1}, {true, 1}, {false, 1}, {true, 1}, {false, 3},   // S
                         {true, 3}, {false, 1}, {true, 3}, {false, 1}, {true, 3}, {false, 3},   // O
                         {true, 1}, {false, 1}, {true, 1}, {false, 1}, {true, 1}, {false, 7}};  // S, word gap
constexpr int kSosSteps = sizeof(kSos) / sizeof(kSos[0]);

template <typename T, size_t N>
T nextOf(const T (&choices)[N], const T current) {
  const auto* at = std::find(std::begin(choices), std::end(choices), current);
  const size_t index = at == std::end(choices) ? 0 : static_cast<size_t>(at - std::begin(choices));
  return choices[(index + 1) % N];
}

const char* modeName(const FlashlightActivity::Mode mode) {
  switch (mode) {
    case FlashlightActivity::Mode::Sos:
      return tr(STR_FLASH_SOS);
    case FlashlightActivity::Mode::Blink:
      return tr(STR_FLASH_BLINK);
    default:
      break;
  }
  return tr(STR_FLASH_STEADY);
}
}  // namespace

void FlashlightActivity::onEnter() {
  Activity::onEnter();
  warmthPresent = Frontlight.present() && Frontlight.hasColorTemperature();
  load();
  requestUpdate();
}

void FlashlightActivity::onExit() {
  stop();
  save();
  Activity::onExit();
}

void FlashlightActivity::load() {
  HalFile file;
  if (!Storage.exists(kStatePath) || !Storage.openFileForRead("FLS", kStatePath, file)) return;
  char text[96];
  const int n = file.read(text, sizeof(text) - 1);
  file.close();
  if (n <= 0) return;
  text[n] = '\0';
  // A key counts only at the start of a line.
  const auto valueOf = [&text](const char* key, long& out) {
    const size_t keyLength = std::strlen(key);
    for (const char* at = text; at != nullptr; at = std::strchr(at, '\n')) {
      if (*at == '\n') ++at;
      if (std::strncmp(at, key, keyLength) == 0) {
        out = std::atol(at + keyLength);
        return true;
      }
      if (*at == '\0') break;
    }
    return false;
  };
  long value = 0;
  if (valueOf("mode=", value) && value >= 0 && value < static_cast<long>(Mode::Count)) mode = static_cast<Mode>(value);
  if (valueOf("bright=", value) && value >= 1 && value <= 100) brightness = static_cast<uint8_t>(value);
  if (valueOf("warm=", value) && value >= 0 && value <= 100) warmth = static_cast<uint8_t>(value);
  if (valueOf("unit=", value) && value >= 50 && value <= 2000) unitMs = static_cast<uint16_t>(value);
}

void FlashlightActivity::save() const {
  Storage.ensureDirectoryExists("/.vaultink");
  HalFile file;
  if (!Storage.openFileForWrite("FLS", kStatePath, file)) return;
  char text[96];
  const int n =
      std::snprintf(text, sizeof(text), "mode=%d\nbright=%u\nwarm=%u\nunit=%u\n", static_cast<int>(mode),
                    static_cast<unsigned>(brightness), static_cast<unsigned>(warmth), static_cast<unsigned>(unitMs));
  if (n > 0) file.write(text, std::min<size_t>(static_cast<size_t>(n), sizeof(text) - 1));
}

void FlashlightActivity::start() {
  if (running || !Frontlight.present()) return;
  wasOn = Frontlight.isOn();
  wasBrightness = Frontlight.brightness();
  wasWarmth = Frontlight.warmth();
  Frontlight.setBrightness(brightness);
  if (hasWarmth()) Frontlight.setWarmth(warmth);
  running = true;
  stepIndex = 0;
  lampOn = false;
  stepMs = 0;  // the first tick starts the first step now
  stepStart = millis();
  tick();
  save();
  requestUpdate();
}

void FlashlightActivity::stop() {
  if (!running) return;
  running = false;
  Frontlight.setBrightness(wasBrightness);
  if (hasWarmth()) Frontlight.setWarmth(wasWarmth);
  Frontlight.setOn(wasOn);
  requestUpdate();
}

// Moves to the next step of the pattern when the current one has run its time.
void FlashlightActivity::tick() {
  const uint32_t now = millis();
  if (stepMs != 0 && now - stepStart < stepMs) return;
  bool on = true;
  uint32_t units = 1;
  if (mode == Mode::Steady) {
    on = true;
    units = 60000;  // looked at again every minute; nothing changes
  } else if (mode == Mode::Blink) {
    on = (stepIndex % 2) == 0;
    ++stepIndex;
  } else {
    const Step& step = kSos[stepIndex % kSosSteps];
    on = step.on;
    units = step.units;
    stepIndex = static_cast<uint8_t>((stepIndex + 1) % kSosSteps);
  }
  lampOn = on;
  Frontlight.setOn(on);
  stepStart = now;
  stepMs = units * unitMs;
}

void FlashlightActivity::cycleRow(const int row) {
  const bool wasRunning = running;
  switch (row) {
    case 0:
      mode = static_cast<Mode>((static_cast<int>(mode) + 1) % static_cast<int>(Mode::Count));
      break;
    case 1:
      brightness = nextOf(kBrightnessChoices, brightness);
      break;
    case 2:
      if (!hasWarmth()) return;
      warmth = nextOf(kWarmthChoices, warmth);
      break;
    case 3:
      unitMs = nextOf(kUnitChoices, unitMs);
      break;
    default:
      return;
  }
  if (wasRunning) {  // the new choice takes effect at once
    stop();
    start();
  } else {
    save();
    requestUpdate();
  }
}

bool FlashlightActivity::handleTap(const int x, const int y) {
  (void)x;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + 16;
  const int buttonTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - kButtonHeight - 12;
  if (y >= buttonTop) {
    if (running) {
      stop();
    } else {
      start();
    }
    return true;
  }
  if (y < top) return false;
  const int row = (y - top) / kRowHeight;
  if (row >= 0 && row < ROWS) {
    focusRow = row;
    cycleRow(row);
    return true;
  }
  return false;
}

void FlashlightActivity::loop() {
  using Button = MappedInputManager::Button;
  if (running) tick();
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y) && handleTap(x, y)) return;
  if (mappedInput.wasPressed(Button::Up)) {
    focusRow = (focusRow + ROWS - 1) % ROWS;
    requestUpdate();
  }
  if (mappedInput.wasPressed(Button::Down)) {
    focusRow = (focusRow + 1) % ROWS;
    requestUpdate();
  }
  if (mappedInput.wasReleased(Button::Confirm)) cycleRow(focusRow);
  if (mappedInput.wasPressed(Button::Left)) {
    if (running) {
      stop();
    } else {
      start();
    }
  }
}

void FlashlightActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_FLASH_TITLE), nullptr);

  const int top = metrics.topPadding + metrics.headerHeight + 16;
  if (!Frontlight.present()) {
    renderer.drawCenteredText(UI_10_FONT_ID, top + 40, tr(STR_FLASH_NO_LIGHT));
  } else {
    char values[ROWS][16];
    std::snprintf(values[0], sizeof(values[0]), "%s", modeName(mode));
    std::snprintf(values[1], sizeof(values[1]), "%u%%", static_cast<unsigned>(brightness));
    if (hasWarmth()) {
      std::snprintf(values[2], sizeof(values[2]), "%u%%", static_cast<unsigned>(warmth));
    } else {
      std::snprintf(values[2], sizeof(values[2]), "-");
    }
    std::snprintf(values[3], sizeof(values[3]), "%.1f s", static_cast<double>(unitMs) / 1000.0);
    const char* labels[ROWS] = {tr(STR_FLASH_MODE), tr(STR_FLASH_BRIGHTNESS), tr(STR_FLASH_WARMTH),
                                tr(STR_FLASH_SPEED)};
    for (int i = 0; i < ROWS; ++i) {
      const int y = top + i * kRowHeight;
      if (i == focusRow) renderer.drawRect(6, y, width - 12, kRowHeight - 6, 3, true);
      const int textY = y + (kRowHeight - 6 - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
      renderer.drawText(UI_10_FONT_ID, 18, textY, labels[i], true);
      const int vw = renderer.getTextWidth(UI_10_FONT_ID, values[i], EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, width - 20 - vw, textY, values[i], true, EpdFontFamily::BOLD);
    }
    const int buttonTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - kButtonHeight - 12;
    renderer.fillRect(20, buttonTop, width - 40, kButtonHeight, !running);
    renderer.drawRect(20, buttonTop, width - 40, kButtonHeight, 3, true);
    const char* label = running ? tr(STR_FLASH_STOP) : tr(STR_FLASH_START);
    const int lw = renderer.getTextWidth(UI_12_FONT_ID, label, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, (width - lw) / 2,
                      buttonTop + (kButtonHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2, label, running,
                      EpdFontFamily::BOLD);
  }
  const auto hints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "<", ">");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
