#pragma once
#include <string>

#include "activities/UiListActivity.h"

// Settings > Reader > Experimental (kos25k #30): switches for text features that change how a book is laid out.
// Changing one lays the open book out again (the section cache keys on them).
class ExperimentalSettingsActivity final : public UiListActivity {
 public:
  explicit ExperimentalSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ExperimentalSettings", renderer, mappedInput) {}

  static constexpr int ITEM_COUNT = 5;

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;

  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
};
