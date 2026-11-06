#include "LibraryFormatsActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
struct Format {
  uint16_t bit;
  const char* label;
};
constexpr Format FORMATS[] = {
    {library::FMT_EPUB, "EPUB"},
    {library::FMT_PDF, "PDF"},
    {library::FMT_TXT, "TXT"},
    {library::FMT_MD, "Markdown (MD)"},
    {library::FMT_XTC, "XTC / XTCH"},
    {library::FMT_CBZ, "CBZ"},
    {library::FMT_MOBI, "MOBI / AZW"},
    // Browse Files only (the last three rows after All and the seven book formats)
    {library::BROWSE_PICTURES, "Pictures"},
    {library::BROWSE_DRAWINGS, "Drawings (canvas, Excalidraw)"},
    {library::BROWSE_ZIP, "ZIP files"},
};
}  // namespace

uint16_t LibraryFormatsActivity::stored() const {
  return browser ? SETTINGS.browserFormatMask : SETTINGS.libraryFormatMask;
}

void LibraryFormatsActivity::store(const uint16_t value) const {
  if (browser) {
    SETTINGS.browserFormatMask = value;
  } else {
    SETTINGS.libraryFormatMask = static_cast<uint8_t>(value & library::FMT_ALL);
  }
}

void LibraryFormatsActivity::onEnter() {
  UiListActivity::onEnter();
  mask = stored();
}

void LibraryFormatsActivity::onExit() {
  if (mask != stored()) {
    store(mask);
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

const char* LibraryFormatsActivity::headerTitle() const {
  return browser ? tr(STR_FILE_TYPES) : tr(STR_LIBRARY_FORMATS);
}

void LibraryFormatsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  nav.selected = index;
  app.clearTapFlash();
  if (index == 0) {  // All: every format at once
    mask = allMask();
    requestUpdate();
    return;
  }
  const uint16_t next = mask ^ FORMATS[index - 1].bit;
  if ((next & library::FMT_ALL) != 0)
    mask = next;  // at least one book format stays on  // the list never goes empty by accident
  requestUpdate();
}

void LibraryFormatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  const int count = listCount();
  for (int i = 0; i < count; ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].label = i == 0 ? tr(STR_LIBRARY_SHOW_ALL) : FORMATS[i - 1].label;
    rowItems[i].toggle = true;
    rowItems[i].toggleChecked = i == 0 ? mask == allMask() : (mask & FORMATS[i - 1].bit) != 0;
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
