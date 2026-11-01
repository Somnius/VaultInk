#include "SudokuActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

#if defined(ESP_PLATFORM)
#include <esp_random.h>
#endif

namespace {
constexpr char SAVE_PATH[] = "/.vaultink/sudoku.sav";

uint32_t randomSeed() {
#if defined(ESP_PLATFORM)
  return esp_random();
#else
  return static_cast<uint32_t>(millis()) * 2654435761u ^ static_cast<uint32_t>(micros());
#endif
}

const char* levelName(const sudoku::Difficulty d) {
  switch (d) {
    case sudoku::Difficulty::Easy:
      return tr(STR_SUDOKU_EASY);
    case sudoku::Difficulty::Hard:
      return tr(STR_SUDOKU_HARD);
    default:
      return tr(STR_SUDOKU_MEDIUM);
  }
}
}  // namespace

void SudokuActivity::onEnter() {
  Activity::onEnter();
  load();
  lastTickMs = millis();
  requestUpdate();
}

void SudokuActivity::onExit() {
  tick();
  save();
  Activity::onExit();
}

void SudokuActivity::load() {
  HalFile file;
  sudoku::SaveBuffer buf{};
  bool ok = false;
  if (Storage.exists(SAVE_PATH) && Storage.openFileForRead("SDK", SAVE_PATH, file)) {
    ok = file.read(buf.data(), buf.size()) == static_cast<int>(buf.size()) &&
         sudoku::deserialize(buf.data(), buf.size(), game);
    file.close();
  }
  if (ok) {
    level = game.difficulty;
    solved = sudoku::isSolved(game.board);
  } else {
    startNew();
  }
}

void SudokuActivity::save() const {
  sudoku::SaveBuffer buf{};
  sudoku::serialize(game, buf);
  Storage.ensureDirectoryExists("/.vaultink");
  HalFile file;
  if (Storage.openFileForWrite("SDK", SAVE_PATH, file)) {
    file.write(buf.data(), buf.size());
    file.close();
  } else {
    LOG_ERR("SDK", "Could not save the game");
  }
}

void SudokuActivity::startNew() {
  sudoku::generate(game, randomSeed(), level);
  lastTickMs = millis();  // the new timer starts now, not with the idle time before the press
  selected = 40;
  notesMode = false;
  solved = false;
  save();
}

void SudokuActivity::tick() {
  const unsigned long now = millis();
  if (!solved) game.elapsedSeconds += static_cast<uint32_t>((now - lastTickMs) / 1000);
  lastTickMs += ((now - lastTickMs) / 1000) * 1000;  // keep the sub-second part
}

SudokuActivity::Layout SudokuActivity::layout() const {
  Layout l{};
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  l.cell = (width - 16) / 9;
  l.gridX = (width - l.cell * 9) / 2;
  l.infoY = metrics.topPadding + metrics.headerHeight + 4;
  l.gridY = l.infoY + renderer.getLineHeight(UI_10_FONT_ID) + 8;
  l.padY = l.gridY + l.cell * 9 + 12;
  l.padH = 52;
  l.toolY = l.padY + l.padH + 8;
  l.toolH = 46;
  return l;
}

int SudokuActivity::cellAt(const int x, const int y) const {
  const Layout l = layout();
  if (x < l.gridX || y < l.gridY) return -1;
  const int col = (x - l.gridX) / l.cell;
  const int row = (y - l.gridY) / l.cell;
  return col < 9 && row < 9 ? row * 9 + col : -1;
}

void SudokuActivity::pressDigit(const uint8_t digit, const bool forceValue) {
  if (solved) return;
  if (!(notesMode && !forceValue)) {
    const auto placed = std::count_if(game.board.cells.begin(), game.board.cells.end(),
                                      [digit](const sudoku::Cell& c) { return c.value == digit; });
    if (placed >= 9 && game.board.cells[selected].value != digit) return;  // all nine are on the board
  }
  const bool changed =
      (notesMode && !forceValue) ? sudoku::toggleNote(game, selected, digit) : sudoku::setValue(game, selected, digit);
  if (!changed) return;
  if (sudoku::isSolved(game.board)) {
    tick();
    solved = true;
    save();
  }
}

void SudokuActivity::pressTool(const Tool tool) {
  switch (tool) {
    case TOOL_NOTES:
      notesMode = !notesMode;
      break;
    case TOOL_ERASE:
      if (!solved) sudoku::clearCell(game, selected);
      break;
    case TOOL_UNDO:
      if (!solved) sudoku::undo(game);
      break;
    case TOOL_HINT: {
      if (solved) break;
      const sudoku::Hint hint = sudoku::hint(game);
      if (hint.found) {
        selected = hint.cell;
        sudoku::setValue(game, hint.cell, hint.value);
        if (sudoku::isSolved(game.board)) {
          tick();
          solved = true;
          save();
        }
      }
      break;
    }
    case TOOL_NEW:
      startNew();
      break;
    case TOOL_LEVEL:
      level = static_cast<sudoku::Difficulty>((static_cast<int>(level) + 1) % 3);
      startNew();
      break;
    default:
      break;
  }
}

bool SudokuActivity::handleTap(const int x, const int y) {
  const Layout l = layout();
  if (solved) {
    startNew();  // any tap after a win begins the next puzzle
    return true;
  }
  const int cell = cellAt(x, y);
  if (cell >= 0) {
    selected = cell;
    return true;
  }
  const int width = renderer.getScreenWidth();
  if (y >= l.padY && y < l.padY + l.padH) {
    const int digit = x * 9 / width + 1;
    if (digit >= 1 && digit <= 9) pressDigit(static_cast<uint8_t>(digit));
    return true;
  }
  if (y >= l.toolY && y < l.toolY + l.toolH) {
    const int tool = x * TOOL_COUNT / width;
    if (tool >= 0 && tool < TOOL_COUNT) pressTool(static_cast<Tool>(tool));
    return true;
  }
  return false;
}

void SudokuActivity::handleButtons() {
  using Button = MappedInputManager::Button;
  const bool back = mappedInput.wasPressed(Button::Left) || mappedInput.wasPressed(Button::Up) ||
                    mappedInput.wasPressed(Button::PageBack);
  const bool forward = mappedInput.wasPressed(Button::Right) || mappedInput.wasPressed(Button::Down) ||
                       mappedInput.wasPressed(Button::PageForward);
  if (back) selected = (selected + sudoku::CELLS - 1) % sudoku::CELLS;
  if (forward) selected = (selected + 1) % sudoku::CELLS;
  if (mappedInput.wasLongPressed(Button::Confirm, 700)) {
    notesMode = !notesMode;
  } else if (mappedInput.wasReleased(Button::Confirm)) {
    if (solved) {
      startNew();
    } else {
      const uint8_t current = game.board.cells[selected].value;
      if (!game.board.cells[selected].given) {
        if (current >= 9) {
          sudoku::clearCell(game, selected);
        } else {
          pressDigit(static_cast<uint8_t>(current + 1), true);
        }
      }
    }
  }
}

void SudokuActivity::loop() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }
  bool dirty = false;
  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenTapped(x, y)) dirty = handleTap(x, y);
  const int before = selected;
  const auto beforeNotes = notesMode;
  handleButtons();
  dirty = dirty || before != selected || beforeNotes != notesMode || mappedInput.wasReleased(Button::Confirm);
  if (dirty) {
    tick();
    if (!solved) save();
    requestUpdate();
  }
}

void SudokuActivity::drawCell(const Layout& l, const int index, const bool* wrong) const {
  const int col = index % 9;
  const int row = index / 9;
  const int x = l.gridX + col * l.cell;
  const int y = l.gridY + row * l.cell;
  const sudoku::Cell& cell = game.board.cells[index];
  const int sel = selected;
  const bool inUnit = (sel / 9 == row) || (sel % 9 == col) || ((sel / 9) / 3 == row / 3 && (sel % 9) / 3 == col / 3);
  if (index == sel) {
    renderer.fillRect(x + 1, y + 1, l.cell - 1, l.cell - 1, true);
  } else if (inUnit) {
    renderer.fillRectDither(x + 1, y + 1, l.cell - 1, l.cell - 1, LightGray);
  }
  const bool onDark = index == sel;
  if (cell.value) {
    const char text[2] = {static_cast<char>('0' + cell.value), 0};
    const int font = UI_12_FONT_ID;
    const auto style = cell.given ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int tw = renderer.getTextWidth(font, text, style);
    renderer.drawText(font, x + (l.cell - tw) / 2, y + (l.cell - renderer.getLineHeight(font)) / 2, text, !onDark,
                      style);
    if (wrong[index]) renderer.drawRect(x + 4, y + 4, l.cell - 7, l.cell - 7, 2, !onDark);
  } else if (cell.notes) {
    const int sub = l.cell / 3;
    for (int d = 0; d < 9; ++d) {
      if (!(cell.notes & (1u << d))) continue;
      const char text[2] = {static_cast<char>('1' + d), 0};
      const int tw = renderer.getTextWidth(SMALL_FONT_ID, text);
      renderer.drawText(SMALL_FONT_ID, x + (d % 3) * sub + (sub - tw) / 2,
                        y + (d / 3) * sub + (sub - renderer.getLineHeight(SMALL_FONT_ID)) / 2, text, !onDark);
    }
  }
}

void SudokuActivity::render(RenderLock&&) {
  const Layout l = layout();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_SUDOKU), nullptr);

  const uint32_t secs = game.elapsedSeconds;
  char info[48];
  snprintf(info, sizeof(info), "%s   %u:%02u   %s", levelName(game.difficulty), static_cast<unsigned>(secs / 60),
           static_cast<unsigned>(secs % 60), notesMode ? tr(STR_SUDOKU_NOTES_ON) : "");
  renderer.drawText(UI_10_FONT_ID, l.gridX, l.infoY, info, true);
  if (solved) {
    const char* done = tr(STR_SUDOKU_SOLVED);
    renderer.drawText(UI_10_FONT_ID, l.gridX + l.cell * 9 - renderer.getTextWidth(UI_10_FONT_ID, done), l.infoY, done,
                      true, EpdFontFamily::BOLD);
  }

  const auto wrong = sudoku::conflicts(game.board);
  for (int i = 0; i < sudoku::CELLS; ++i) drawCell(l, i, wrong.data());
  for (int i = 0; i <= 9; ++i) {
    const int w = i % 3 == 0 ? 3 : 1;
    renderer.drawLine(l.gridX + i * l.cell, l.gridY, l.gridX + i * l.cell, l.gridY + 9 * l.cell, w, true);
    renderer.drawLine(l.gridX, l.gridY + i * l.cell, l.gridX + 9 * l.cell, l.gridY + i * l.cell, w, true);
  }

  // Digits 1-9; one that is all placed is left blank.
  int placed[10] = {};
  for (const auto& c : game.board.cells) placed[c.value]++;
  const int bw = width / 9;
  for (int d = 1; d <= 9; ++d) {
    const int x = (d - 1) * bw;
    renderer.drawRect(x + 2, l.padY, bw - 4, l.padH, 2, true);
    if (placed[d] >= 9) continue;
    const char text[2] = {static_cast<char>('0' + d), 0};
    const int tw = renderer.getTextWidth(UI_12_FONT_ID, text);
    renderer.drawText(UI_12_FONT_ID, x + (bw - tw) / 2, l.padY + (l.padH - renderer.getLineHeight(UI_12_FONT_ID)) / 2,
                      text, true);
  }

  const char* labels[TOOL_COUNT] = {tr(STR_SUDOKU_NOTES), tr(STR_SUDOKU_ERASE), tr(STR_SUDOKU_UNDO),
                                    tr(STR_SUDOKU_HINT),  tr(STR_SUDOKU_NEW),   levelName(level)};
  const int tw = width / TOOL_COUNT;
  for (int t = 0; t < TOOL_COUNT; ++t) {
    const int x = t * tw;
    const bool on = t == TOOL_NOTES && notesMode;
    if (on) renderer.fillRect(x + 2, l.toolY, tw - 4, l.toolH, true);
    renderer.drawRect(x + 2, l.toolY, tw - 4, l.toolH, 2, true);
    const int w = renderer.getTextWidth(SMALL_FONT_ID, labels[t]);
    renderer.drawText(SMALL_FONT_ID, x + (tw - w) / 2, l.toolY + (l.toolH - renderer.getLineHeight(SMALL_FONT_ID)) / 2,
                      labels[t], !on);
  }

  const auto hints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SUDOKU_DIGIT), "<", ">");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
