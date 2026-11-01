#include "EpubSearchActivity.h"

#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrigramFilter.h>
#include <WordSearch.h>
#include <ZipFile.h>

#include <algorithm>
#include <functional>
#include <iterator>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/settings/SearchWordSettingsActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/headerIcons.h"

namespace fui = freeink::ui;

EpubSearchActivity::EpubSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       std::shared_ptr<Epub> epub, const ReaderRenderSpec& spec, SearchSession* session,
                                       const int limitSpine, const uint32_t limitOffset, const int startSpine)
    : UiListActivity("EpubSearch", renderer, mappedInput),
      epub(std::move(epub)),
      spec(spec),
      session(session),
      limitSpine(limitSpine),
      limitOffset(limitOffset),
      startSpine(startSpine) {}

const char* EpubSearchActivity::headerTitle() const { return tr(STR_SEARCH_IN_BOOK); }

void EpubSearchActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_TYPE, &EpubSearchActivity::typeActionTrampoline, this);
  rebuildRows();
  requestUpdate();
}

// Back, whichever way it arrives: a press, a release or a hold. Looked at on every step; the flags are
// per input update, so a tap that ends before the next step is still seen.
bool EpubSearchActivity::backRequested() {
  using Button = MappedInputManager::Button;
  if (mappedInput.wasReleased(Button::Back)) return true;
  if (mappedInput.wasPressed(Button::Back) || mappedInput.isPressed(Button::Back)) {
    swallowBackRelease = true;  // the release that follows must not also leave the screen
    return true;
  }
  return false;
}

void EpubSearchActivity::loop() {
  if (askPending) {
    askPending = false;
    askForQuery();
    return;
  }
  if (searching) {
    if (backRequested()) {
      finishSearch();
      return;
    }
    UiListActivity::loop();  // scroll and pick while it works
    if (searching) stepSearch();
    return;
  }
  if (swallowBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      swallowBackRelease = false;
      return;
    }
    if (!mappedInput.isPressed(MappedInputManager::Button::Back)) swallowBackRelease = false;
  }
  UiListActivity::loop();
}

void EpubSearchActivity::askForQuery() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH_IN_BOOK), query, 80);
  if (!keyboard) {
    LOG_ERR("BSR", "OOM: search keyboard");
    onBackButton();
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      if (query.empty()) onBackButton();  // nothing searched yet: back to the reader menu
      return;
    }
    startSearch(std::get<KeyboardResult>(result.data).text);
  });
}

void EpubSearchActivity::startSearch(const std::string& text) {
  closeChapter();
  query = text;
  prepared = wordsearch::prepareQuery(query);
  hits.clear();
  pendingHits.clear();
  step = 0;
  chapterSkipped = false;
  chaptersSkipped = 0;
  nextSpine = spineAt(0);
  chaptersDone = 0;
  searching = !prepared.empty();
  searchIndex.reset();
  filterBits.reset();
  queryGrams.clear();
  if (searching && SETTINGS.searchIndex != 0) {
    filterBits = makeUniqueNoThrow<uint8_t[]>(trigramfilter::kBytes);
    auto index = makeUniqueNoThrow<SearchIndex>(epub->getCachePath() + "/search.idx", epub->getSpineItemsCount());
    if (filterBits && index && index->open()) {
      searchIndex = std::move(index);
      queryGrams = trigramfilter::queryTrigrams(prepared);
    }
  }
  if (searching && session && !session->lastKey.empty() && session->lastKey == cacheKey()) {
    // The same words again: the finished list is still here.
    std::transform(session->lastFound.begin(), session->lastFound.end(), std::back_inserter(hits),
                   [](const SearchSession::Found& f) { return Hit{f.spine, f.offset, f.label, f.detail}; });
    searching = false;
    nav.selected = hits.empty() ? 0 : 1;
    rebuildRows();
    requestUpdate();
    return;
  }
  lastRedrawMs = millis();
  progressText.clear();
  rebuildRows();
  requestUpdate();
}

// A chapter's size plus one: the index slot is only good while the chapter has the size it was read at.
uint32_t EpubSearchActivity::chapterStamp(const int spineIndex) const {
  const size_t end = epub->getCumulativeSpineItemSize(spineIndex);
  const size_t begin = spineIndex > 0 ? epub->getCumulativeSpineItemSize(spineIndex - 1) : 0;
  return static_cast<uint32_t>(end > begin ? end - begin : 0) + 1;
}

// True when the index already knows this chapter and its filter lacks the searched words.
bool EpubSearchActivity::skippedByIndex(const int spineIndex) {
  if (!searchIndex || queryGrams.empty()) return false;
  if (!searchIndex->read(spineIndex, chapterStamp(spineIndex), filterBits.get())) return false;
  return !trigramfilter::mayContain(filterBits.get(), queryGrams);
}

// A search over the book, spine by spine from the reader's chapter: the chapters after it, then the ones before.
int EpubSearchActivity::spineAt(const int searchStep) const {
  const int bookTotal = epub->getSpineItemsCount();
  if (limitSpine >= 0 || bookTotal <= 0) return searchStep;  // up to the reading point: in book order
  return (std::clamp(startSpine, 0, bookTotal - 1) + searchStep) % bookTotal;
}

int EpubSearchActivity::stepTotal() const {
  const int bookTotal = epub->getSpineItemsCount();
  return limitSpine >= 0 ? std::min(bookTotal, limitSpine + 1) : bookTotal;
}

// What makes two searches of this book the same: the words, whole words or not, and how far it reads.
std::string EpubSearchActivity::cacheKey() const {
  return prepared + "|" + std::to_string(SETTINGS.searchType) + "|" + std::to_string(limitSpine) + "|" +
         std::to_string(limitOffset);
}

// Only a search that read every chapter is worth keeping; one that was stopped is not the whole answer.
void EpubSearchActivity::rememberFinished() {
  if (!session) return;
  session->lastKey = cacheKey();
  session->lastFound.clear();
  std::transform(hits.begin(), hits.end(), std::back_inserter(session->lastFound),
                 [](const Hit& h) { return SearchSession::Found{h.spine, h.offset, h.label, h.detail}; });
}

void EpubSearchActivity::finishSearch() {
  closeChapter();
  searching = false;
  LOG_DBG("BSR", "Search \"%s\": %u hits, stopped at chapter %d of %d, %d skipped by the index", query.c_str(),
          static_cast<unsigned>(hits.size() + pendingHits.size()), nextSpine, epub->getSpineItemsCount(),
          chaptersSkipped);
  progressText.clear();
  mergePendingHits();
  nav.selected = hits.empty() ? 0 : 1;
  requestUpdate();
}

// Moves what was found since the last redraw into the list; the render task reads it, so under its lock.
void EpubSearchActivity::mergePendingHits() {
  RenderLock lock;
  hits.insert(hits.end(), std::make_move_iterator(pendingHits.begin()), std::make_move_iterator(pendingHits.end()));
  pendingHits.clear();
  rebuildRows();
}

void EpubSearchActivity::closeChapter() {
  searcher.reset();
  entryReader.reset();
  if (chapterFile) chapterFile.close();
  chapterFromFile = false;
}

// Starts reading chapter `spineIndex`: from the unzipped html in the book cache when an earlier layout
// left it there, else from the zip entry a piece at a time. Nothing is laid out.
bool EpubSearchActivity::openChapter(const int spineIndex) {
  closeChapter();
  const size_t room = MAX_HITS - hits.size() - pendingHits.size();
  searcher = makeUniqueNoThrow<ChapterTextSearcher>(prepared, room, nullptr, ChapterTextSearcher::kHitsPerBucket,
                                                    SETTINGS.searchType == 1);
  if (!searcher || !searcher->ok()) {
    LOG_ERR("BSR", "OOM: chapter %d searcher", spineIndex);
    closeChapter();
    return false;
  }
  if (searchIndex) {
    std::fill_n(filterBits.get(), trigramfilter::kBytes, uint8_t{0});
    searcher->collectTrigrams(filterBits.get());
  }
  if (!readBuf) readBuf = makeUniqueNoThrow<uint8_t[]>(READ_CHUNK);
  if (!readBuf) {
    LOG_ERR("BSR", "OOM: read buffer");
    closeChapter();
    return false;
  }
  const std::string htmlPath = epub->getCachePath() + "/html/" + std::to_string(spineIndex) + ".html";
  chapterFromFile = Storage.exists(htmlPath.c_str()) && Storage.openFileForRead("BSR", htmlPath, chapterFile);
  if (!chapterFromFile) entryReader = epub->openItemReader(epub->getSpineItem(spineIndex).href);
  return true;
}

// One slice of work, about STEP_MS: loop() reads the buttons before the next one.
void EpubSearchActivity::stepSearch() {
  const int total = stepTotal();
  if (step >= total || hits.size() + pendingHits.size() >= MAX_HITS) {
    const bool whole = step >= total && !chapterSkipped;  // a full list or a chapter that would not open is no answer
    finishSearch();
    if (whole) rememberFinished();
    return;
  }
  nextSpine = spineAt(step);
  if (!searcher && skippedByIndex(nextSpine)) {
    ++chaptersSkipped;
    noteChapterDone();
    return;
  }
  const unsigned long start = millis();
  if (!searcher) {
    if (!openChapter(nextSpine)) {
      chapterSkipped = true;
      ++step;
      return;
    }
  }
  bool chapterEnded = false;
  if (!chapterFromFile && !entryReader) {
    // Encrypted or note content: no way to take it in pieces, read it in one go.
    epub->readItemContentsToStream(epub->getSpineItem(nextSpine).href, *searcher, READ_CHUNK, true);
    chapterEnded = true;
  } else {
    while (millis() - start < STEP_MS) {
      const int n =
          chapterFromFile ? chapterFile.read(readBuf.get(), READ_CHUNK) : entryReader->read(readBuf.get(), READ_CHUNK);
      if (n < 0) LOG_ERR("BSR", "Chapter %d: read failed, kept the hits before it", nextSpine);
      if (n <= 0) {
        chapterEnded = true;
        break;
      }
      // A short write means the hit cap was reached or the XML is broken: the hits so far stay.
      if (searcher->write(readBuf.get(), static_cast<size_t>(n)) != static_cast<size_t>(n)) {
        chapterEnded = true;
        break;
      }
    }
  }
  if (chapterEnded) finishChapter();
}

void EpubSearchActivity::finishChapter() {
  const int spineIndex = nextSpine;
  searcher->finish();
  if (!searcher->ok()) LOG_ERR("BSR", "Chapter %d: broken XML, kept the hits before it", spineIndex);
  const uint32_t chapterTotal = std::max<uint32_t>(searcher->visibleTotal(), 1);
  std::string where;
  const int toc = epub->getTocIndexForSpineIndex(spineIndex);
  if (toc >= 0) where = epub->getTocItem(toc).title;
  if (!searcher->hits().empty()) {
    // "p. N" when the reader has laid this chapter out with the current settings, else the percent.
    auto section = makeUniqueNoThrow<Section>(epub, spineIndex, renderer);
    const bool laidOut = section && section->loadSectionFile(spec) && !section->isPartial();
    for (const auto& found : searcher->hits()) {
      if (spineIndex == limitSpine && found.offset > limitOffset) continue;  // past the reading point
      Hit hit;
      hit.spine = spineIndex;
      hit.offset = found.offset;
      hit.label = found.snippet;
      std::string place;
      const auto page = laidOut ? section->getPageForVisibleTextOffset(found.offset) : std::nullopt;
      if (page) {
        place = std::string(tr(STR_PAGE_SHORT)) + " " + std::to_string(*page + 1);
      } else {
        place = std::to_string(static_cast<uint64_t>(found.offset) * 100 / chapterTotal) + "%";
      }
      hit.detail = where.empty() ? place : where + " \xC2\xB7 " + place;
      pendingHits.push_back(std::move(hit));
    }
  }
  if (searchIndex && searcher->trigramsComplete()) {
    searchIndex->write(spineIndex, chapterStamp(spineIndex), filterBits.get());
  }
  closeChapter();
  noteChapterDone();
}

void EpubSearchActivity::noteChapterDone() {
  ++step;
  ++chaptersDone;
  // One e-ink refresh now and then, not one per chapter.
  if (millis() - lastRedrawMs >= REDRAW_MS) {
    lastRedrawMs = millis();
    {
      RenderLock lock;
      hits.insert(hits.end(), std::make_move_iterator(pendingHits.begin()), std::make_move_iterator(pendingHits.end()));
      pendingHits.clear();
      char buf[64];
      snprintf(buf, sizeof(buf), tr(STR_SEARCH_PROGRESS), chaptersDone + 1, epub->getSpineItemsCount());
      progressText = buf;
      rebuildRows();
    }
    requestUpdate();
  }
}

void EpubSearchActivity::rebuildRows() {
  queryLabel = std::string(tr(STR_SEARCH_IN_BOOK)) + ": " + query;
  rows.assign(hits.size() + 1, fui::ListItem{});
  rows[0].label = queryLabel.c_str();
  if (searching) {
    rows[0].subtitle = progressText.empty() ? tr(STR_SEARCHING) : progressText.c_str();
  } else {
    rows[0].subtitle = query.empty() ? nullptr : (hits.empty() ? tr(STR_NO_RESULTS) : tr(STR_SEARCH_AGAIN_HINT));
  }
  rows[0].actionValue = 0;
  for (size_t i = 0; i < hits.size(); ++i) {
    rows[i + 1].label = hits[i].label.c_str();
    rows[i + 1].subtitle = hits[i].detail.c_str();
    rows[i + 1].actionValue = static_cast<int16_t>(i + 1);
  }
}

void EpubSearchActivity::typeActionTrampoline(const fui::ActionEvent&, void* user) {
  static_cast<EpubSearchActivity*>(user)->openTypeSettings();
}

// The gear next to the title: part of a word or whole words. A search already on screen runs again with it.
void EpubSearchActivity::openTypeSettings() {
  app.clearTapFlash();
  if (searching) finishSearch();
  const uint8_t before = SETTINGS.searchType;
  auto screen = makeUniqueNoThrow<SearchWordSettingsActivity>(renderer, mappedInput, true);
  if (!screen) return;
  startActivityForResult(std::move(screen), [this, before](const ActivityResult&) {
    if (SETTINGS.searchType != before && !query.empty()) startSearch(query);
    requestUpdate();
  });
}

void EpubSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  if (mappedInput.hasTouch()) {
    // Header gear at the right edge, away from the home button at the top centre (kos25k #24): the header
    // control has no slot for it, so it is drawn by hand.
    const int16_t size = static_cast<int16_t>(metrics.headerHeight - 24);
    const int16_t x = static_cast<int16_t>(renderer.getScreenWidth() - metrics.contentSidePadding - 8 - size);
    fui::ButtonProps gear;
    gear.icon = fui::bitmapFromIcon(icon_settings_32);
    gear.action = ACTION_TYPE;
    gear.minTouchSize = 44;
    fui::button(screen.frame(), fui::Rect{x, static_cast<int16_t>(metrics.topPadding + 22), size, size}, gear);
  }
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  // Each result line is cut to the row's width, with no dots at the end, so it fills the line.
  {
    const fui::TextStyle style = screen.theme().bodyText;
    const int available = screen.frame().screen().width - 2 * (metrics.contentSidePadding + 8) - 28;
    fitted.resize(hits.size());
    for (size_t i = 0; i < hits.size() && i + 1 < rows.size(); ++i) {
      fitted[i] = hits[i].label;
      while (!fitted[i].empty() &&
             screen.target().measureText(style.font, fitted[i].c_str(), style).width > available) {
        size_t cut = fitted[i].size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(fitted[i][cut]) & 0xC0) == 0x80) --cut;  // whole characters
        fitted[i].erase(cut);
      }
      rows[i + 1].label = fitted[i].c_str();
    }
  }
  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubSearchActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index == 0) {
    closeChapter();
    searching = false;
    progressText.clear();
    mergePendingHits();
    askForQuery();
    return;
  }
  const int i = index - 1;
  if (i < 0 || i >= static_cast<int>(hits.size())) return;
  closeChapter();
  searching = false;
  ProgressChangeResult result{};
  result.spineIndex = hits[i].spine;
  result.hasSavedProgress = true;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = hits[i].offset;  // page and totalPages unknown: the reader jumps by the offset
  if (session) {
    // The reader keeps the whole list to step through; the pick is where it starts.
    session->query = prepared;
    session->hits.clear();
    session->hits.reserve(hits.size());
    for (const auto& hit : hits) session->hits.push_back({hit.spine, hit.offset});
    session->current = i;
  }
  setResult(std::move(result));
  finish();
}

void EpubSearchActivity::onBackButton() {
  // Back leaves without moving; the reader reopens its menu.
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}
