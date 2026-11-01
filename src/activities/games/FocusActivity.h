#pragma once

#include <Focus.h>
#include <GfxRenderer.h>

#include "activities/Activity.h"

class MappedInputManager;

// Focus timer (0.63.0), a Pomodoro timer built in next to Dice and Sudoku in Home > Plugins. The state machine is
// lib/Focus; the timer runs on the wall clock, so a sleeping reader still knows when the phase ended. The screen is
// redrawn once a minute (and when something changes), not every second: it is e-ink. Finished phases are logged to
// /.vaultink/focus.tsv. Touch: Start/Pause, Skip, Stop and Settings buttons; buttons: Confirm starts or pauses,
// Left skips, Right stops, Up opens the settings, Back leaves.
class FocusActivity final : public Activity {
 public:
  explicit FocusActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Focus", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override { save(); }
  bool preventAutoSleep() override { return keepAwake && timer.running(); }
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Screen : uint8_t { Timer, Settings };

  void load();
  void save() const;
  void logPhase(focus::Phase phase, int64_t endedAt, bool completed) const;
  void tick(int64_t now);
  void primary(int64_t now);
  void cycleSetting(int row);
  bool handleTap(int x, int y);
  void drawTimer() const;
  void drawSettings() const;
  static int64_t nowEpoch();

  focus::Timer timer;
  Screen screen = Screen::Timer;
  int focusRow = 0;
  bool keepAwake = true;
  bool vibrate = true;
  int64_t lastDay = 0;
  uint32_t lastDraw = 0;
  uint32_t lastTick = 0;
  focus::Phase lastPhase = focus::Phase::Idle;
  int64_t phaseStart = 0;
  mutable int64_t shownNow = 0;
};
