#pragma once

#include <atomic>

#include "activities/UiListActivity.h"
#include "util/ThirdPartyData.h"

// Settings > System > About > Open-source licences: the components inside the firmware binary, one row
// each (name, licence under it), under a line that says what VaultInk's own licence is. A row opens a
// detail view of that component (purpose, licence, source, copyright); Back, Confirm or a tap returns to
// the list. The table is src/util/ThirdPartyData.h, generated from docs/third-party.json by
// scripts/gen_licenses.py. Names, licence ids and the table's texts are English on every language.
class LicencesActivity final : public UiListActivity {
 public:
  explicit LicencesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Licences", renderer, mappedInput) {}

  void onEnter() override;

 private:
  static constexpr int COUNT = static_cast<int>(thirdparty::COMPONENT_COUNT);

  int listCount() const override { return COUNT; }
  void buildScreen(UiScreen& screen) override;
  void buildDetail(UiScreen& screen, const thirdparty::Component& component);
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawFooter() override;

  freeink::ui::ListItem rowItems_[COUNT]{};
  // The row whose detail view is showing, -1 for the list. Written by loop(), read by the render task.
  std::atomic<int16_t> detail_{-1};
};
