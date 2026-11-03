#pragma once

#include <GfxRenderer.h>
#include <LibraryFormats.h>

#include "activities/UiListActivity.h"

class MappedInputManager;

// Library book formats (kos25k #18): one switch per type the Library can list. Reached from the gear
// in the Library header and from Settings > Library. Saved on leaving; the last switch stays on.
class LibraryFormatsActivity final : public UiListActivity {
 public:
  explicit LibraryFormatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("LibraryFormats", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;

 private:
  static constexpr int COUNT = 7;
  int listCount() const override { return COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rowItems[COUNT]{};
  uint8_t mask = library::FMT_ALL;
};
