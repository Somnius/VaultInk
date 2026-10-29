#include "SettingsSearchActivity.h"

#include <Logging.h>
#include <Memory.h>
#include <TextFold.h>

#include "SettingsActivity.h"
#include "SettingsList.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/vault/VaultSearchActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

void SettingsSearchActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
  requestUpdate();
}

const char* SettingsSearchActivity::headerTitle() const { return tr(STR_SEARCH_SETTINGS); }

void SettingsSearchActivity::loop() {
  // The keyboard opens from the loop, after this screen exists.
  if (askPending) {
    askPending = false;
    askForQuery();
    return;
  }
  UiListActivity::loop();
}

void SettingsSearchActivity::askForQuery() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH_SETTINGS), query, 40);
  if (!keyboard) {
    LOG_ERR("SET", "OOM: search keyboard");
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

void SettingsSearchActivity::runSearch(const std::string& text) {
  query = text;
  results.clear();
  const std::string wanted = textfold::fold(query);
  const auto matches = [&](const StrId name) {
    return !wanted.empty() && textfold::fold(I18N.get(name)).find(wanted) != std::string::npos;
  };
  // Each section's rows as its screen shows them, so a result is always a row that exists there.
  const auto all = SettingsActivity::allSettings();  // once per search, not once per section
  std::vector<SettingInfo> list;
  for (size_t s = 0; s < SETTINGS_SECTION_COUNT && results.size() < MAX_RESULTS; ++s) {
    const auto section = static_cast<SettingsSection>(s);
    SettingsActivity::buildList(section, list, &all);
    for (const auto& setting : list) {
      if (results.size() >= MAX_RESULTS) break;
      if (matches(setting.nameId)) results.push_back({setting.nameId, section, false});
    }
  }
  // Text Settings rows are not in any section list.
  for (const auto& setting : all) {
    if (results.size() >= MAX_RESULTS) break;
    if (SettingsActivity::isTextSetting(setting) && matches(setting.nameId)) {
      results.push_back({setting.nameId, SettingsSection::Reader, true});
    }
  }
  LOG_DBG("SET", "Search \"%s\": %u settings", query.c_str(), static_cast<unsigned>(results.size()));
  rebuildRows();
  nav.selected = results.empty() ? 0 : 1;
  nav.top = 0;
  requestUpdate();
}

void SettingsSearchActivity::rebuildRows() {
  rows.clear();
  subtitles.clear();
  rows.reserve(results.size() + 2);
  subtitles.reserve(results.size());

  // Row 0: the words searched; tap to search again.
  queryLabel = std::string(tr(STR_SEARCH_SETTINGS)) + (query.empty() ? "" : ": " + query);
  fui::ListItem head;
  head.label = queryLabel.c_str();
  head.subtitle = query.empty() ? nullptr : (results.empty() ? tr(STR_NO_RESULTS) : tr(STR_SEARCH_AGAIN_HINT));
  head.actionValue = 0;
  rows.push_back(head);

  for (const auto& result : results) {
    std::string where = I18N.get(SETTINGS_SECTION_TITLES[static_cast<size_t>(result.section)]);
    if (result.textSetting) where += std::string(" · ") + tr(STR_TEXT_SETTINGS);
    subtitles.push_back(std::move(where));
  }
  for (size_t i = 0; i < results.size(); ++i) {
    fui::ListItem item;
    item.label = I18N.get(results[i].name);
    item.subtitle = subtitles[i].c_str();
    item.actionValue = static_cast<int16_t>(rows.size());
    rows.push_back(item);
  }

  // Last row: the same words in the notes.
  if (!query.empty()) {
    notesLabel = std::string(tr(STR_SEARCH_NOTES_FOR)) + " “" + query + "”";
    fui::ListItem notes;
    notes.label = notesLabel.c_str();
    notes.actionValue = static_cast<int16_t>(rows.size());
    rows.push_back(notes);
  }
}

void SettingsSearchActivity::activateIndex(const int index) {
  app.clearTapFlash();
  nav.selected = index;
  if (index == 0) {
    askForQuery();
    return;
  }
  const int resultIndex = index - 1;
  if (resultIndex >= static_cast<int>(results.size())) {
    // The notes row: search every vault for the same words; Back comes here again.
    if (auto search = makeUniqueNoThrow<VaultSearchActivity>(renderer, mappedInput, std::string{}, query)) {
      startActivityForResult(std::move(search), [this](const ActivityResult&) { requestUpdate(); });
    }
    return;
  }
  const Result& result = results[static_cast<size_t>(resultIndex)];
  const StrId focus = result.textSetting ? StrId::STR_TEXT_SETTINGS : result.name;
  if (auto activity = makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, result.section, focus)) {
    startActivityForResult(std::move(activity), [this](const ActivityResult&) {
      rebuildRows();  // the UI language may have changed in there
      requestUpdate();
    });
  }
}

void SettingsSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}
