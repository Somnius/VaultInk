#pragma once

#include <Gestures.h>
#include <GfxRenderer.h>

#include <string>

#include "I18nKeys.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class MappedInputManager;

// Settings -> Reader -> Gestures (KOReader parity milestone): what the reader does on a tap in each corner, a tap
// in the centre and a swipe each way, and the six places of the Quick Menu. A tap on a row opens a list of the
// actions. "Default" leaves a gesture to the older settings (Next Page, Previous Page, Tap Zones, Show Reader
// Menu), so nothing changes until a row is edited. The last row puts everything back to Default. Hardware buttons
// and the edge swipes are not listed: they cannot be remapped, so no choice here can lock the reader. Saved on
// leaving.
class GesturesActivity final : public UiListActivity {
 public:
  explicit GesturesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("Gestures", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int FIRST_QUICK = gestures::kSlotCount;
  static constexpr int ROW_RESET = FIRST_QUICK + gestures::kQuickMenuSize;
  static constexpr int ROW_COUNT = ROW_RESET + 1;

  int listCount() const override { return ROW_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  freeink::ui::ListItem rowItems[ROW_COUNT]{};
  std::string quickLabels[gestures::kQuickMenuSize];
  OptionPopup optionPopup;
  bool changed = false;
};
