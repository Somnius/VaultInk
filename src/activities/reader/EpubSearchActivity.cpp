#include "EpubSearchActivity.h"

#include <Epub/Page.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WordSearch.h>

#include <algorithm>

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

void EpubSearchActivity::loop() {
  if (askPending) {
    askPending = false;
    askForQuery();
    return;
  }
  if (searching) {
    // Back (held or pressed) stops it; what was found stays.
    if (mappedInput.isPressed(MappedInputManager::Button::Back) || nextSpine >= epub->getSpineItemsCount() ||
        hits.size() >= MAX_HITS) {
      finishSearch();
      return;
    }
    searchChapter(nextSpine++);
    return;
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
  query = text;
  prepared = wordsearch::prepareQuery(query);
  hits.clear();
  nextSpine = 0;
  searching = !prepared.empty();
  rebuildRows();
  requestUpdate();
}

void EpubSearchActivity::finishSearch() {
  searching = false;
  LOG_DBG("BSR", "Search \"%s\": %u hits, stopped at chapter %d of %d", query.c_str(),
          static_cast<unsigned>(hits.size()), nextSpine, epub->getSpineItemsCount());
  rebuildRows();
  nav.selected = hits.empty() ? 0 : 1;
  requestUpdate();
}

void EpubSearchActivity::searchChapter(const int spineIndex) {
  const int total = epub->getSpineItemsCount();
  {
    RenderLock lock;
    const Rect popup = GUI.drawPopup(renderer, tr(STR_SEARCHING));
    GUI.fillPopupProgress(renderer, popup, total > 0 ? spineIndex * 100 / total : 0);
  }
  Section section(epub, spineIndex, renderer);
  bool ready = section.loadSectionFile(spec) && !section.isPartial();
  if (!ready) {
    // The chapter is laid out once and kept, so the reader finds it built.
    GfxRenderer::FrameBufferLoan loan(renderer);
    ready = section.createSectionFile(spec, nullptr);
    loan.end();
  }
  if (!ready) {
    LOG_ERR("BSR", "Chapter %d could not be laid out; skipped", spineIndex);
    return;
  }
  std::string where;
  const int toc = epub->getTocIndexForSpineIndex(spineIndex);
  if (toc >= 0) where = epub->getTocItem(toc).title;
  for (int p = 0; p < section.pageCount && hits.size() < MAX_HITS; ++p) {
    auto page = section.loadPage(p);
    if (!page) continue;
    std::vector<std::string> words;
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const TextBlock* block = static_cast<const PageLine*>(element.get())->getBlock();
      if (!block) continue;
      for (uint16_t w = 0; w < block->wordCount(); ++w) words.emplace_back(block->wordText(w));
    }
    for (const size_t at : wordsearch::find(words, prepared, std::min(MAX_HITS_PER_PAGE, MAX_HITS - hits.size()))) {
      Hit hit;
      hit.spine = spineIndex;
      hit.page = p;
      hit.totalPages = section.pageCount;
      hit.offset = page->visibleTextOffset;
      hit.label = wordsearch::snippet(words, at, 2, 5);
      hit.where = where;
      hit.pageText = std::string(tr(STR_PAGE_SHORT)) + " " + std::to_string(p + 1);
      hits.push_back(std::move(hit));
    }
  }
}

void EpubSearchActivity::rebuildRows() {
  queryLabel = std::string(tr(STR_SEARCH_IN_BOOK)) + ": " + query;
  rows.assign(hits.size() + 1, fui::ListItem{});
  rows[0].label = queryLabel.c_str();
  rows[0].subtitle = query.empty() ? nullptr : (hits.empty() ? tr(STR_NO_RESULTS) : tr(STR_SEARCH_AGAIN_HINT));
  rows[0].actionValue = 0;
  for (size_t i = 0; i < hits.size(); ++i) {
    rows[i + 1].label = hits[i].label.c_str();
    rows[i + 1].subtitle = hits[i].where.c_str();
    rows[i + 1].value = hits[i].pageText.c_str();
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
    askForQuery();
    return;
  }
  const int i = index - 1;
  if (i < 0 || i >= static_cast<int>(hits.size())) return;
  ProgressChangeResult result{};
  result.spineIndex = hits[i].spine;
  result.page = hits[i].page;
  result.totalPages = hits[i].totalPages;
  result.hasSavedProgress = true;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = hits[i].offset;
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
