#pragma once

#include <string>

#include "activities/UiListActivity.h"

// Settings > System > Battery (kos25k #20): what the battery chip reports, read-only and refreshed
// every few seconds. The X4 Pro's CW2017 gauge keeps no cycle count or health and can't stop
// charging, so only these are shown. Temperature only when the gauge returns a plausible value.
class BatteryActivity final : public UiListActivity {
 public:
  explicit BatteryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Battery", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;

 private:
  static constexpr int MAX_ROWS = 5;
  int listCount() const override { return rowCount; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int) override {}
  const char* headerTitle() const override;
  void readBattery();

  int rowCount = 0;
  unsigned long lastRead = 0;
  std::string values[MAX_ROWS];
  freeink::ui::ListItem rows[MAX_ROWS]{};
};
