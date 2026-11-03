#include "LibraryFormatsActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
struct Format {
  uint8_t bit;
  const char* label;
};
constexpr Format FORMATS[] = {
    {library::FMT_EPUB, "EPUB"},        {library::FMT_PDF, "PDF"},        {library::FMT_TXT, "TXT"},
    {library::FMT_MD, "Markdown (MD)"}, {library::FMT_XTC, "XTC / XTCH"}, {library::FMT_CBZ, "CBZ"},
    {library::FMT_MOBI, "MOBI / PRC"},
};
}  // namespace

void LibraryFormatsActivity::onEnter() {
  UiListActivity::onEnter();
  mask = SETTINGS.libraryFormatMask;
}

void LibraryFormatsActivity::onExit() {
  if (mask != SETTINGS.libraryFormatMask) {
    SETTINGS.libraryFormatMask = mask;
    SETTINGS.saveToFile();
  }
  Activity::onExit();
}

const char* LibraryFormatsActivity::headerTitle() const { return tr(STR_LIBRARY_FORMATS); }

void LibraryFormatsActivity::activateIndex(const int index) {
  if (index < 0 || index >= COUNT) return;
  nav.selected = index;
  app.clearTapFlash();
  const uint8_t next = mask ^ FORMATS[index].bit;
  if (next != 0) mask = next;  // the Library never goes empty by accident
  requestUpdate();
}

void LibraryFormatsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  for (int i = 0; i < COUNT; ++i) {
    rowItems[i] = fui::ListItem{};
    rowItems[i].label = FORMATS[i].label;
    rowItems[i].toggle = true;
    rowItems[i].toggleChecked = (mask & FORMATS[i].bit) != 0;
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  fui::ListProps props;
  props.items = rowItems;
  props.count = COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
