#include "EpubSearchActivity.h"

#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WordSearch.h>
#include <ZipFile.h>

#include <algorithm>
#include <functional>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

EpubSearchActivity::EpubSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       std::shared_ptr<Epub> epub, const ReaderRenderSpec& spec)
    : UiListActivity("EpubSearch", renderer, mappedInput), epub(std::move(epub)), spec(spec) {}

const char* EpubSearchActivity::headerTitle() const { return tr(STR_SEARCH_IN_BOOK); }

void EpubSearchActivity::onEnter() {
  UiListActivity::onEnter();
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
    finish();
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      if (query.empty()) finish();  // nothing searched yet: back to the reader menu
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
  nextSpine = 0;
  chaptersDone = 0;
  searching = !prepared.empty();
  lastRedrawMs = millis();
  progressText.clear();
  rebuildRows();
  requestUpdate();
}

void EpubSearchActivity::finishSearch() {
  closeChapter();
  searching = false;
  LOG_DBG("BSR", "Search \"%s\": %u hits, stopped at chapter %d of %d", query.c_str(),
          static_cast<unsigned>(hits.size() + pendingHits.size()), nextSpine, epub->getSpineItemsCount());
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
  searcher = makeUniqueNoThrow<ChapterTextSearcher>(prepared, room);
  if (!searcher || !searcher->ok()) {
    LOG_ERR("BSR", "OOM: chapter %d searcher", spineIndex);
    closeChapter();
    return false;
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
  const int total = epub->getSpineItemsCount();
  if (nextSpine >= total || hits.size() + pendingHits.size() >= MAX_HITS) {
    finishSearch();
    return;
  }
  const unsigned long start = millis();
  if (!searcher) {
    if (!openChapter(nextSpine)) {
      ++nextSpine;
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
  closeChapter();
  ++nextSpine;
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

void EpubSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
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
