#include "NetworkModeSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "network/SshDevice.h"

namespace fui = freeink::ui;

namespace {
#if FREEINK_CAP_USB_MSC
constexpr bool HAS_USB_DRIVE = true;
#else
constexpr bool HAS_USB_DRIVE = false;
#endif

struct RowText {
  StrId label;
  StrId desc;
  UIIcon icon;
};

RowText rowText(const NetworkMode mode) {
  switch (mode) {
    case NetworkMode::CONNECT_CALIBRE:
      return {StrId::STR_CALIBRE_WIRELESS, StrId::STR_CALIBRE_DESC, UIIcon::Library};
    case NetworkMode::CREATE_HOTSPOT:
      return {StrId::STR_CREATE_HOTSPOT, StrId::STR_HOTSPOT_DESC, UIIcon::Hotspot};
    case NetworkMode::USB_DRIVE:
      return {StrId::STR_USB_DRIVE, StrId::STR_USB_DRIVE_DESC, UIIcon::Usb};
    case NetworkMode::REMOTE_SERVERS:
      return {StrId::STR_TRANSFER_REMOTE_SERVERS, StrId::STR_TRANSFER_REMOTE_DESC, UIIcon::Folder};
    case NetworkMode::SSH_TERMINAL:
      return {StrId::STR_TRANSFER_SSH_TERMINAL, StrId::STR_TRANSFER_SSH_DESC, UIIcon::Text};
    case NetworkMode::JOIN_NETWORK:
    default:
      return {StrId::STR_JOIN_NETWORK, StrId::STR_JOIN_DESC, UIIcon::Wifi};
  }
}
}  // namespace

NetworkModeSelectionActivity::NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           const NetworkMode selected)
    : UiListActivity("NetworkModeSelection", renderer, mappedInput),
      rows_(networkmodes::buildRows(HAS_USB_DRIVE, sshdevice::available())) {
  // Entirely static, so built once here rather than every buildScreen() call.
  for (int i = 0; i < rows_.count; i++) {
    const RowText text = rowText(rows_.modes[static_cast<size_t>(i)]);
    fui::ListItem item;
    item.label = I18N.get(text.label);
    item.subtitle = I18N.get(text.desc);
    item.icon = listIconFor(text.icon, 32);      // subtitle rows carry the larger icon
    item.actionValue = static_cast<int16_t>(i);  // the row index, as UiListActivity expects
    rowItems_[i] = item;
  }
  startRow_ = networkmodes::startRow(rows_, selected);
}

void NetworkModeSelectionActivity::onEnter() {
  UiListActivity::onEnter();  // puts the cursor on the first row
  nav.selected = startRow_;
}

int NetworkModeSelectionActivity::listCount() const { return rows_.count; }

const char* NetworkModeSelectionActivity::headerTitle() const { return tr(STR_FILE_TRANSFER); }

void NetworkModeSelectionActivity::activateIndex(const int index) {
  // Selection leaves this screen; a lingering flash would gray an unrelated
  // element on the next render.
  app.clearTapFlash();
  if (index < 0 || index >= rows_.count) return;
  nav.selected = index;

  onModeSelected(rows_.modes[static_cast<size_t>(index)]);
}

void NetworkModeSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems_ was built once in the constructor and is reused here on every
  // repaint.
  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(rows_.count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void NetworkModeSelectionActivity::onModeSelected(NetworkMode mode) {
  setResult(NetworkModeResult{mode});
  finish();
}

void NetworkModeSelectionActivity::onCancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
