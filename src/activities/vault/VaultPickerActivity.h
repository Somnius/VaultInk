#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Lists the Obsidian vaults on the SD card (every folder in /Vaults, and top-level folders that
// hold .obsidian) and opens the picked one in the file browser.
class VaultPickerActivity final : public UiListActivity {
 public:
  explicit VaultPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;

 private:
  struct Vault {
    std::string name;
    std::string path;
    std::string last;      // the note read last in this vault, or ""
    std::string subtitle;  // "Last: <note>", or the path
  };

  int listCount() const override { return static_cast<int>(vaults.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  void findVaults();

  std::vector<Vault> vaults;
  std::vector<freeink::ui::ListItem> rowItems;
};
