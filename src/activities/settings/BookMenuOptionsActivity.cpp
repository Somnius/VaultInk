#include "BookMenuOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "activities/reader/EpubReaderMenuActivity.h"
#include "activities/reader/TextPanelRows.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
using MA = EpubReaderMenuActivity::MenuAction;

struct Label {
  int id;
  StrId label;
};

// What the More panel can ever hold, in the order the reader shows it by default. A note has entries a book has not,
// and the other way round, and a few only exist with bookmarks, footnotes or clippings: all of them are listed, so a
// choice made now still applies the day the entry shows up. The note's list comes first, with the book's own entries
// slotted in after the entry that comes before them in a book.
std::vector<Label> moreEntries() {
  std::vector<EpubReaderMenuActivity::MenuItem> book;
  std::vector<EpubReaderMenuActivity::MenuItem> note;
  EpubReaderMenuActivity::buildMenuItems(book, true, true, true, false, false, true);
  EpubReaderMenuActivity::buildMenuItems(note, true, true, true, true, false);
  std::vector<EpubReaderMenuActivity::MenuItem> merged = note;
  const auto has = [&merged](const MA action) {
    return std::any_of(merged.begin(), merged.end(), [action](const auto& m) { return m.action == action; });
  };
  for (size_t i = 0; i < book.size(); ++i) {
    if (has(book[i].action)) continue;
    size_t at = 0;  // after the nearest earlier book entry that the merged list has
    for (size_t j = i; j > 0; --j) {
      const auto it =
          std::find_if(merged.begin(), merged.end(), [&](const auto& m) { return m.action == book[j - 1].action; });
      if (it != merged.end()) {
        at = static_cast<size_t>(it - merged.begin()) + 1;
        break;
      }
    }
    merged.insert(merged.begin() + static_cast<std::ptrdiff_t>(at), book[i]);
  }
  EpubReaderMenuActivity::arrangeMoreItems(merged);  // the same order and leaving-outs as the panel itself
  std::vector<Label> all;
  all.reserve(merged.size());
  std::transform(merged.begin(), merged.end(), std::back_inserter(all),
                 [](const auto& item) { return Label{EpubReaderMenuActivity::layoutId(item.action), item.labelId}; });
  return all;
}
}  // namespace

void BookMenuOptionsActivity::onEnter() {
  UiListActivity::onEnter();
  textPanel = false;
  moveMode = true;
  changed = false;
  loadPanel();
}

void BookMenuOptionsActivity::onExit() {
  storePanel();
  Activity::onExit();
}

const char* BookMenuOptionsActivity::headerTitle() const { return tr(STR_BOOK_MENU_OPTIONS); }

bool BookMenuOptionsActivity::locked(const int id) const {
  return !textPanel && (id == static_cast<int>(MA::BACK) || id == static_cast<int>(MA::GO_HOME));
}

const char* BookMenuOptionsActivity::entryLabel(const int id) const {
  for (const auto& [entryId, label] : labels) {
    if (entryId == id) return I18N.get(label);
  }
  return "";
}

void BookMenuOptionsActivity::loadPanel() {
  defaults.clear();
  labels.clear();
  if (textPanel) {
    for (int i = 0; i < textpanel::COUNT; ++i) {
      defaults.push_back(i);
      labels.emplace_back(i, textpanel::ROWS[i]);
    }
    order = menulayout::fullOrder(defaults, SETTINGS.textMenuOrder);
    hidden = SETTINGS.textMenuHidden;
  } else {
    for (const auto& entry : moreEntries()) {
      defaults.push_back(entry.id);
      labels.emplace_back(entry.id, entry.label);
    }
    order = menulayout::fullOrder(defaults, SETTINGS.moreMenuOrder);
    hidden = SETTINGS.moreMenuHidden;
  }
  if (order.size() > static_cast<size_t>(MAX_ENTRIES)) order.resize(MAX_ENTRIES);
  picked = -1;
}

void BookMenuOptionsActivity::storePanel() {
  if (!changed) return;
  // The default order is stored as nothing, so a reset leaves no trace and later versions keep their own order.
  const std::string encoded = order == defaults ? std::string() : menulayout::encode(order);
  if (textPanel) {
    strncpy(SETTINGS.textMenuOrder, encoded.c_str(), sizeof(SETTINGS.textMenuOrder) - 1);
    SETTINGS.textMenuOrder[sizeof(SETTINGS.textMenuOrder) - 1] = '\0';
    SETTINGS.textMenuHidden = hidden;
  } else {
    strncpy(SETTINGS.moreMenuOrder, encoded.c_str(), sizeof(SETTINGS.moreMenuOrder) - 1);
    SETTINGS.moreMenuOrder[sizeof(SETTINGS.moreMenuOrder) - 1] = '\0';
    SETTINGS.moreMenuHidden = hidden;
  }
  SETTINGS.saveToFile();
  changed = false;
}

void BookMenuOptionsActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  const int last = FIRST_ENTRY + static_cast<int>(order.size());
  if (index == ROW_PANEL) {
    storePanel();
    textPanel = !textPanel;
    loadPanel();
    nav.selected = ROW_PANEL;
  } else if (index == ROW_MODE) {
    moveMode = !moveMode;
    picked = -1;
  } else if (index == last) {  // reset this panel
    order = defaults;
    hidden = 0;
    picked = -1;
    changed = true;
  } else if (index > ROW_MODE && index < last) {
    const int position = index - FIRST_ENTRY;
    if (!moveMode) {
      const int id = order[static_cast<size_t>(position)];
      if (!locked(id)) {
        hidden ^= menulayout::maskOf(id);
        changed = true;
      }
    } else if (picked < 0) {
      picked = position;
    } else {
      menulayout::move(order, picked, position);  // tapping the picked entry again just puts it down
      picked = -1;
      changed = true;
    }
  }
  requestUpdate();
}

void BookMenuOptionsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const int count = listCount();
  for (int i = 0; i < count; ++i) rowItems[i] = fui::ListItem{};

  rowItems[ROW_PANEL].label = tr(STR_BOOK_MENU_PANEL);
  rowItems[ROW_PANEL].value = textPanel ? tr(STR_TOOL_TEXT) : tr(STR_TOOL_MORE);
  rowItems[ROW_MODE].label = tr(STR_BOOK_MENU_TAP_TO);
  rowItems[ROW_MODE].value = moveMode ? tr(STR_BOOK_MENU_MOVE) : tr(STR_BOOK_MENU_SHOW_HIDE);
  for (size_t p = 0; p < order.size(); ++p) {
    fui::ListItem& item = rowItems[FIRST_ENTRY + static_cast<int>(p)];
    const int id = order[p];
    item.label = entryLabel(id);
    item.toggle = true;
    item.toggleChecked = (hidden & menulayout::maskOf(id)) == 0 || locked(id);
    if (static_cast<int>(p) == picked) item.subtitle = tr(STR_HOME_ROWS_PICKED);
  }
  rowItems[count - 1].label = tr(STR_BOOK_MENU_RESET);
  for (int i = 0; i < count; ++i) rowItems[i].actionValue = static_cast<int16_t>(i);

  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
