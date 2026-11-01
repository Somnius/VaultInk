#include "HabitsActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char kPath[] = "/.vaultink/habits.txt";
constexpr int kRowHeight = 96;
constexpr int kBox = 38;
constexpr int kBoxGap = 6;
}  // namespace

int64_t HabitsActivity::nowEpoch() {
  const int64_t now = trustedtime::trustedNow();
  return now > 0 ? now : static_cast<int64_t>(time(nullptr));
}

void HabitsActivity::onEnter() {
  Activity::onEnter();
  list = makeUniqueNoThrow<habits::HabitList>();
  const int64_t now = nowEpoch();
  clockSet = now > 1577836800;  // 2020-01-01: before that the clock was never set
  today = habits::dayFromEpoch(now);
  load();
  requestUpdate();
}

void HabitsActivity::onExit() {
  save();
  Activity::onExit();
}

void HabitsActivity::load() {
  if (!list) return;
  list->clear();
  auto text = makeUniqueNoThrow<char[]>(habits::kFileCapacity + 1);
  HalFile file;
  if (!text || !Storage.exists(kPath) || !Storage.openFileForRead("HAB", kPath, file)) return;
  const int n = file.read(text.get(), habits::kFileCapacity);
  file.close();
  if (n > 0) habits::parse(text.get(), static_cast<size_t>(n), *list);
}

void HabitsActivity::save() const {
  if (!list) return;
  auto text = makeUniqueNoThrow<char[]>(habits::kFileCapacity + 1);
  if (!text) return;
  const size_t n = habits::serialize(*list, text.get(), habits::kFileCapacity + 1);
  Storage.ensureDirectoryExists("/.vaultink");
  HalFile file;
  if (Storage.openFileForWrite("HAB", kPath, file)) file.write(text.get(), std::min<size_t>(n, habits::kFileCapacity));
}

int HabitsActivity::rowCountTotal() const { return (list ? list->count : 0) + 2; }  // habits, Add, Remove/Done

void HabitsActivity::addHabit() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_HABITS_NEW), "", 30);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (!result.isCancelled && list) {
      const std::string& name = std::get<KeyboardResult>(result.data).text;
      if (list->add(name.c_str())) {
        save();
        focus = list->count - 1;
      }
    }
    requestUpdate();
  });
}

bool HabitsActivity::handleTap(const int x, const int y) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + 24;
  if (y < listTop || !list) return false;
  const int row = top + (y - listTop) / kRowHeight;
  if (row < 0 || row >= rowCountTotal()) return false;
  focus = row;
  const int habitCount = list->count;
  if (row < habitCount) {
    if (mode == Mode::Remove) {
      list->remove(static_cast<size_t>(row));
      save();
      mode = Mode::Track;
      focus = std::max(0, std::min(focus, rowCountTotal() - 1));
    } else if (clockSet) {
      // The seven boxes sit at the right edge, today last.
      const int width = renderer.getScreenWidth();
      const int boxesX = width - 16 - 7 * (kBox + kBoxGap);
      const int column = x >= boxesX ? std::min(6, (x - boxesX) / (kBox + kBoxGap)) : 6;
      list->toggle(static_cast<size_t>(row), static_cast<habits::Day>(today - (6 - column)));
      dayColumn = column;
      save();
    }
  } else if (row == habitCount) {
    addHabit();
  } else {
    mode = mode == Mode::Remove ? Mode::Track : Mode::Remove;
  }
  requestUpdate();
  return true;
}

void HabitsActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    if (mode == Mode::Remove) {
      mode = Mode::Track;
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y) && handleTap(x, y)) return;
  const int rows = rowCountTotal();
  if (mappedInput.wasPressed(Button::Up)) {
    focus = (focus + rows - 1) % rows;
    requestUpdate();
  }
  if (mappedInput.wasPressed(Button::Down)) {
    focus = (focus + 1) % rows;
    requestUpdate();
  }
  if (mappedInput.wasPressed(Button::Left) && dayColumn > 0) {
    --dayColumn;
    requestUpdate();
  }
  if (mappedInput.wasPressed(Button::Right) && dayColumn < 6) {
    ++dayColumn;
    requestUpdate();
  }
  if (mappedInput.wasReleased(Button::Confirm) && list) {
    const int habitCount = list->count;
    if (focus < habitCount) {
      if (mode == Mode::Remove) {
        list->remove(static_cast<size_t>(focus));
        mode = Mode::Track;
      } else if (clockSet) {
        list->toggle(static_cast<size_t>(focus), static_cast<habits::Day>(today - (6 - dayColumn)));
      }
      save();
    } else if (focus == habitCount) {
      addHabit();
    } else {
      mode = mode == Mode::Remove ? Mode::Track : Mode::Remove;
    }
    requestUpdate();
  }
  const int visible = std::max(1, (renderer.getScreenHeight() - 260) / kRowHeight);
  if (focus < top) top = focus;
  if (focus >= top + visible) top = focus - visible + 1;
}

void HabitsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_HABITS_TITLE), nullptr);
  int y = metrics.topPadding + metrics.headerHeight + 24;
  if (!clockSet) {
    renderer.drawText(UI_10_FONT_ID, 16, y, tr(STR_HABITS_SET_CLOCK), true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }
  if (!list) return;
  const int visible = std::max(1, (renderer.getScreenHeight() - 260) / kRowHeight);
  const int total = rowCountTotal();
  for (int row = top; row < total && row < top + visible; ++row) {
    const int rowY = y + (row - top) * kRowHeight;
    if (row == focus) renderer.drawRect(6, rowY, width - 12, kRowHeight - 6, 3, true);
    if (row < list->count) {
      const habits::Habit& habit = list->habits[row];
      // The name stops before the seven boxes.
      const int nameWidth = width - 16 - 7 * (kBox + kBoxGap) - 30;
      const std::string name = renderer.truncatedText(UI_10_FONT_ID, habit.name, nameWidth, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, 18, rowY + 8, name.c_str(), true, EpdFontFamily::BOLD);
      char info[48];
      std::snprintf(info, sizeof(info), "%s %u   %s %u", tr(STR_HABITS_STREAK),
                    static_cast<unsigned>(list->streak(row, today)), tr(STR_HABITS_TOTAL),
                    static_cast<unsigned>(list->totalDone(row)));
      renderer.drawText(SMALL_FONT_ID, 18, rowY + 8 + renderer.getLineHeight(UI_10_FONT_ID) + 6, info, true);
      if (mode == Mode::Remove) {
        const char* remove = tr(STR_HABITS_TAP_REMOVE);
        renderer.drawText(SMALL_FONT_ID, width - 16 - renderer.getTextWidth(SMALL_FONT_ID, remove), rowY + 36, remove,
                          true);
      } else {
        const int boxesX = width - 16 - 7 * (kBox + kBoxGap);
        for (int column = 0; column < 7; ++column) {
          const habits::Day day = static_cast<habits::Day>(today - (6 - column));
          const int bx = boxesX + column * (kBox + kBoxGap);
          const int by = rowY + 8;
          renderer.drawRect(bx, by, kBox, kBox, column == 6 ? 3 : 2, true);
          if (list->isDone(row, day)) renderer.fillRect(bx + 6, by + 6, kBox - 12, kBox - 12, true);
          if (row == focus && column == dayColumn) renderer.drawRect(bx - 3, by - 3, kBox + 6, kBox + 6, 1, true);
          // The weekday initial under the box (Monday first in the data).
          static const char* const kDays[] = {"M", "T", "W", "T", "F", "S", "S"};
          const char* letter = kDays[habits::dayOfWeek(day)];
          renderer.drawText(SMALL_FONT_ID, bx + (kBox - renderer.getTextWidth(SMALL_FONT_ID, letter)) / 2,
                            by + kBox + 2, letter, true);
        }
      }
    } else if (row == list->count) {
      renderer.drawText(UI_10_FONT_ID, 18, rowY + 24, tr(STR_HABITS_ADD), true, EpdFontFamily::BOLD);
    } else {
      renderer.drawText(UI_10_FONT_ID, 18, rowY + 24,
                        mode == Mode::Remove ? tr(STR_HABITS_DONE) : tr(STR_HABITS_REMOVE), true, EpdFontFamily::BOLD);
    }
  }
  const auto hints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_HABITS_TICK), "<", ">");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
