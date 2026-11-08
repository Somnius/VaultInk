#pragma once

#include <GfxRenderer.h>

#include <utility>
#include <vector>

#include "I18nKeys.h"
#include "activities/UiListActivity.h"
#include "util/MenuLayout.h"

class MappedInputManager;

// Settings -> Reader -> Book menu options (kos25k #15): the order of the entries in the reader's More and Text
// panels, and which of them are shown. Two switch rows at the top: which panel, and what a tap on an entry does
// (moves it: tap it, then tap where it goes, or shows/hides it). The last row resets the panel. Saved on leaving.
class BookMenuOptionsActivity final : public UiListActivity {
 public:
  explicit BookMenuOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("BookMenuOptions", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  static constexpr int MAX_ENTRIES = 40;
  static constexpr int ROW_PANEL = 0;
  static constexpr int ROW_MODE = 1;
  static constexpr int FIRST_ENTRY = 2;

  int listCount() const override { return FIRST_ENTRY + static_cast<int>(order.size()) + 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void loadPanel();   // `order` and the hidden mask from the settings
  void storePanel();  // and back, when something changed
  const char* entryLabel(int id) const;
  bool locked(int id) const;

  freeink::ui::ListItem rowItems[FIRST_ENTRY + MAX_ENTRIES + 1]{};
  bool textPanel = false;                     // false: More
  bool moveMode = true;                       // false: a tap shows or hides
  std::vector<std::pair<int, StrId>> labels;  // each entry's id and name, read once per panel
  std::vector<int> defaults;
  std::vector<int> order;  // every entry of the panel, hidden ones too
  uint32_t hidden = 0;
  int picked = -1;  // the entry being moved (a position in `order`), or -1
  bool changed = false;
};
