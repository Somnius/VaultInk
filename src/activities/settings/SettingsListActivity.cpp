#include "SettingsListActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "SettingsActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr int16_t ICON_SIZE = 32;
// Row icons, indexed by SettingsSection.
constexpr const freeink::Icon* SECTION_ICONS[SETTINGS_SECTION_COUNT] = {
    &icon_settings_32, &icon_smartphone_32, &icon_book_open_32, &icon_pointer_32, &icon_library_32, &icon_wifi_32,
};
// One-line contents of each section, indexed by SettingsSection.
constexpr StrId SECTION_DESCS[SETTINGS_SECTION_COUNT] = {
    StrId::STR_SETTINGS_SYSTEM_DESC,   StrId::STR_SETTINGS_DISPLAY_DESC, StrId::STR_SETTINGS_READER_DESC,
    StrId::STR_SETTINGS_CONTROLS_DESC, StrId::STR_SETTINGS_LIBRARY_DESC, StrId::STR_SETTINGS_NETWORK_DESC,
};
// Screen order (docs/design/settings.png): reading first, then the device. Each group's first row
// carries its heading.
constexpr SettingsSection ROW_ORDER[SETTINGS_SECTION_COUNT] = {
    SettingsSection::Reader,   SettingsSection::Library, SettingsSection::Display,
    SettingsSection::Controls, SettingsSection::Network, SettingsSection::General,
};
constexpr int DEVICE_GROUP_FIRST_ROW = 2;

int rowForSection(const SettingsSection section) {
  for (size_t i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
    if (ROW_ORDER[i] == section) return static_cast<int>(i);
  }
  return 0;
}
}  // namespace

SettingsListActivity::SettingsListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SettingsSection openSection)
    : UiListActivity("SettingsList", renderer, mappedInput), pendingSection(openSection) {}

void SettingsListActivity::onEnter() {
  UiListActivity::onEnter();
  rebuildRows();
}

void SettingsListActivity::onExit() {
  UiListActivity::onExit();
  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsListActivity::loop() {
  if (pendingSection != SettingsSection::End) {
    const SettingsSection section = pendingSection;
    pendingSection = SettingsSection::End;
    openSection(section);
    return;
  }
  UiListActivity::loop();
}

// Rebuilds row labels; call when entering and after a section returns (the
// UI language may have changed).
void SettingsListActivity::rebuildRows() {
  for (size_t i = 0; i < SETTINGS_SECTION_COUNT; ++i) {
    const auto section = static_cast<size_t>(ROW_ORDER[i]);
    auto& item = rowItems_[i];
    item.label = I18N.get(SETTINGS_SECTION_TITLES[section]);
    item.subtitle = I18N.get(SECTION_DESCS[section]);
    item.icon = fui::bitmapFromIcon(*SECTION_ICONS[section]);
    item.actionValue = static_cast<int16_t>(i);
    item.sectionHeading = i == 0                        ? tr(STR_SETTINGS_GROUP_READING)
                          : i == DEVICE_GROUP_FIRST_ROW ? tr(STR_SETTINGS_GROUP_DEVICE)
                                                        : nullptr;
  }
}

void SettingsListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  // Rows are as tall as a label plus a small second line, giving the icons air.
  const auto& theme = screen.theme();
  const int16_t textHeight = static_cast<int16_t>(screen.target().lineHeight(theme.bodyText.font) +
                                                  screen.target().lineHeight(theme.smallText.font));
  const int16_t paddingY = mappedInput.hasTouch() ? theme.listTouchRowPaddingY : theme.listRowPaddingY;
  props.rowHeight = static_cast<int16_t>(std::max<int16_t>(textHeight, ICON_SIZE) + 2 * paddingY);
  syncListViewport(screen, props);
  screen.list(props);
}

void SettingsListActivity::activateIndex(const int index) {
  // The section screen replaces this one; a lingering flash would gray an
  // unrelated element on return.
  app.clearTapFlash();
  openSection(ROW_ORDER[index]);
}

void SettingsListActivity::openSection(const SettingsSection section) {
  nav.selected = rowForSection(section);
  auto activity = makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput, section);
  if (!activity) {
    LOG_ERR("SETTINGS", "OOM: SettingsActivity");
    return;
  }
  startActivityForResult(std::move(activity), [this](const ActivityResult&) {
    rebuildRows();
    // A theme change inside the section must restyle this screen too.
    resetUi();
    requestUpdate();
  });
}

void SettingsListActivity::onBackButton() { onGoHome(); }

void SettingsListActivity::drawChrome() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Version rides in the header's trailing label slot: the footer position
  // conflicts with button hints on non-touch devices.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_SETTINGS_TITLE), CROSSPOINT_VERSION);
}
