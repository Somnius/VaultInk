#pragma once

#include <Epub.h>
#include <Epub/ReaderRenderSpec.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// Searches the open book for a word or phrase (kos25k #24): case and accents don't matter. Asks with
// the keyboard, then goes through the chapters in book order (laying out the ones the reader has not
// built yet, which keeps their cache for later), and lists each hit with its page in the chapter.
// Back while searching stops it and shows what was found so far. A pick finishes with a
// ProgressChangeResult for the reader.
class EpubSearchActivity final : public UiListActivity {
 public:
  explicit EpubSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                              const ReaderRenderSpec& spec);
  void onEnter() override;
  void loop() override;

 private:
  struct Hit {
    int spine;
    int page;
    int totalPages;
    uint32_t offset;       // visible text offset where the page starts
    std::string label;     // a few words around the hit
    std::string where;     // chapter title
    std::string pageText;  // "p. 12"
  };
  static constexpr size_t MAX_HITS = 100;
  static constexpr size_t MAX_HITS_PER_PAGE = 3;  // a page full of the word does not flood the list

  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;

  void askForQuery();
  void startSearch(const std::string& text);
  void searchChapter(int spineIndex);
  void finishSearch();
  void rebuildRows();

  std::shared_ptr<Epub> epub;
  ReaderRenderSpec spec;
  bool askPending = true;
  bool searching = false;
  int nextSpine = 0;
  std::string query;
  std::string prepared;
  std::string queryLabel;
  std::string progressText;
  std::vector<Hit> hits;
  std::vector<freeink::ui::ListItem> rows;
};
