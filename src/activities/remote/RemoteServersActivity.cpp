#include "RemoteServersActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

#include "MappedInputManager.h"
#include "RemoteBrowserActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
const char* protocolName(const RemoteServer::Protocol protocol) {
  return protocol == RemoteServer::Protocol::Ftp ? "FTP" : "WebDAV";
}

// Content below the GUI.drawHeader band, above the button hints (as the OPDS server pages).
void setListMargins(UiAppHost::UiScreen& screen, const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(
      fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                  static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                  static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height) + metrics.buttonHintsHeight),
                  static_cast<int16_t>(safe.x)});
}
}  // namespace

// ---- the list ----

RemoteServerListActivity::RemoteServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("RemoteServerList", renderer, mappedInput) {}

void RemoteServerListActivity::onEnter() {
  UiListActivity::onEnter();
  REMOTE_STORE.loadFromFile();
  nav.selected = 0;
  rebuildRowItems();
}

void RemoteServerListActivity::rebuildRowItems() {
  RenderLock lock(*this);
  const auto& servers = REMOTE_STORE.getServers();
  rowItems_.clear();
  rowSubtitles_.clear();
  rowItems_.reserve(servers.size() + 1);
  rowSubtitles_.reserve(servers.size());
  for (size_t i = 0; i < servers.size(); i++) {
    // "FTP, not encrypted · host/path": the warning is on the list too, not only before connecting.
    std::string subtitle = protocolName(servers[i].protocol);
    if (servers[i].isPlain()) {
      subtitle += ", ";
      subtitle += tr(STR_REMOTE_NOT_ENCRYPTED);
    }
    if (!servers[i].name.empty() && !servers[i].url.empty()) subtitle += " \xC2\xB7 " + servers[i].url;
    rowSubtitles_.push_back(std::move(subtitle));
  }
  for (size_t i = 0; i < servers.size(); i++) {
    fui::ListItem item;
    item.label = servers[i].name.empty() ? (servers[i].url.empty() ? tr(STR_NOT_SET) : servers[i].url.c_str())
                                         : servers[i].name.c_str();
    item.subtitle = rowSubtitles_[i].c_str();
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
  if (servers.size() < RemoteServerStore::MAX_SERVERS) {
    fui::ListItem add;
    add.label = tr(STR_ADD_SERVER);
    add.actionValue = static_cast<int16_t>(servers.size());
    rowItems_.push_back(add);
  }
}

const char* RemoteServerListActivity::headerTitle() const { return tr(STR_REMOTE_SERVERS); }

void RemoteServerListActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
  const auto serverCount = static_cast<int>(REMOTE_STORE.getCount());
  auto page = makeUniqueNoThrow<RemoteServerActivity>(renderer, mappedInput, index < serverCount ? index : -1);
  if (!page) {
    LOG_ERR("RMT", "OOM: server page");
    return;
  }
  startActivityForResult(std::move(page), [this](const ActivityResult&) {
    REMOTE_STORE.loadFromFile();
    nav.selected = 0;
    rebuildRowItems();
  });
}

void RemoteServerListActivity::buildScreen(UiScreen& screen) {
  setListMargins(screen, renderer);
  screen.spacer(static_cast<int16_t>(UITheme::getInstance().getMetrics().verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}

// ---- one server ----

RemoteServerActivity::RemoteServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const int serverIndex)
    : UiListActivity("RemoteServer", renderer, mappedInput), serverIndex(serverIndex) {}

void RemoteServerActivity::onEnter() {
  UiListActivity::onEnter();
  isNewServer = serverIndex < 0;
  showSaveError = false;
  if (!isNewServer) {
    if (const auto* server = REMOTE_STORE.getServer(static_cast<size_t>(serverIndex))) {
      editServer = *server;
    } else {
      isNewServer = true;
      serverIndex = -1;
    }
  }
  layoutRows();
}

void RemoteServerActivity::layoutRows() {
  RenderLock lock(*this);
  int n = 0;
  if (!isNewServer && !editServer.url.empty()) rows_[n++] = Row::Connect;
  rows_[n++] = Row::Name;
  rows_[n++] = Row::Protocol;
  rows_[n++] = Row::Address;
  rows_[n++] = Row::Username;
  rows_[n++] = Row::Password;
  // FTP has no certificate to check.
  if (editServer.protocol == RemoteServer::Protocol::WebDav) rows_[n++] = Row::CheckCert;
  if (!isNewServer) rows_[n++] = Row::Delete;
  rowCount_ = n;
  if (nav.selected >= n) nav.selected = n - 1;
}

bool RemoteServerActivity::saveServer() {
  bool success = false;
  if (isNewServer) {
    success = REMOTE_STORE.addServer(editServer);
    if (success) {
      // Later edits update this entry instead of adding another.
      isNewServer = false;
      serverIndex = static_cast<int>(REMOTE_STORE.getCount()) - 1;
    }
  } else {
    success = REMOTE_STORE.updateServer(static_cast<size_t>(serverIndex), editServer);
  }
  if (!success) LOG_ERR("RMT", "Failed to save the server");
  showSaveError = !success;
  layoutRows();
  requestUpdate();
  return success;
}

void RemoteServerActivity::editText(const Row row) {
  std::string* field = nullptr;
  std::string prefill;
  const char* label = "";
  InputType type = InputType::Text;
  switch (row) {
    case Row::Name:
      field = &editServer.name;
      label = tr(STR_SERVER_NAME);
      prefill = editServer.name;
      break;
    case Row::Address:
      field = &editServer.url;
      label = tr(STR_REMOTE_ADDRESS);
      prefill =
          editServer.url.empty() && editServer.protocol == RemoteServer::Protocol::WebDav ? "https://" : editServer.url;
      type = InputType::Url;
      break;
    case Row::Username:
      field = &editServer.username;
      label = tr(STR_USERNAME);
      prefill = editServer.username;
      break;
    case Row::Password:
      // Never shown again once saved: the keyboard starts empty.
      field = &editServer.password;
      label = tr(STR_PASSWORD);
      type = InputType::Password;
      break;
    default:
      return;
  }
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, label, prefill,
                                                           RemoteServerStore::MAX_FIELD_BYTES, type);
  if (!keyboard) {
    LOG_ERR("RMT", "OOM: keyboard");
    return;
  }
  const bool isAddress = row == Row::Address;
  startActivityForResult(std::move(keyboard), [this, field, isAddress](const ActivityResult& result) {
    if (result.isCancelled) return;
    std::string text = std::get<KeyboardResult>(result.data).text;
    if (isAddress && (text == "https://" || text == "http://")) text.clear();
    *field = std::move(text);
    saveServer();
  });
}

void RemoteServerActivity::activateIndex(const int index) {
  if (index < 0 || index >= rowCount_) return;
  nav.selected = index;
  app.clearTapFlash();
  switch (rows_[index]) {
    case Row::Connect: {
      // The browser asks about a connection that is not encrypted before it switches Wi-Fi on.
      auto browser = makeUniqueNoThrow<RemoteBrowserActivity>(renderer, mappedInput, editServer);
      if (!browser) {
        LOG_ERR("RMT", "OOM: browser");
        return;
      }
      startActivityForResult(std::move(browser), [](const ActivityResult&) {});
      break;
    }
    case Row::Protocol:
      editServer.protocol = editServer.protocol == RemoteServer::Protocol::WebDav ? RemoteServer::Protocol::Ftp
                                                                                  : RemoteServer::Protocol::WebDav;
      saveServer();
      break;
    case Row::CheckCert:
      editServer.verifyCert = !editServer.verifyCert;
      saveServer();
      break;
    case Row::Delete:
      if (!REMOTE_STORE.removeServer(static_cast<size_t>(serverIndex))) {
        showSaveError = true;
        requestUpdate();
        return;
      }
      finish();
      break;
    default:
      editText(rows_[index]);
      break;
  }
}

void RemoteServerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  setListMargins(screen, renderer);

  // Two lines under the header: how to write the address, or the warning for a plain connection.
  fui::TextStyle hint = screen.theme().smallText;
  hint.maxLines = 2;
  const int16_t lineHeight = screen.target().lineHeight(hint.font);
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(lineHeight * 2));
  const int16_t pad = screen.theme().headerSidePadding;
  const bool plain = !editServer.url.empty() && editServer.isPlain();
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}),
                       plain ? tr(STR_REMOTE_PLAIN_HINT) : tr(STR_REMOTE_ADDRESS_HINT), hint);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (int i = 0; i < rowCount_; i++) {
    fui::ListItem item;
    item.actionValue = static_cast<int16_t>(i);
    switch (rows_[i]) {
      case Row::Connect:
        item.label = tr(STR_REMOTE_BROWSE);
        item.value = ">";
        break;
      case Row::Name:
        item.label = tr(STR_SERVER_NAME);
        item.value = editServer.name.empty() ? tr(STR_NOT_SET) : editServer.name.c_str();
        break;
      case Row::Protocol:
        item.label = tr(STR_REMOTE_PROTOCOL);
        item.value = protocolName(editServer.protocol);
        break;
      case Row::Address:
        item.label = tr(STR_REMOTE_ADDRESS);
        item.value = editServer.url.empty() ? tr(STR_NOT_SET) : editServer.url.c_str();
        break;
      case Row::Username:
        item.label = tr(STR_USERNAME);
        item.value = editServer.username.empty() ? tr(STR_NOT_SET) : editServer.username.c_str();
        break;
      case Row::Password:
        item.label = tr(STR_PASSWORD);
        item.value = editServer.password.empty() ? tr(STR_NOT_SET) : "******";
        break;
      case Row::CheckCert:
        item.label = tr(STR_LIVESYNC_CERT_CHECK);
        GUI.setCheckboxRow(item, editServer.verifyCert);
        break;
      case Row::Delete:
        item.label = tr(STR_DELETE_SERVER);
        break;
    }
    rowItems_[i] = item;
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(rowCount_);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

const char* RemoteServerActivity::headerTitle() const {
  if (isNewServer) return tr(STR_ADD_SERVER);
  return editServer.name.empty() ? tr(STR_REMOTE_SERVERS) : editServer.name.c_str();
}

void RemoteServerActivity::drawFooter() {
  UiListActivity::drawFooter();
  if (showSaveError) GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
}
