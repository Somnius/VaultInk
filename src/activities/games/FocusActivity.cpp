#include "FocusActivity.h"

#include <Habits.h>
#include <HalHaptics.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char kStatePath[] = "/.vaultink/focus.txt";
constexpr char kLogPath[] = "/.vaultink/focus.tsv";
constexpr uint8_t kFocusChoices[] = {15, 20, 25, 30, 45, 50, 60};
constexpr uint8_t kShortChoices[] = {3, 5, 10};
constexpr uint8_t kLongChoices[] = {10, 15, 20, 30};
constexpr uint8_t kCycleChoices[] = {3, 4, 5, 6};
constexpr int kRowHeight = 64;
constexpr int kButtons = 4;

template <size_t N>
uint8_t nextOf(const uint8_t (&choices)[N], const uint8_t current) {
  const auto* at = std::find(std::begin(choices), std::end(choices), current);
  const size_t index = at == std::end(choices) ? 0 : static_cast<size_t>(at - std::begin(choices));
  return choices[(index + 1) % N];
}

const char* phaseName(const focus::Phase phase) {
  switch (phase) {
    case focus::Phase::Focus:
      return tr(STR_FOCUS_PHASE_FOCUS);
    case focus::Phase::ShortBreak:
      return tr(STR_FOCUS_PHASE_SHORT);
    case focus::Phase::LongBreak:
      return tr(STR_FOCUS_PHASE_LONG);
    case focus::Phase::Idle:
      break;
  }
  return tr(STR_FOCUS_PHASE_IDLE);
}
}  // namespace

int64_t FocusActivity::nowEpoch() {
  const int64_t now = trustedtime::trustedNow();
  return now > 0 ? now : static_cast<int64_t>(time(nullptr));
}

void FocusActivity::onEnter() {
  Activity::onEnter();
  load();
  tick(nowEpoch());
  lastDraw = millis();
  requestUpdate();
}

void FocusActivity::onExit() {
  save();
  Activity::onExit();
}

void FocusActivity::load() {
  HalFile file;
  if (Storage.exists(kStatePath) && Storage.openFileForRead("FOC", kStatePath, file)) {
    char text[focus::Timer::SAVE_CAPACITY + 64];
    const int n = file.read(text, sizeof(text) - 1);
    file.close();
    if (n > 0) {
      text[n] = '\0';
      timer.deserialize(text, static_cast<size_t>(n));
      // The extras after the timer's own lines.
      // A key counts only at the start of a line: "day=" must not match inside "today=".
      const auto valueOf = [&text](const char* key, long long& out) {
        const size_t keyLength = std::strlen(key);
        for (const char* at = text; at != nullptr; at = std::strchr(at, '\n')) {
          if (*at == '\n') ++at;
          if (std::strncmp(at, key, keyLength) == 0) {
            out = std::atoll(at + keyLength);
            return true;
          }
          if (*at == '\0') break;
        }
        return false;
      };
      long long value = 0;
      if (valueOf("day=", value)) lastDay = value;
      if (valueOf("awake=", value)) keepAwake = value != 0;
      if (valueOf("vibrate=", value)) vibrate = value != 0;
      if (valueOf("phasestart=", value)) phaseStart = value;
    }
  }
  const int64_t today = habits::dayFromEpoch(nowEpoch());
  if (today != lastDay) {
    timer.resetToday();  // a new day starts from zero
    lastDay = today;
  }
  lastPhase = timer.phase();
}

void FocusActivity::save() const {
  Storage.ensureDirectoryExists("/.vaultink");
  HalFile file;
  if (!Storage.openFileForWrite("FOC", kStatePath, file)) return;
  char text[focus::Timer::SAVE_CAPACITY + 64];
  size_t n = timer.serialize(text, focus::Timer::SAVE_CAPACITY);
  if (n == 0) return;
  n += static_cast<size_t>(std::snprintf(
      text + n, sizeof(text) - n, "day=%lld\nawake=%d\nvibrate=%d\nphasestart=%lld\n", static_cast<long long>(lastDay),
      keepAwake ? 1 : 0, vibrate ? 1 : 0, static_cast<long long>(phaseStart)));
  file.write(text, std::min(n, sizeof(text) - 1));
}

void FocusActivity::logPhase(const focus::Phase phase, const int64_t endedAt, const bool completed) const {
  if (phase == focus::Phase::Idle) return;
  char line[96];
  const size_t n =
      focus::formatLogLine(line, sizeof(line), endedAt, phase, focus::phaseMinutes(timer.config(), phase), completed);
  if (n == 0) return;
  Storage.ensureDirectoryExists("/.vaultink");
  HalFile file = Storage.open(kLogPath, O_RDWR | O_CREAT | O_AT_END);
  if (file) file.write(line, n);
}

// Advances the timer with the clock; logs and signals a phase that ended.
void FocusActivity::tick(const int64_t now) {
  const focus::Phase before = timer.phase();
  if (timer.update(now) == focus::Event::PhaseEnded) {
    logPhase(before, now, true);
    if (vibrate) HalHaptics::longPress(true, SETTINGS.hapticIntensity);
    phaseStart = now;
    save();
    requestUpdate();
  }
  lastPhase = timer.phase();
}

void FocusActivity::primary(const int64_t now) {
  if (timer.running()) {
    timer.pause(now);
  } else if (timer.paused()) {
    timer.resume(now);
  } else {
    timer.start(timer.nextPhase(), now);
    phaseStart = now;
  }
  save();
  requestUpdate();
}

void FocusActivity::cycleSetting(const int row) {
  focus::Config c = timer.config();
  switch (row) {
    case 0:
      c.focusMin = nextOf(kFocusChoices, c.focusMin);
      break;
    case 1:
      c.shortMin = nextOf(kShortChoices, c.shortMin);
      break;
    case 2:
      c.longMin = nextOf(kLongChoices, c.longMin);
      break;
    case 3:
      c.cyclesBeforeLong = nextOf(kCycleChoices, c.cyclesBeforeLong);
      break;
    case 4:
      c.autoNext = !c.autoNext;
      break;
    case 5:
      keepAwake = !keepAwake;
      break;
    default:
      vibrate = !vibrate;
      break;
  }
  timer.setConfig(c);
  save();
  requestUpdate();
}

bool FocusActivity::handleTap(const int x, const int y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  if (screen == Screen::Settings) {
    const int top = metrics.topPadding + metrics.headerHeight + 16;
    if (y < top) return false;
    const int row = (y - top) / kRowHeight;
    if (row >= 0 && row < 7) {
      focusRow = row;
      cycleSetting(row);
      return true;
    }
    return false;
  }
  const int buttonsTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - 90;
  if (y >= buttonsTop && y < buttonsTop + 80) {
    const int column = x * kButtons / width;
    const int64_t now = nowEpoch();
    if (column == 0) {
      primary(now);
    } else if (column == 1) {
      const focus::Phase before = timer.phase();
      if (timer.skip(now) == focus::Event::PhaseEnded) logPhase(before, now, false);
      phaseStart = now;
      save();
      requestUpdate();
    } else if (column == 2) {
      logPhase(timer.phase(), now, false);
      timer.stop();
      save();
      requestUpdate();
    } else {
      screen = Screen::Settings;
      focusRow = 0;
      requestUpdate();
    }
    return true;
  }
  return false;
}

void FocusActivity::loop() {
  using Button = MappedInputManager::Button;
  const uint32_t ms = millis();
  if (ms - lastTick >= 1000) {
    lastTick = ms;
    const int64_t now = nowEpoch();
    tick(now);
    // The screen shows minutes: once a minute is enough for e-ink.
    if (timer.running() && ms - lastDraw >= 60000) {
      lastDraw = ms;
      requestUpdate();
    }
  }
  if (mappedInput.wasReleased(Button::Back)) {
    if (screen == Screen::Settings) {
      screen = Screen::Timer;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y) && handleTap(x, y)) return;
  if (screen == Screen::Settings) {
    if (mappedInput.wasPressed(Button::Up) || mappedInput.wasPressed(Button::Left)) {
      focusRow = (focusRow + 6) % 7;
      requestUpdate();
    }
    if (mappedInput.wasPressed(Button::Down) || mappedInput.wasPressed(Button::Right)) {
      focusRow = (focusRow + 1) % 7;
      requestUpdate();
    }
    if (mappedInput.wasReleased(Button::Confirm)) cycleSetting(focusRow);
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) primary(nowEpoch());
  if (mappedInput.wasPressed(Button::Left)) {
    const int64_t now = nowEpoch();
    const focus::Phase before = timer.phase();
    if (timer.skip(now) == focus::Event::PhaseEnded) logPhase(before, now, false);
    phaseStart = now;
    save();
    requestUpdate();
  }
  if (mappedInput.wasPressed(Button::Right)) {
    logPhase(timer.phase(), nowEpoch(), false);
    timer.stop();
    save();
    requestUpdate();
  }
  if (mappedInput.wasPressed(Button::Up)) {
    screen = Screen::Settings;
    requestUpdate();
  }
}

void FocusActivity::drawTimer() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_FOCUS_TITLE), nullptr);
  const int64_t now = nowEpoch();
  shownNow = now;
  const focus::Phase shownPhase = timer.phase() == focus::Phase::Idle ? timer.nextPhase() : timer.phase();
  int y = metrics.topPadding + metrics.headerHeight + 40;
  const char* name = phaseName(timer.phase());
  const int nameW = renderer.getTextWidth(UI_12_FONT_ID, name, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, (width - nameW) / 2, y, name, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 24;

  const uint32_t remaining = timer.phase() == focus::Phase::Idle
                                 ? static_cast<uint32_t>(focus::phaseMinutes(timer.config(), shownPhase)) * 60u
                                 : timer.remainingSec(now);
  char big[24];
  std::snprintf(big, sizeof(big), "%u %s", static_cast<unsigned>((remaining + 59) / 60), tr(STR_FOCUS_MIN));
  const int bigW = renderer.getTextWidth(NOTOSANS_18_FONT_ID, big, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_18_FONT_ID, (width - bigW) / 2, y, big, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(NOTOSANS_18_FONT_ID) + 16;
  if (timer.paused()) {
    const char* paused = tr(STR_FOCUS_PAUSED);
    renderer.drawText(UI_10_FONT_ID, (width - renderer.getTextWidth(UI_10_FONT_ID, paused)) / 2, y, paused, true);
  }
  y += 36;

  // A bar for how far the phase is.
  const int barX = 30;
  const int barW = width - 60;
  renderer.drawRect(barX, y, barW, 28, 2, true);
  const uint32_t total = timer.durationSec();
  if (total > 0) {
    const uint32_t elapsed = std::min(total, timer.elapsedSec(now));
    renderer.fillRect(barX + 3, y + 3, static_cast<int>((barW - 6) * static_cast<uint64_t>(elapsed) / total), 22, true);
  }
  y += 56;

  char line[80];
  std::snprintf(line, sizeof(line), "%s: %u   %s: %u/%u", tr(STR_FOCUS_TODAY),
                static_cast<unsigned>(timer.focusDoneToday()), tr(STR_FOCUS_CYCLE),
                static_cast<unsigned>(timer.cycleIndex() + (timer.phase() == focus::Phase::Focus ? 1 : 0)),
                static_cast<unsigned>(timer.config().cyclesBeforeLong));
  renderer.drawText(UI_10_FONT_ID, (width - renderer.getTextWidth(UI_10_FONT_ID, line)) / 2, y, line, true);
  y += 40;
  std::snprintf(line, sizeof(line), "%s: %lu", tr(STR_FOCUS_TOTAL), static_cast<unsigned long>(timer.focusDoneTotal()));
  renderer.drawText(SMALL_FONT_ID, (width - renderer.getTextWidth(SMALL_FONT_ID, line)) / 2, y, line, true);

  const int buttonsTop = renderer.getScreenHeight() - metrics.buttonHintsHeight - 90;
  const char* labels[kButtons] = {
      timer.running() ? tr(STR_FOCUS_PAUSE) : (timer.paused() ? tr(STR_FOCUS_RESUME) : tr(STR_FOCUS_START)),
      tr(STR_FOCUS_SKIP), tr(STR_FOCUS_STOP), tr(STR_FOCUS_SETTINGS)};
  const int w = width / kButtons;
  for (int i = 0; i < kButtons; ++i) {
    const bool primaryButton = i == 0;
    if (primaryButton) renderer.fillRect(i * w + 3, buttonsTop, w - 6, 80, true);
    renderer.drawRect(i * w + 3, buttonsTop, w - 6, 80, 2, true);
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, labels[i], EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, i * w + (w - tw) / 2,
                      buttonsTop + (80 - renderer.getLineHeight(UI_10_FONT_ID)) / 2, labels[i], !primaryButton,
                      EpdFontFamily::BOLD);
  }
}

void FocusActivity::drawSettings() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_FOCUS_SETTINGS), nullptr);
  const focus::Config& c = timer.config();
  char values[7][16];
  std::snprintf(values[0], sizeof(values[0]), "%u min", static_cast<unsigned>(c.focusMin));
  std::snprintf(values[1], sizeof(values[1]), "%u min", static_cast<unsigned>(c.shortMin));
  std::snprintf(values[2], sizeof(values[2]), "%u min", static_cast<unsigned>(c.longMin));
  std::snprintf(values[3], sizeof(values[3]), "%u", static_cast<unsigned>(c.cyclesBeforeLong));
  std::snprintf(values[4], sizeof(values[4]), "%s", c.autoNext ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
  std::snprintf(values[5], sizeof(values[5]), "%s", keepAwake ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
  std::snprintf(values[6], sizeof(values[6]), "%s", vibrate ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
  const char* labels[7] = {tr(STR_FOCUS_LEN_FOCUS), tr(STR_FOCUS_LEN_SHORT), tr(STR_FOCUS_LEN_LONG),
                           tr(STR_FOCUS_CYCLES),    tr(STR_FOCUS_AUTO),      tr(STR_FOCUS_KEEP_AWAKE),
                           tr(STR_FOCUS_VIBRATE)};
  const int top = metrics.topPadding + metrics.headerHeight + 16;
  for (int i = 0; i < 7; ++i) {
    const int y = top + i * kRowHeight;
    if (i == focusRow) renderer.drawRect(6, y, width - 12, kRowHeight - 6, 3, true);
    const int textY = y + (kRowHeight - 6 - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    renderer.drawText(UI_10_FONT_ID, 18, textY, labels[i], true);
    const int vw = renderer.getTextWidth(UI_10_FONT_ID, values[i], EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, width - 20 - vw, textY, values[i], true, EpdFontFamily::BOLD);
  }
}

void FocusActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (screen == Screen::Settings) {
    drawSettings();
  } else {
    drawTimer();
  }
  const auto hints =
      mappedInput.mapLabels(tr(STR_BACK), screen == Screen::Settings ? tr(STR_SELECT) : tr(STR_FOCUS_START), "<", ">");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
