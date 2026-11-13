#pragma once

#include <string>

#include "activities/UiListActivity.h"

// Settings → Network → Translation: the address, API key and Check certificate switch of a
// LibreTranslate server of the user's own. With no address the translation lookup asks MyMemory
// (no key), and the first row's value says so. A stored key is never shown or put back in the
// keyboard: the row says only whether one is set, and typing a new one replaces it (an empty
// entry removes it).
class TranslateSettingsActivity final : public UiListActivity {
 public:
  explicit TranslateSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 3;

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  std::string rowValues_[MENU_ITEMS];
  freeink::ui::ListItem rowItems_[MENU_ITEMS]{};
};
