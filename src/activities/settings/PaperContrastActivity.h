#pragma once

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Settings -> Reader -> Paper Button Cycle -> Paper contrast (kos25k #1): a slider from 0 to 100 for how
// many of the paper texture's dots are drawn, with a tile of every background above it that changes while
// the slider moves. Confirm keeps the value, Back leaves the old one.
class PaperContrastActivity final : public Activity, private UiAppHost {
 public:
  explicit PaperContrastActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static void contrastScreen(UiScreen& screen, void* user);
  static void onSliderEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onOkEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildContrastScreen(UiScreen& screen);
  void drawPreview() const;
  void setContrast(int value);
  void confirm();
  void cancel();

  int contrast;
  ButtonNavigator buttonNavigator;
  bool draggingSlider = false;  // swallows the swipe/tap fallout of a drag, like the percent dialog
};
