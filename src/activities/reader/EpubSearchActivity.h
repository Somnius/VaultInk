#pragma once

#include <Epub.h>
#include <Epub/ReaderRenderSpec.h>
#include <HalStorage.h>
#include <ZipFile.h>

#include <memory>
#include <string>
#include <vector>

#include "BookTextSearch.h"
#include "CrossPointSettings.h"
#include "SearchSession.h"
#include "activities/UiListActivity.h"
#include "util/SearchIndex.h"

// Searches the open book for a word or phrase (kos25k #24): case and accents don't matter. Asks with
// the keyboard, then reads the chapters' text in book order, without laying anything out
// (ChapterTextSearcher), in steps of about 50 ms per loop() so Back is always seen. Hits appear in
// the list as chapters finish. Back while searching stops it and keeps what was found; a pick
// finishes with a ProgressChangeResult (spine + visible text offset) for the reader.
class EpubSearchActivity final : public UiListActivity {
 public:
  explicit EpubSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::shared_ptr<Epub> epub,
                              const ReaderRenderSpec& spec, SearchSession* session = nullptr, int limitSpine = -1,
                              uint32_t limitOffset = UINT32_MAX, int startSpine = 0);
  void onEnter() override;
  void loop() override;

 private:
  struct Hit {
    int spine;
    uint32_t offset;     // visible text offset of the match
    std::string label;   // a few words around the hit
    std::string detail;  // "chapter name · 37%" or "chapter name · p. 12"
  };
  static constexpr size_t READ_CHUNK = 4096;
  static constexpr size_t MAX_HITS = 100;
  static constexpr unsigned long STEP_MS = 50;      // work per loop() before input is looked at again
  static constexpr unsigned long REDRAW_MS = 1500;  // e-ink: at most one refresh per this while searching

  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;

  static constexpr freeink::ui::ActionId ACTION_TYPE = ACTION_USER;
  static void typeActionTrampoline(const freeink::ui::ActionEvent& event, void* user);
  void openTypeSettings(bool restart = true);
  // The range setting applies to the next search: up to the reading point, or the whole book.
  int limit() const { return SETTINGS.searchScope == 1 ? limitSpine : -1; }
  uint32_t limitOff() const { return SETTINGS.searchScope == 1 ? limitOffset : UINT32_MAX; }

  void askForQuery();
  void startSearch(const std::string& text);
  void stepSearch();
  bool openChapter(int spineIndex);
  int spineAt(int searchStep) const;
  int stepTotal() const;
  std::string cacheKey() const;
  uint32_t chapterStamp(int spineIndex) const;
  bool skippedByIndex(int spineIndex);
  void noteChapterDone();
  void rememberFinished();
  void finishChapter();
  void closeChapter();
  void mergePendingHits();
  bool backRequested();
  void finishSearch();
  void rebuildRows();

  std::shared_ptr<Epub> epub;
  ReaderRenderSpec spec;
  SearchSession* session;  // gets the hits when one is picked (null in tests)
  int limitSpine;          // the reading point: the chapter (and offset in it) the reader was at
  uint32_t limitOffset;
  int startSpine;               // the chapter the reader is in: searched first, then the ones before it (wrapping)
  bool chapterSkipped = false;  // a chapter could not be opened, so the result is not the whole answer
  int chaptersSkipped = 0;      // by the index, for the log
  int step = 0;                 // chapters finished in this search
  bool askPending = true;
  bool searching = false;
  int nextSpine = 0;
  std::string query;
  std::string prepared;
  std::string queryLabel;
  std::string progressText;
  std::unique_ptr<ChapterTextSearcher> searcher;
  HalFile chapterFile;  // the chapter's unzipped html in the book cache, when it is there
  bool chapterFromFile = false;
  std::unique_ptr<ZipEntryReader> entryReader;  // the chapter in the zip, when it is not in the cache
  std::unique_ptr<uint8_t[]> readBuf;
  bool swallowBackRelease = false;  // Back stopped the search; its release must not also leave
  bool rowsDirty = false;
  unsigned long lastRedrawMs = 0;
  std::vector<Hit> hits;
  std::vector<Hit> pendingHits;  // found since the last redraw; merged under the render lock
  int chaptersDone = 0;
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> fitted;  // the result lines cut to the row width, rebuilt per screen

  // The opt-in search index (Settings -> Reader -> Search word): chapters it knows lack the words are skipped, and
  // the ones read now fill it.
  std::unique_ptr<SearchIndex> searchIndex;
  std::unique_ptr<uint8_t[]> filterBits;  // trigramfilter::kBytes
  std::vector<uint32_t> queryGrams;
};
