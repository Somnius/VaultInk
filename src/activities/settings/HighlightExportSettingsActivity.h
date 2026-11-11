#pragma once

#include <string>

#include "activities/UiListActivity.h"

// Settings → Network → Highlight Exports: the Readwise access token, and the address, access
// token and Check certificate switch of a Memos server. A stored token is never shown or put back
// in the keyboard: the row says only whether one is set, and typing a new one replaces it (an
// empty entry removes it).
class HighlightExportSettingsActivity final : public UiListActivity {
 public:
  explicit HighlightExportSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 4;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  std::string rowValues_[MENU_ITEMS];
  freeink::ui::ListItem rowItems_[MENU_ITEMS]{};
};
