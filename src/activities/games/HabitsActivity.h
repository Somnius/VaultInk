#pragma once

#include <GfxRenderer.h>
#include <Habits.h>

#include <memory>

#include "activities/Activity.h"

class MappedInputManager;

// Habits (0.63.0), a daily habit tracker built in next to Dice and Sudoku in Home > Plugins. The logic is
// lib/Habits. Each habit is a row with the last seven days as boxes (today on the right): tap a box to tick or
// untick that day; the streak and the total are on the row. Buttons: Up/Down choose a habit, Confirm ticks today,
// Left/Right move between the day boxes. The list is saved to /.vaultink/habits.txt.
class HabitsActivity final : public Activity {
 public:
  explicit HabitsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Habits", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override { save(); }
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode : uint8_t { Track, Remove };

  void load();
  void save() const;
  void addHabit();
  bool handleTap(int x, int y);
  int rowCountTotal() const;
  static int64_t nowEpoch();

  std::unique_ptr<habits::HabitList> list;
  Mode mode = Mode::Track;
  int focus = 0;
  int dayColumn = 6;  // 0 = six days ago ... 6 = today
  habits::Day today = 0;
  bool clockSet = true;
  int top = 0;
};
