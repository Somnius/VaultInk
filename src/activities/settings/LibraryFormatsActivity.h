#pragma once

#include <GfxRenderer.h>
#include <LibraryFormats.h>

#include "activities/UiListActivity.h"

class MappedInputManager;

// Library book formats (kos25k #18): one switch per type the Library can list. Reached from the gear
// in the Library header and from Settings > Library. Saved on leaving; the last switch stays on.
class LibraryFormatsActivity final : public UiListActivity {
 public:
  // `browser` picks the Browse Files list (kos25k #6) instead of the Library's; both start with an All row.
  explicit LibraryFormatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool browser = false)
      : UiListActivity("LibraryFormats", renderer, mappedInput), browser(browser) {}

  void onEnter() override;
  void onExit() override;

 private:
  static constexpr int COUNT = 8;  // All, then the seven formats
  int listCount() const override { return COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rowItems[COUNT]{};
  uint8_t mask = library::FMT_ALL;
  bool browser = false;
  uint8_t& stored() const;
};
