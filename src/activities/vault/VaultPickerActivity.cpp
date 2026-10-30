#include "VaultPickerActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "FlashcardsActivity.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/VaultFiles.h"

namespace fui = freeink::ui;

VaultPickerActivity::VaultPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("VaultPicker", renderer, mappedInput) {}

const char* VaultPickerActivity::headerTitle() const { return tr(STR_VAULTS); }

void VaultPickerActivity::findVaults() {
  vaults.clear();
  auto found = vaultfiles::findVaults();
  std::transform(found.begin(), found.end(), std::back_inserter(vaults),
                 [](vaultfiles::Vault& v) { return Vault{std::move(v.name), std::move(v.path), {}, {}}; });
  for (auto& v : vaults) {
    v.last = vaultfiles::lastNote(v.path);
    if (v.last.empty()) {
      v.subtitle = v.path;
    } else {
      std::string title = v.last.substr(v.last.find_last_of('/') + 1);
      if (title.size() > 3 && title.compare(title.size() - 3, 3, ".md") == 0) title.resize(title.size() - 3);
      v.subtitle = std::string(tr(STR_VAULT_LAST_NOTE)) + ": " + title;
    }
  }
  LOG_DBG("VLT", "Found %u vaults", static_cast<unsigned>(vaults.size()));
}

void VaultPickerActivity::findRecentNotes() {
  recentNotes.clear();
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (recentNotes.size() >= MAX_RECENT_NOTES) break;
    const std::string& path = book.path;
    if (path.size() < 4 || path.compare(path.size() - 3, 3, ".md") != 0 || !Storage.exists(path.c_str())) continue;
    RecentNote note{book.title, path, {}};
    if (note.title.empty()) {
      note.title = path.substr(path.find_last_of('/') + 1);
      note.title.resize(note.title.size() - 3);
    }
    const auto inside = std::find_if(vaults.begin(), vaults.end(), [&path](const Vault& v) {
      return path.size() > v.path.size() && path.compare(0, v.path.size(), v.path) == 0 && path[v.path.size()] == '/';
    });
    if (inside != vaults.end()) note.vault = inside->name;
    if (note.vault.empty()) continue;  // only vault notes, not What's New or other .md files
    recentNotes.push_back(std::move(note));
  }
}

void VaultPickerActivity::onEnter() {
  UiListActivity::onEnter();
  findVaults();
  findRecentNotes();
  rowItems.assign(EXTRA_ROWS + vaults.size() + recentNotes.size(), fui::ListItem{});
  rowItems[0].label = tr(STR_FLASHCARDS);
  rowItems[0].icon = fui::bitmapFromIcon(icon_file_text_32);
  rowItems[0].actionValue = 0;
  for (size_t i = 0; i < vaults.size(); ++i) {
    auto& item = rowItems[EXTRA_ROWS + i];
    item.label = vaults[i].name.c_str();
    item.subtitle = vaults[i].subtitle.c_str();
    item.icon = fui::bitmapFromIcon(icon_vault_32);
    item.actionValue = static_cast<int16_t>(EXTRA_ROWS + i);
  }
  for (size_t i = 0; i < recentNotes.size(); ++i) {
    auto& item = rowItems[EXTRA_ROWS + vaults.size() + i];
    item.label = recentNotes[i].title.c_str();
    item.subtitle = recentNotes[i].vault.c_str();
    item.icon = fui::bitmapFromIcon(icon_file_text_32);
    item.actionValue = static_cast<int16_t>(EXTRA_ROWS + vaults.size() + i);
    if (i == 0) item.sectionHeading = tr(STR_RECENT_NOTES);
  }
  requestUpdate();
}

void VaultPickerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (vaults.empty()) {
    auto text = screen.theme().bodyText;
    text.align = fui::TextAlign::Center;
    text.maxLines = 6;
    const auto body = screen.body().inset(fui::Insets{40, 24, 0, 24});
    screen.target().text(body, tr(STR_NO_VAULTS_HELP), text);
    return;
  }
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}

void VaultPickerActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  if (index < EXTRA_ROWS) {
    if (auto cards = makeUniqueNoThrow<FlashcardsActivity>(renderer, mappedInput)) {
      startActivityForResult(std::move(cards), nullptr);
    }
    return;
  }
  const size_t row = static_cast<size_t>(index - EXTRA_ROWS);
  if (row >= vaults.size()) {
    activityManager.goToReader(recentNotes[row - vaults.size()].path);
    return;
  }
  // Open on the note read last (its folder, with the note selected), else the vault's top.
  const auto& v = vaults[row];
  activityManager.goToFileBrowser(v.last.empty() || !SETTINGS.vaultOpenLastNote ? v.path : v.last);
}

void VaultPickerActivity::onBackButton() { onGoHome(); }
