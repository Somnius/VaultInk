#pragma once
#include <string>

#include "ReadingPresetsStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Settings > Reader > Reading presets (kos25k #12), also the reader's classic menu: "Save preset" on top, then the
// ten slots. Selecting a slot copies its values into the settings (the reader lays the book out again on return);
// Save preset asks for a slot, whether to include the reader gestures (default no), then a name, and stores the
// current Text panel settings there. A preset saved with gestures sets them too when it is applied, and says so.
class ReadingPresetsActivity final : public UiListActivity {
 public:
  explicit ReadingPresetsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("ReadingPresets", renderer, mappedInput) {}

  static constexpr int ITEM_COUNT = ReadingPresetsStore::SLOTS + 1;

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  // The result is cancelled unless a slot was applied, so the reader only lays the book out again when needed.
  void onBackButton() override;

  // Save preset: the slot, then (touch boards) "Include gestures" No/Yes, then the name.
  void askGesturesAndSave(int slot);
  void askNameAndSave(int slot, bool includeGestures);

  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
  OptionPopup optionPopup;
  bool applied_ = false;
};
