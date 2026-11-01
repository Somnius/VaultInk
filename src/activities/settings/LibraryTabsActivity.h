#pragma once

#include <GfxRenderer.h>

#include "activities/UiListActivity.h"
#include "util/LibraryTabOrder.h"

class MappedInputManager;

// Settings -> Library -> Tab order (kos25k #18): the order of the Library's tabs. Tap a tab to pick it up, then
// tap where it goes. The last row resets the order. Saved on leaving.
class LibraryTabsActivity final : public UiListActivity {
 public:
  explicit LibraryTabsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("LibraryTabs", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  static constexpr int RESET_ROW = libtabs::COUNT;

  int listCount() const override { return libtabs::COUNT + 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rowItems[libtabs::COUNT + 1]{};
  libtabs::Order order = libtabs::DEFAULT_ORDER;
  int picked = -1;  // the tab being moved, or -1
};
