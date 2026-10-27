#pragma once

#include <GfxRenderer.h>

#include "activities/UiListActivity.h"
#include "util/PaperTexture.h"

class MappedInputManager;

// Settings -> Reader -> Paper Button Cycle: a checklist of the page backgrounds the quick-settings
// paper tile steps through. At least one stays checked.
class PaperCycleActivity final : public UiListActivity {
 public:
  explicit PaperCycleActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("PaperCycle", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return paper_texture::STYLE_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rowItems[paper_texture::STYLE_COUNT]{};
  uint16_t workingMask = 0;
};
