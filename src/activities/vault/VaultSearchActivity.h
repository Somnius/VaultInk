#pragma once

#include <VaultIndex.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Search across every vault: brings each vault's index up to date (with a progress popup), asks
// for the words with the on-screen keyboard, lists the matching notes and opens the picked one.
class VaultSearchActivity final : public UiListActivity {
 public:
  explicit VaultSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;

 private:
  struct Result {
    int score;
    std::string path;
    std::string title;
    std::string where;  // "Vault · folder"
  };
  static constexpr size_t MAX_RESULTS = 60;

  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;

  void updateIndexes();
  void askForQuery();
  void runSearch(const std::string& text);
  void rebuildRows();

  bool indexed = false;
  bool askPending = true;
  std::string query;
  std::string queryLabel;
  std::vector<Result> results;
  std::vector<freeink::ui::ListItem> rows;
};
