#pragma once
#include <string>

#include "TypographyEngineOptions.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Settings > Reader > Typography Engine (kos25k #30): space condensing, letter spacing on loose lines, the locked
// space after an opening quote or dash, and the hyphenation mode, minimum letters and language. Each row opens a
// list of its choices. Changing one lays the open book out again (the section cache keys on them).
class TypographyEngineSettingsActivity final : public UiListActivity {
 public:
  explicit TypographyEngineSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("TypographyEngineSettings", renderer, mappedInput) {}

  static constexpr int ITEM_COUNT = typooptions::COUNT;

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  OptionPopup optionPopup;

  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;

  std::string rowValues_[ITEM_COUNT];
  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
};
