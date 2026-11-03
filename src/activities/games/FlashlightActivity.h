#pragma once

#include <GfxRenderer.h>

#include "activities/Activity.h"

class MappedInputManager;

// Flashlight (0.65.0, kos25k #28): the front light at full brightness and a chosen warmth, steady, blinking, or as an
// S.O.S. in Morse (three short, three long, three short; a short flash is the flash speed, a long one three times
// that). Built in next to the Focus timer in Home > Plugins, because SD plugins cannot run code. The light's earlier
// state comes back when it is stopped or the tool is left. The screen is drawn once, not while flashing: it is e-ink.
// Touch: tap a row to change it, tap the big button to start or stop; buttons: Up/Down pick a row, Confirm changes it,
// Left starts or stops, Back leaves.
class FlashlightActivity final : public Activity {
 public:
  explicit FlashlightActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Flashlight", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  bool preventAutoSleep() override { return running; }
  void loop() override;
  void render(RenderLock&&) override;

  enum class Mode : uint8_t { Sos = 0, Blink, Steady, Count };

 private:
  static constexpr int ROWS = 4;  // mode, brightness, warmth, flash speed

  void load();
  void save() const;
  void start();
  void stop();
  void cycleRow(int row);
  bool handleTap(int x, int y);
  void tick();
  bool hasWarmth() const { return warmthPresent; }

  Mode mode = Mode::Sos;
  uint8_t brightness = 100;
  uint8_t warmth = 50;
  uint16_t unitMs = 300;  // one short flash (and one gap) in S.O.S. and Blink
  int focusRow = 0;
  bool warmthPresent = false;  // the light has a warmth control (decided in onEnter)

  bool running = false;
  bool lampOn = false;
  uint32_t stepStart = 0;  // millis() when the current step began
  uint32_t stepMs = 0;     // how long it lasts
  uint8_t stepIndex = 0;   // position in the pattern
  bool wasOn = false;      // the light's state before start()
  uint8_t wasBrightness = 60;
  uint8_t wasWarmth = 50;
};
