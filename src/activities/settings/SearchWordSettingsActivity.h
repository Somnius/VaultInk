#pragma once

#include <GfxRenderer.h>

#include "activities/UiListActivity.h"

class MappedInputManager;

// Search word settings (kos25k #24): Search in Book in the More menu, the range, the type (part of
// a word or whole words) and the book index. With typeOnly it is the short version opened from the gear in Search in
// Book: just the two types. Every change is saved at once.
class SearchWordSettingsActivity final : public UiListActivity {
 public:
  SearchWordSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool typeOnly)
      : UiListActivity("SearchWordSettings", renderer, mappedInput), typeOnly(typeOnly) {}

 private:
  static constexpr int MAX_ROWS = 4;
  int listCount() const override { return typeOnly ? 2 : MAX_ROWS; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  bool typeOnly;
  freeink::ui::ListItem rowItems[MAX_ROWS]{};
};
