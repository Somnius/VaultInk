#pragma once

#include <GfxRenderer.h>
#include <Sudoku.h>

#include "activities/Activity.h"

class MappedInputManager;

// Sudoku (0.59.0), a built-in game opened from Home > Plugins. The engine is lib/Sudoku. Touch: tap a cell,
// then a digit (or Notes first for pencil marks); Erase, Undo, Hint, a new puzzle and the level sit under
// the digits. Buttons: Up/Left and Down/Right move the cursor, Confirm cycles the digit in the cell, a long
// Confirm switches notes on or off, Back leaves. The game is saved when you leave and when the device sleeps.
class SudokuActivity final : public Activity {
 public:
  explicit SudokuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sudoku", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override { save(); }
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Tool : uint8_t { TOOL_NOTES, TOOL_ERASE, TOOL_UNDO, TOOL_HINT, TOOL_NEW, TOOL_LEVEL, TOOL_COUNT };
  struct Layout {
    int gridX, gridY, cell, padY, padH, toolY, toolH, infoY;
  };
  Layout layout() const;
  int cellAt(int x, int y) const;
  void startNew();
  void load();
  void save() const;
  void tick();
  void pressDigit(uint8_t digit, bool forceValue = false);
  void pressTool(Tool tool);
  bool handleTap(int x, int y);
  void handleButtons();
  void drawCell(const Layout& l, int index, const bool* wrong) const;

  sudoku::Game game;
  sudoku::Difficulty level = sudoku::Difficulty::Medium;
  int selected = 40;
  bool notesMode = false;
  bool solved = false;
  unsigned long lastTickMs = 0;
};
