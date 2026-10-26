#include "VaultSearchActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/VaultFiles.h"

namespace fui = freeink::ui;

VaultSearchActivity::VaultSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("VaultSearch", renderer, mappedInput) {}

const char* VaultSearchActivity::headerTitle() const { return tr(STR_SEARCH_NOTES); }

void VaultSearchActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
  requestUpdate();
}

void VaultSearchActivity::loop() {
  // The index update and the keyboard run from the loop, after the screen exists.
  if (!indexed) {
    indexed = true;
    updateIndexes();
  }
  if (askPending) {
    askPending = false;
    askForQuery();
    return;
  }
  UiListActivity::loop();
}

void VaultSearchActivity::updateIndexes() {
  const auto vaults = vaultfiles::findVaults();
  const auto fs = vaultfiles::sdFs();
  Rect popup{};
  bool shown = false;
  for (size_t v = 0; v < vaults.size(); ++v) {
    vaultindex::BuildStats stats;
    const bool ok = vaultindex::build(
        vaults[v].path, vaultfiles::indexPathFor(vaults[v].path), fs, &stats, [&](size_t done, size_t total) {
          delay(1);                // big vaults take a while; let the other tasks run
          if (total == 0) return;  // still checking which notes changed
          // Only notes that changed are read, so the popup shows only when there is work.
          if (!shown) {
            RenderLock lock;
            popup = GUI.drawPopup(renderer, tr(STR_UPDATING_SEARCH));
            shown = true;
          }
          if (done % 8 == 0 || done == total) {
            RenderLock lock;
            GUI.fillPopupProgress(renderer, popup, static_cast<int>((v * 100 + done * 100 / total) / vaults.size()));
          }
        });
    LOG_DBG("VLT", "Index %s: %s, %u notes, %u read", vaults[v].name.c_str(), ok ? "ok" : "FAILED",
            static_cast<unsigned>(stats.notes), static_cast<unsigned>(stats.rescanned));
  }
}

void VaultSearchActivity::askForQuery() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH_NOTES), query, 80);
  if (!keyboard) {
    LOG_ERR("VLT", "OOM: search keyboard");
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      if (query.empty()) finish();  // nothing searched yet: leave
      return;
    }
    runSearch(std::get<KeyboardResult>(result.data).text);
  });
}

void VaultSearchActivity::runSearch(const std::string& text) {
  query = text;
  results.clear();
  const auto fs = vaultfiles::sdFs();
  for (const auto& vault : vaultfiles::findVaults()) {
    for (auto& hit : vaultindex::search(vaultfiles::indexPathFor(vault.path), query, fs, MAX_RESULTS)) {
      std::string folder = hit.path.substr(vault.path.size());
      folder = folder.substr(0, folder.find_last_of('/'));
      if (!folder.empty() && folder.front() == '/') folder.erase(0, 1);
      std::string where = vault.name;
      if (!folder.empty()) where += " · " + folder;
      results.push_back({hit.score, std::move(hit.path), std::move(hit.title), std::move(where)});
    }
  }
  // Rank all vaults together, then keep the best.
  std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
    return a.score != b.score ? a.score > b.score : a.title < b.title;
  });
  if (results.size() > MAX_RESULTS) results.resize(MAX_RESULTS);
  LOG_DBG("VLT", "Search \"%s\": %u results", query.c_str(), static_cast<unsigned>(results.size()));
  rebuildRows();
  nav.selected = results.empty() ? 0 : 1;
  requestUpdate();
}

void VaultSearchActivity::rebuildRows() {
  queryLabel = std::string(tr(STR_SEARCH_NOTES)) + ": " + query;
  rows.assign(results.size() + 1, fui::ListItem{});
  rows[0].label = queryLabel.c_str();
  rows[0].subtitle = query.empty() ? nullptr : (results.empty() ? tr(STR_NO_RESULTS) : tr(STR_SEARCH_AGAIN_HINT));
  rows[0].actionValue = 0;
  for (size_t i = 0; i < results.size(); ++i) {
    rows[i + 1].label = results[i].title.c_str();
    rows[i + 1].subtitle = results[i].where.c_str();
    rows[i + 1].icon = fui::bitmapFromIcon(icon_file_text_32);
    rows[i + 1].actionValue = static_cast<int16_t>(i + 1);
  }
}

void VaultSearchActivity::buildScreen(UiScreen& screen) {
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

void VaultSearchActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (index == 0) {
    askForQuery();
    return;
  }
  const size_t i = static_cast<size_t>(index - 1);
  if (i < results.size()) activityManager.goToReader(results[i].path);
}

void VaultSearchActivity::onBackButton() { onGoHome(); }
