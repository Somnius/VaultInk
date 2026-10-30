#pragma once

#include <GfxRenderer.h>

#include "activities/UiListActivity.h"
#include "util/HomeRowOrder.h"

class MappedInputManager;

// Settings -> Display -> Home Screen Items (kos25k #14): which rows the VaultInk home shows. One
// switch per row in home order; Books and Settings always stay. Saved on leaving.
class HomeItemsActivity final : public UiListActivity {
 public:
  explicit HomeItemsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("HomeItems", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  int listCount() const override { return count; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rowItems[homerows::COUNT]{};
  uint8_t ids[homerows::COUNT]{};
  int count = 0;
  uint16_t hidden = 0;
};
