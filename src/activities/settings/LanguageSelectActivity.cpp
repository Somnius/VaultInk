#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <iterator>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

LanguageSelectActivity::LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("LanguageSelect", renderer, mappedInput) {}

namespace {
// VaultInk's two main languages lead the list; the rest follow in the usual order.
std::array<uint8_t, sizeof(SORTED_LANGUAGE_INDICES)> listOrder() {
  std::array<uint8_t, sizeof(SORTED_LANGUAGE_INDICES)> order{};
  size_t n = 0;
  order[n++] = static_cast<uint8_t>(Language::EN);
  order[n++] = static_cast<uint8_t>(Language::EL);
  for (const uint8_t lang : SORTED_LANGUAGE_INDICES) {
    if (lang != order[0] && lang != order[1]) order[n++] = lang;
  }
  return order;
}
const auto LIST_ORDER = listOrder();
}  // namespace

void LanguageSelectActivity::onEnter() {
  UiListActivity::onEnter();

  // Open on the current language, which may sit past the first page; the first
  // screen build pulls the viewport to it (ListNav follow-on-build).
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  const auto* begin = LIST_ORDER.data();
  const auto* end = LIST_ORDER.data() + LIST_ORDER.size();
  const auto* it = std::find(begin, end, currentLang);
  nav.selected = (it != end) ? static_cast<int>(std::distance(begin, it)) : 0;

  // Built once here rather than every buildScreen() call: labels are static,
  // and the "Selected" marker can't go stale mid-visit since activateIndex()
  // finishes the activity immediately on selection.
  for (int i = 0; i < totalItems; ++i) {
    fui::ListItem item;
    item.label = I18N.getLanguageName(static_cast<Language>(LIST_ORDER[i]));
    if (LIST_ORDER[i] == currentLang) {
      item.value = tr(STR_SELECTED);
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
}

const char* LanguageSelectActivity::headerTitle() const { return tr(STR_LANGUAGE); }

void LanguageSelectActivity::activateIndex(const int index) {
  // The activated row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  const uint8_t langIndex = LIST_ORDER[index];

  {
    RenderLock lock(*this);
    I18N.setLanguage(static_cast<Language>(langIndex));
  }

  SETTINGS.language = langIndex;
  SETTINGS.saveToFile();

  // Return to previous page
  finish();
}

void LanguageSelectActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems was built once in onEnter() and is reused here on every repaint.
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(totalItems);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
