#include "VaultPickerActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <iterator>

#include "MappedInputManager.h"
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
                 [](vaultfiles::Vault& v) { return Vault{std::move(v.name), std::move(v.path)}; });
  LOG_DBG("VLT", "Found %u vaults", static_cast<unsigned>(vaults.size()));
}

void VaultPickerActivity::onEnter() {
  UiListActivity::onEnter();
  findVaults();
  rowItems.assign(vaults.size(), fui::ListItem{});
  for (size_t i = 0; i < vaults.size(); ++i) {
    rowItems[i].label = vaults[i].name.c_str();
    rowItems[i].subtitle = vaults[i].path.c_str();
    rowItems[i].icon = fui::bitmapFromIcon(icon_vault_32);
    rowItems[i].actionValue = static_cast<int16_t>(i);
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
  if (index < 0 || index >= static_cast<int>(vaults.size())) return;
  app.clearTapFlash();
  activityManager.goToFileBrowser(vaults[static_cast<size_t>(index)].path);
}

void VaultPickerActivity::onBackButton() { onGoHome(); }
