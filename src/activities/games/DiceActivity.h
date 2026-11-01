#pragma once

#include <Dice.h>
#include <GfxRenderer.h>

#include "activities/Activity.h"

class MappedInputManager;

// Dice (0.61.0), a built-in roller opened from Home > Plugins. The engine is lib/Dice: polyhedral dice
// with advantage and keep-highest, old World of Darkness, Vampire V5 (Hunger, Willpower reroll, Rouse),
// Chronicles of Darkness, Fate, PbtA, Blades in the Dark, Call of Cthulhu, Shadowrun, Savage Worlds, Year
// Zero and Cyberpunk Red. The top row picks the system, the rows under it are that system's numbers and
// switches, then Roll. Touch: tap -/+ or a switch, tap Roll. Buttons: Up/Down choose a row, Left/Right
// change it, Confirm rolls, Back leaves. What you set is kept for next time.
class DiceActivity final : public Activity {
 public:
  explicit DiceActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Dice", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override { save(); }
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int MAX_PARAMS = 6;
  static constexpr int HISTORY = 4;

  int paramCount() const;
  int extraCount() const;
  int rowCount() const { return 1 + paramCount() + 1 + extraCount(); }
  int rollRow() const { return 1 + paramCount(); }
  int rowY(int row) const;
  void load();
  void save() const;
  void changeSystem(int delta);
  void adjust(int row, int delta);
  void roll();
  void runExtra(int extra);
  void remember();
  void describe(const dice::Result& r, char* head, size_t headSize, char* info, size_t infoSize) const;
  bool handleTap(int x, int y);
  void handleButtons();
  void drawDie(const dice::Die& die, int x, int y, int size) const;
  void drawRow(int row, int y) const;

  int sys = 0;
  int16_t values[16][MAX_PARAMS]{};
  int focus = 0;
  dice::Result last{};
  bool hasLast = false;
  bool extraUsed = false;  // the Willpower reroll or push of this roll is spent
  char history[HISTORY][28]{};
  int historyCount = 0;
};
