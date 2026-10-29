#pragma once

#include <GfxRenderer.h>

#include "activities/UiListActivity.h"
#include "util/HomeRowOrder.h"

class MappedInputManager;

// Settings -> Display -> Home Screen Rows: the order of the VaultInk home's rows. Tap a row to pick
// it up, then tap where it goes (two taps for any move, no popups to refresh). The last row resets
// to the default order. Saved on leaving.
class HomeRowsActivity final : public UiListActivity {
 public:
  explicit HomeRowsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("HomeRows", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  static constexpr int RESET_ROW = homerows::COUNT;

  int listCount() const override { return homerows::COUNT + 1; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rowItems[homerows::COUNT + 1]{};
  homerows::Order order = homerows::DEFAULT_ORDER;
  int picked = -1;  // the row being moved, or -1
};
