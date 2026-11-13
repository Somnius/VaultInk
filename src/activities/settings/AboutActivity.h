#pragma once
#include <string>

#include "activities/UiListActivity.h"

// VaultInk logo and credits, then read-only device information: detected hardware (device profile, display
// controller, touch, frontlight, RTC, IMU), firmware version, and MAC address.
// Rows are informational; activating one does nothing, except the first, which opens
// LicencesActivity (the open-source licences of what the firmware contains).
class AboutActivity final : public UiListActivity {
 public:
  explicit AboutActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 12;

  void onEnter() override;

 private:
  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void buildCredits(UiScreen& screen);
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  std::string rowValues_[ITEM_COUNT];
  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
};
