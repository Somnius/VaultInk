#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Lists the Obsidian vaults on the SD card (every folder in /Vaults, and top-level folders that
// hold .obsidian) and opens the picked one in the file browser. Below them, the last notes read.
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

  // The notes read last, from any vault (the recent list), under the vaults.
  struct RecentNote {
    std::string title;
    std::string path;
    std::string vault;
  };
  static constexpr size_t MAX_RECENT_NOTES = 5;

  int listCount() const override { return static_cast<int>(vaults.size() + recentNotes.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;
  void findVaults();
  void findRecentNotes();

  std::vector<Vault> vaults;
  std::vector<RecentNote> recentNotes;
  std::vector<freeink::ui::ListItem> rowItems;
};
