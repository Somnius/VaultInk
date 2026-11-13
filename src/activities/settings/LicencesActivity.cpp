#include "LicencesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

const char* LicencesActivity::headerTitle() const { return tr(STR_LICENCES); }

void LicencesActivity::onEnter() {
  detail_.store(-1);
  UiListActivity::onEnter();
  for (int i = 0; i < COUNT; i++) {
    rowItems_[i] = fui::ListItem{};
    rowItems_[i].label = thirdparty::COMPONENTS[i].name;
    rowItems_[i].subtitle = thirdparty::COMPONENTS[i].licence;
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

void LicencesActivity::activateIndex(const int index) {
  if (index < 0 || index >= COUNT) return;
  nav.selected = index;
  app.clearTapFlash();
  detail_.store(static_cast<int16_t>(index));
  requestUpdate();
}

// The detail view has no rows: it takes every input, and Back, Confirm or a tap closes it.
bool LicencesActivity::handleCustomInput() {
  if (detail_.load() < 0) return false;
  int x = 0, y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    detail_.store(-1);
    requestUpdate();
  }
  return true;
}

void LicencesActivity::drawFooter() {
  if (detail_.load() < 0) {
    UiListActivity::drawFooter();
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void LicencesActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto side = static_cast<int16_t>(metrics.contentSidePadding);
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int detail = detail_.load();
  if (detail >= 0 && detail < COUNT) {
    buildDetail(screen, thirdparty::COMPONENTS[detail]);
    return;
  }

  // VaultInk's own licence and why the binary is under the GPL, above the rows. A build with the SSH
  // library (wolfSSH, GPL-3.0-or-later: the X4 Pro builds) is under version 3 as a whole; the others
  // under version 2 or later.
#if defined(VAULTINK_SSH) && VAULTINK_SSH
  const char* introText = tr(STR_LICENCES_INTRO_SSH);
#else
  const char* introText = tr(STR_LICENCES_INTRO);
#endif
  auto intro = screen.theme().smallText;
  intro.maxLines = 6;
  const auto introWidth = static_cast<int16_t>(screen.body().width - 2 * side);
  const int16_t introHeight = fui::measureWrappedText(screen.target(), introText, intro, introWidth).height;
  screen.target().text(screen.takeTop(introHeight, screen.theme().spaceMd).inset(fui::Insets{0, side, 0, side}),
                       introText, intro);

  fui::ListProps props;
  props.items = rowItems_;
  props.count = COUNT;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

void LicencesActivity::buildDetail(UiScreen& screen, const thirdparty::Component& component) {
  const auto& theme = screen.theme();
  const auto side = static_cast<int16_t>(UITheme::getInstance().getMetrics().contentSidePadding);
  const auto width = static_cast<int16_t>(screen.body().width - 2 * side);

  const auto block = [&](const char* text, fui::TextStyle style, const uint8_t maxLines) {
    style.maxLines = maxLines;
    const int16_t height = fui::measureWrappedText(screen.target(), text, style, width).height;
    screen.target().text(screen.takeTop(height, theme.spaceMd).inset(fui::Insets{0, side, 0, side}), text, style);
  };

  auto title = theme.titleText;
  title.bold = true;
  block(component.name, title, 2);

  // "Label: value" lines. The longest value in the table (a copyright line) is under 120 bytes.
  char line[224];
  const auto field = [&](const char* label, const char* value, const uint8_t maxLines) {
    if (!value || !*value) return;
    snprintf(line, sizeof(line), "%s: %s", label, value);
    block(line, theme.smallText, maxLines);
  };
  field(tr(STR_LICENCE_FIELD), component.licence, 2);
  field(tr(STR_LICENCE_USED_FOR), component.usedFor, 3);
  field(tr(STR_LICENCE_SOURCE), component.source, 3);
  field(tr(STR_LICENCE_COPYRIGHT), component.copyright, 5);
}
