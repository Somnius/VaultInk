#pragma once

#include <VaultIndex.h>

#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Search across every vault: brings each vault's index up to date (with a progress popup), asks
// for the words with the on-screen keyboard, lists the matching notes and opens the picked one.
// Given a note path instead, lists the notes in its vault that link to it (backlinks).
class VaultSearchActivity final : public UiListActivity {
 public:
  // `initialQuery` (from the settings search) runs at once instead of asking.
  explicit VaultSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string backlinksOf = {},
                               std::string initialQuery = {});
  void onEnter() override;
  void loop() override;

 private:
  struct Result {
    int score;
    std::string path;
    std::string title;
    std::string where;    // "Vault · folder"
    std::string heading;  // matched heading the note opens at, or ""
    std::string label;    // "Title > Heading", or the title
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
  void findBacklinks();
  bool backlinkMode() const { return !backlinksOf.empty(); }
  // Rows before the first result: the query row in search mode, none for backlinks.
  int resultOffset() const { return backlinkMode() ? 0 : 1; }

  std::string backlinksOf;
  bool indexed = false;
  bool askPending = true;
  std::string initialQuery;
  std::string query;
  std::string queryLabel;
  std::vector<Result> results;
  std::vector<freeink::ui::ListItem> rows;
};
