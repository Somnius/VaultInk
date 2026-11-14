#include "RemoteServersActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SshAddress.h>
#include <SshTermFont.h>
#include <SshTerminal.h>

#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "RemoteBrowserActivity.h"
#include "SshKeyActivity.h"
#include "SshTerminalActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "network/SshDevice.h"

namespace fui = freeink::ui;

namespace {
const char* protocolName(const RemoteServer::Protocol protocol) {
  switch (protocol) {
    case RemoteServer::Protocol::Ftp:
      return "FTP";
    case RemoteServer::Protocol::Sftp:
      return "SSH / SFTP";
    case RemoteServer::Protocol::WebDav:
    default:
      return "WebDAV";
  }
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

RemoteServerListActivity::RemoteServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   const bool sshOnly, const RemoteOrigin origin)
    : UiListActivity("RemoteServerList", renderer, mappedInput), sshOnly_(sshOnly), origin_(origin) {}

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
  rowServer_.clear();
  rowItems_.reserve(servers.size() + 3);
  rowSubtitles_.reserve(servers.size());
  rowServer_.reserve(servers.size());
  for (size_t i = 0; i < servers.size(); i++) {
    if (sshOnly_ && !servers[i].isSsh()) continue;
    rowServer_.push_back(static_cast<int16_t>(i));
    // "FTP, not encrypted · host/path": the warning is on the list too, not only before connecting.
    std::string subtitle = protocolName(servers[i].protocol);
    if (servers[i].isPlain()) {
      subtitle += ", ";
      subtitle += tr(STR_REMOTE_NOT_ENCRYPTED);
    }
    if (!servers[i].name.empty() && !servers[i].url.empty()) subtitle += " \xC2\xB7 " + servers[i].url;
    rowSubtitles_.push_back(std::move(subtitle));
  }
  for (size_t row = 0; row < rowServer_.size(); row++) {
    const RemoteServer& server = servers[static_cast<size_t>(rowServer_[row])];
    fui::ListItem item;
    item.label =
        server.name.empty() ? (server.url.empty() ? tr(STR_NOT_SET) : server.url.c_str()) : server.name.c_str();
    item.subtitle = rowSubtitles_[row].c_str();
    item.actionValue = static_cast<int16_t>(row);
    rowItems_.push_back(item);
  }
  if (servers.size() < RemoteServerStore::MAX_SERVERS) {
    fui::ListItem add;
    add.label = tr(STR_ADD_SERVER);
    add.actionValue = static_cast<int16_t>(rowServer_.size());
    rowItems_.push_back(add);
  }
  // The SSH list: the terminal's text size. A tap goes to the next size; the value says the cell
  // and how many columns it gives on this screen. Read by the terminal when it opens. Only in
  // the builds that have the terminal, so the font is not linked into the others.
  textSizeRow_ = -1;
#if VAULTINK_SSH_TERMINAL
  if (sshOnly_) {
    const sshfont::Font& font = sshfont::fontForHeight(SETTINGS.sshTextSize);
    const sshfont::Grid grid = sshfont::gridFor(renderer.getScreenWidth(), renderer.getScreenHeight(), font,
                                                sshterm::Terminal::MAX_COLS, sshterm::Terminal::MAX_ROWS);
    textSizeValue_ = std::to_string(font.cellW) + " x " + std::to_string(font.cellH) + ", " +
                     std::to_string(grid.cols) + " " + tr(STR_SSH_COLUMNS);
    fui::ListItem size;
    size.label = tr(STR_SSH_TEXT_SIZE);
    size.value = textSizeValue_.c_str();
    textSizeRow_ = static_cast<int>(rowItems_.size());
    size.actionValue = static_cast<int16_t>(textSizeRow_);
    rowItems_.push_back(size);
  }
#endif
  // The SSH list: the reset for the pinned server keys, when there are any (also the way out when
  // the file is unreadable or was changed outside the device).
  forgetAllRow_ = -1;
  if (sshOnly_ && sshdevice::hasKnownHosts()) {
    fui::ListItem forget;
    forget.label = tr(STR_SSH_FORGET_ALL);
    forgetAllRow_ = static_cast<int>(rowItems_.size());
    forget.actionValue = static_cast<int16_t>(forgetAllRow_);
    rowItems_.push_back(forget);
  }
}

const char* RemoteServerListActivity::headerTitle() const {
  return sshOnly_ ? tr(STR_SSH_SERVERS) : tr(STR_REMOTE_SERVERS);
}

void RemoteServerListActivity::activateIndex(const int index) {
  nav.selected = index;
  app.clearTapFlash();
#if VAULTINK_SSH_TERMINAL
  if (index == textSizeRow_ && textSizeRow_ >= 0) {
    SETTINGS.sshTextSize = sshfont::nextHeight(SETTINGS.sshTextSize);
    SETTINGS.saveToFile();
    rebuildRowItems();
    nav.selected = index;
    requestUpdate();
    return;
  }
#endif
  if (index == forgetAllRow_ && forgetAllRow_ >= 0) {
    auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_SSH_FORGET_ALL),
                                                          tr(STR_SSH_FORGET_ALL_BODY));
    if (!dialog) return;
    // Cancel is the preselected answer.
    startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
      if (!result.isCancelled) sshdevice::forgetAllHosts();
      nav.selected = 0;
      rebuildRowItems();
    });
    return;
  }
  const auto serverRows = static_cast<int>(rowServer_.size());
  const int storeIndex = index >= 0 && index < serverRows ? rowServer_[static_cast<size_t>(index)] : -1;
  auto page = makeUniqueNoThrow<RemoteServerActivity>(renderer, mappedInput, storeIndex, sshOnly_, origin_);
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
                                           const int serverIndex, const bool newAsSsh, const RemoteOrigin origin)
    : UiListActivity("RemoteServer", renderer, mappedInput),
      serverIndex(serverIndex),
      newAsSsh(newAsSsh),
      origin(origin) {}

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
  if (isNewServer && newAsSsh && sshdevice::available()) editServer.protocol = RemoteServer::Protocol::Sftp;
  layoutRows();
}

void RemoteServerActivity::layoutRows() {
  RenderLock lock(*this);
  int n = 0;
  const bool ssh = editServer.isSsh();
  if (!isNewServer && !editServer.url.empty()) {
    if (ssh) rows_[n++] = Row::Terminal;
    rows_[n++] = Row::Connect;
  }
  rows_[n++] = Row::Name;
  rows_[n++] = Row::Protocol;
  rows_[n++] = Row::Address;
  rows_[n++] = Row::Username;
  // An SSH password is typed at every connection and never saved: there is no row to save one.
  if (!ssh) rows_[n++] = Row::Password;
  // FTP has no certificate to check.
  if (editServer.protocol == RemoteServer::Protocol::WebDav) rows_[n++] = Row::CheckCert;
  if (ssh) {
    rows_[n++] = Row::SignIn;
    if (editServer.keyAuth) rows_[n++] = Row::PublicKey;
    if (!isNewServer && !editServer.url.empty()) rows_[n++] = Row::ForgetHost;
  }
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
    case Row::Terminal:
      openTerminal();
      break;
    case Row::Protocol:
      // WebDAV -> FTP -> SSH (where the build has it) -> WebDAV.
      if (editServer.protocol == RemoteServer::Protocol::WebDav) {
        editServer.protocol = RemoteServer::Protocol::Ftp;
      } else if (editServer.protocol == RemoteServer::Protocol::Ftp && sshdevice::available()) {
        editServer.protocol = RemoteServer::Protocol::Sftp;
      } else {
        editServer.protocol = RemoteServer::Protocol::WebDav;
      }
      // A password typed for one protocol is not sent with another, and SSH saves none.
      editServer.password.clear();
      if (!editServer.isSsh()) editServer.keyAuth = false;
      saveServer();
      break;
    case Row::SignIn:
      toggleSignIn();
      break;
    case Row::PublicKey:
      showPublicKey();
      break;
    case Row::ForgetHost:
      forgetHostKey();
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
  const char* hintText = plain ? tr(STR_REMOTE_PLAIN_HINT) : tr(STR_REMOTE_ADDRESS_HINT);
  if (editServer.isSsh()) hintText = tr(STR_SSH_ADDRESS_HINT);
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), hintText, hint);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  for (int i = 0; i < rowCount_; i++) {
    fui::ListItem item;
    item.actionValue = static_cast<int16_t>(i);
    switch (rows_[i]) {
      case Row::Connect:
        item.label = editServer.isSsh() ? tr(STR_SSH_BROWSE) : tr(STR_REMOTE_BROWSE);
        item.value = ">";
        break;
      case Row::Terminal:
        item.label = tr(STR_SSH_TERMINAL);
        item.value = ">";
        break;
      case Row::SignIn:
        item.label = tr(STR_SSH_SIGN_IN);
        item.value = editServer.keyAuth ? tr(STR_SSH_SIGN_IN_KEY) : tr(STR_SSH_SIGN_IN_PASSWORD);
        break;
      case Row::PublicKey:
        item.label = tr(STR_SSH_PUBLIC_KEY);
        item.value = ">";
        break;
      case Row::ForgetHost:
        item.label = tr(STR_SSH_FORGET_HOST);
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

void RemoteServerActivity::toggleSignIn() {
  if (editServer.keyAuth) {
    editServer.keyAuth = false;
    saveServer();
    return;
  }
  // The first use of the device key makes it (on the device, with its own random generator).
  if (!sshdevice::hasKey()) {
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    if (!sshdevice::createKey()) {
      showSaveError = true;
      requestUpdate();
      return;
    }
  }
  editServer.keyAuth = true;
  if (saveServer()) showPublicKey();
}

void RemoteServerActivity::showPublicKey() {
  auto page = makeUniqueNoThrow<SshKeyActivity>(renderer, mappedInput);
  if (!page) {
    LOG_ERR("RMT", "OOM: key page");
    return;
  }
  startActivityForResult(std::move(page), [](const ActivityResult&) {});
}

void RemoteServerActivity::forgetHostKey() {
  sshaddr::Address address;
  if (!sshaddr::parse(editServer.url, address)) return;
  auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_SSH_FORGET_HOST),
                                                        tr(STR_SSH_FORGET_HOST_BODY));
  if (!dialog) return;
  startActivityForResult(std::move(dialog), [address](const ActivityResult& result) {
    if (!result.isCancelled) sshdevice::forgetHost(address.host, address.port);
  });
}

void RemoteServerActivity::openTerminal() {
  auto terminal = makeUniqueNoThrow<SshTerminalActivity>(renderer, mappedInput, editServer, origin);
  if (!terminal) {
    LOG_ERR("RMT", "OOM: terminal");
    return;
  }
  startActivityForResult(std::move(terminal), [](const ActivityResult&) {});
}

const char* RemoteServerActivity::headerTitle() const {
  if (isNewServer) return tr(STR_ADD_SERVER);
  if (!editServer.name.empty()) return editServer.name.c_str();
  return editServer.isSsh() ? tr(STR_SSH_SERVERS) : tr(STR_REMOTE_SERVERS);
}

void RemoteServerActivity::drawFooter() {
  UiListActivity::drawFooter();
  if (showSaveError) GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
}
