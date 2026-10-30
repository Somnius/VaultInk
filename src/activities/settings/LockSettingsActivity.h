#pragma once
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Lock Screen page under System (kos25k #22): the lock type first, then when and how it opens, then
// the owner's name and phone that the lock screen (and optionally the sleep screen) shows.
class LockSettingsActivity final : public UiListActivity {
 public:
  explicit LockSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int ITEM_COUNT = 6;

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  OptionPopup optionPopup;

  int listCount() const override { return ITEM_COUNT; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  void editText(StrId title, char* target, size_t size);

  char holdText_[4] = {0};
  freeink::ui::ListItem rowItems_[ITEM_COUNT]{};
};
