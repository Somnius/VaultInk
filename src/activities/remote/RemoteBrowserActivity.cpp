#include "RemoteBrowserActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <Memory.h>
#include <SshKey.h>

#include <cstdio>
#include <utility>
#include <vector>

#include "MappedInputManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "network/ProtectedPaths.h"
#include "network/SshDevice.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr char DEFAULT_SAVE_FOLDER[] = "/books";

StrId errorText(const remotefs::Error error) {
  switch (error) {
    case remotefs::Error::BadAddress:
      return StrId::STR_REMOTE_ERR_ADDRESS;
    case remotefs::Error::PlainRefused:
      return StrId::STR_REMOTE_PLAIN_TITLE;
    case remotefs::Error::LowMemory:
      return StrId::STR_MEMORY_ERROR;
    case remotefs::Error::NoClock:
      return StrId::STR_REMOTE_ERR_CLOCK;
    case remotefs::Error::Certificate:
      return StrId::STR_REMOTE_ERR_CERT;
    case remotefs::Error::Auth:
      return StrId::STR_REMOTE_ERR_AUTH;
    case remotefs::Error::NotFound:
      return StrId::STR_REMOTE_ERR_NOT_FOUND;
    case remotefs::Error::Refused:
      return StrId::STR_REMOTE_ERR_REFUSED;
    case remotefs::Error::Redirected:
      return StrId::STR_REMOTE_ERR_REDIRECT;
    case remotefs::Error::BadAnswer:
      return StrId::STR_REMOTE_ERR_ANSWER;
    case remotefs::Error::File:
      return StrId::STR_REMOTE_ERR_FILE;
    case remotefs::Error::HostKeyChanged:
      return StrId::STR_SSH_HOST_KEY_CHANGED;
    case remotefs::Error::HostKeyUnknown:
      return StrId::STR_SSH_HOST_KEY_TITLE;
    case remotefs::Error::NoCipher:
      return StrId::STR_SSH_ERR_CIPHER;
    case remotefs::Error::Unavailable:
      return StrId::STR_SSH_ERR_UNAVAILABLE;
    case remotefs::Error::HostKeysUnreadable:
      return StrId::STR_SSH_KNOWN_HOSTS_UNREADABLE;
    case remotefs::Error::HostKeysChanged:
      return StrId::STR_SSH_KNOWN_HOSTS_CHANGED;
    case remotefs::Error::Network:
    default:
      return StrId::STR_REMOTE_ERR_NETWORK;
  }
}

// "/books" + "a.epub" -> "/books/a.epub" on the card.
std::string localPath(const std::string& folder, const std::string& name) {
  return remotefiles::joinPath(folder.empty() ? "/" : folder, name);
}

std::string baseName(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}
}  // namespace

RemoteBrowserActivity::RemoteBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             RemoteServer server)
    : CatalogActivity("RemoteBrowser", renderer, mappedInput), server(std::move(server)) {}

void RemoteBrowserActivity::onEnter() {
  CatalogActivity::onEnter();
  saveFolder = Storage.exists(DEFAULT_SAVE_FOLDER) ? DEFAULT_SAVE_FOLDER : "/";
  state = State::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  plainConfirmed = false;

  if (server.isSsh()) {
    if (!sshdevice::available()) {
      fail(StrId::STR_SSH_ERR_UNAVAILABLE);
    } else if (server.url.empty() || server.username.empty()) {
      fail(StrId::STR_SSH_ERR_NO_USER);
    } else if (server.keyAuth) {
      if (sshdevice::hasKey()) {
        connect();
      } else {
        fail(StrId::STR_SSH_KEY_FAILED);
      }
    } else {
      askSshPassword();
    }
    return;
  }
  if (!server.isPlain()) {
    connect();
    return;
  }
  // Not encrypted: say so and ask, every time, before Wi-Fi is even switched on. Cancel is the
  // preselected answer.
  const bool ftp = server.protocol == RemoteServer::Protocol::Ftp;
  auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOTE_PLAIN_TITLE),
                                                        ftp ? tr(STR_REMOTE_PLAIN_FTP) : tr(STR_REMOTE_PLAIN_HTTP),
                                                        std::string(), tr(STR_CONNECT));
  if (!dialog) {
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      finish();
      return;
    }
    plainConfirmed = true;
    connect();
  });
}

void RemoteBrowserActivity::onExit() {
  if (client) {
    client->close();
    client.reset();
  }
  // The caller (ActivityManager) holds the render lock here, so the list is dropped without taking
  // it again (releaseEntries() would wait for it forever).
  closeRouting();
  listing = remotefiles::Listing{};
  wipePassword();
  // Wi-Fi goes down here (and the device restarts to clear the heap, as after OPDS).
  CatalogActivity::onExit();
}

void RemoteBrowserActivity::wipePassword() {
  // Only an SSH password is typed for the visit; the others are the saved ones of the store.
  if (!server.isSsh()) return;
  sshkey::wipe(server.password.data(), server.password.size());
  server.password.clear();
}

void RemoteBrowserActivity::askSshPassword() {
  wipePassword();
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD), std::string(),
                                                           RemoteServerStore::MAX_FIELD_BYTES, InputType::Password);
  if (!keyboard) {
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      finish();
      return;
    }
    server.password = std::get<KeyboardResult>(result.data).text;
    // The keyboard's copy of the password is not needed past this line.
    sshkey::wipeConst(std::get<KeyboardResult>(result.data).text);
    if (server.password.empty()) {
      finish();
      return;
    }
    connect();
  });
}

void RemoteBrowserActivity::askHostKey() {
  std::string type;
  std::string fingerprint;
  if (!client || !client->hostKey(type, fingerprint)) {
    failWith(remotefs::Error::Network);
    return;
  }
  const std::string body = server.url + "\n" + type + "\n" + fingerprint + "\n\n" + tr(STR_SSH_HOST_KEY_BODY);
  // Cancel is the preselected answer.
  auto dialog = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_SSH_HOST_KEY_TITLE), body,
                                                        std::string(), tr(STR_SSH_TRUST));
  if (!dialog) {
    fail(StrId::STR_MEMORY_ERROR);
    return;
  }
  startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      finish();
      return;
    }
    if (!client || !client->trustHostKey()) {
      failWith(remotefs::Error::File);
      return;
    }
    // Again from the start: the server must now show the key that was just pinned.
    startBrowse();
  });
}

void RemoteBrowserActivity::retryBrowse() {
  // A wrong SSH password is typed again, not sent again.
  if (server.isSsh() && !server.keyAuth && sshdevice::available()) {
    if (client) {
      client->close();
      client.reset();
    }
    askSshPassword();
    return;
  }
  startBrowse();
}

void RemoteBrowserActivity::connect() {
  state = State::CHECK_WIFI;
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();
  checkAndConnectWifi();
}

void RemoteBrowserActivity::releaseEntries() {
  // The app's interaction table holds row indices for the old entries; stop routing touches
  // against it until the next render. The render task reads the list: swap it under the lock.
  closeRouting();
  RenderLock lock(*this);
  std::vector<remotefiles::Entry>().swap(listing.entries);  // really frees the block
  listing = remotefiles::Listing{};
}

void RemoteBrowserActivity::failWith(const remotefs::Error error) {
  LOG_ERR("RMT", "Failed: %d", static_cast<int>(error));
  fail(errorText(error));
}

void RemoteBrowserActivity::startBrowse() {
  if (!client) {
    remotefs::Error error = remotefs::Error::Ok;
    client = remotefs::makeClient(server, plainConfirmed, error);
    // The client has its own copy of a typed SSH password (and wipes it after the sign-in).
    wipePassword();
    if (!client) {
      failWith(error);
      return;
    }
    currentDir = client->startDir();
  }
  releaseEntries();
  nav.reset();
  beginLoading();

  remotefiles::Listing fresh;
  const remotefs::Error error = client->list(currentDir, fresh, remotefs::Transfer{});
  if (error == remotefs::Error::HostKeyUnknown) {
    askHostKey();
    return;
  }
  if (error != remotefs::Error::Ok) {
    failWith(error);
    return;
  }
  if (fresh.truncated) notice = tr(STR_REMOTE_LIST_CUT);
  {
    RenderLock lock(*this);
    listing = std::move(fresh);
  }
  state = State::BROWSING;
  requestUpdate();
}

void RemoteBrowserActivity::downloadFinished(bool) {
  // The list was freed for the transfer: read the folder again (an upload shows up in it).
  startBrowse();
}

void RemoteBrowserActivity::onBackButton() {
  if (!client || currentDir == "/" || currentDir.empty()) {
    finish();
    return;
  }
  currentDir = remotefiles::parentPath(currentDir);
  notice.clear();
  startBrowse();
}

void RemoteBrowserActivity::activateIndex(const int index) {
  app.clearTapFlash();
  if (state != State::BROWSING || !client) return;
  if (index == ROW_SAVE_TO) {
    chooseSaveFolder();
    return;
  }
  if (index == ROW_UPLOAD) {
    chooseUpload();
    return;
  }
  if (index == ROW_NEW_FOLDER) {
    askNewFolder();
    return;
  }
  const size_t entryIndex = static_cast<size_t>(index - ACTION_ROWS);
  if (index < ACTION_ROWS || entryIndex >= listing.entries.size()) return;
  // Copy what is needed: the list is released before the transfer or the next folder is read.
  const std::string name = listing.entries[entryIndex].name;
  const uint64_t size = listing.entries[entryIndex].size;
  if (listing.entries[entryIndex].isDir) {
    currentDir = remotefiles::joinPath(currentDir, name);
    notice.clear();
    startBrowse();
  } else {
    download(name, size);
  }
}

void RemoteBrowserActivity::chooseSaveFolder() {
  auto picker =
      makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, saveFolder, FileBrowserActivity::Mode::PickFolder);
  if (!picker) return;
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (const auto* path = std::get_if<FilePathResult>(&result.data)) {
      saveFolder = path->path;
      while (saveFolder.size() > 1 && saveFolder.back() == '/') saveFolder.pop_back();
      if (saveFolder.empty()) saveFolder = "/";
    }
  });
}

void RemoteBrowserActivity::chooseUpload() {
  auto picker =
      makeUniqueNoThrow<FileBrowserActivity>(renderer, mappedInput, saveFolder, FileBrowserActivity::Mode::PickFile);
  if (!picker) return;
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (const auto* path = std::get_if<FilePathResult>(&result.data)) upload(path->path);
  });
}

void RemoteBrowserActivity::askNewFolder() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_NEW_FOLDER), std::string(), 63,
                                                           InputType::Text);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled || !client) return;
    const std::string name = std::get<KeyboardResult>(result.data).text;
    if (!remotefiles::safeName(name)) {
      notice = tr(STR_REMOTE_ERR_NAME);
      return;
    }
    beginLoading();
    const remotefs::Error error = client->makeDir(currentDir, name);
    if (error != remotefs::Error::Ok) {
      failWith(error);
      return;
    }
    notice.clear();
    startBrowse();
  });
}

void RemoteBrowserActivity::download(const std::string& name, const uint64_t size) {
  if (!remotefiles::safeName(name)) {  // the list holds only safe names; checked again where it is used
    notice = tr(STR_REMOTE_ERR_NAME);
    requestUpdate();
    return;
  }
  if (!Storage.exists(saveFolder.c_str()) && !Storage.mkdir(saveFolder.c_str())) {
    failWith(remotefs::Error::File);
    return;
  }
  const std::string dest = localPath(saveFolder, name);
  // A server must not be able to put a file over the saved passwords, whatever it calls it.
  if (protectedpaths::isSensitivePath(dest.c_str()) || protectedpaths::isInDeviceFolder(dest.c_str())) {
    notice = tr(STR_REMOTE_ERR_NAME);
    requestUpdate();
    return;
  }
  beginDownload(name);
  releaseEntries();  // the list's memory goes to the transfer; the folder is read again afterwards
  beginTransfer();

  remotefs::Transfer transfer;
  transfer.cancel = &cancelDownload;
  transfer.progress = [this](const size_t done, const size_t total) { transferProgress(done, total); };
  const remotefs::Error result = client->download(currentDir, name, dest, size, transfer);
  if (result == remotefs::Error::Ok) {
    // A book that was replaced must not be shown from its old cache; a new one belongs in the library.
    clearBookCache(dest);
    library::markLibraryIndexDirty();
  }
  finishTransfer(result, std::string(tr(STR_REMOTE_SAVED_TO)) + " " + dest);
}

void RemoteBrowserActivity::upload(const std::string& path) {
  const std::string name = baseName(path);
  // The saved passwords (Wi-Fi, OPDS, KOReader sync, these servers) stay on the device.
  if (!client || !remotefiles::safeName(name) || protectedpaths::isSensitivePath(path.c_str()) ||
      protectedpaths::isInDeviceFolder(path.c_str())) {
    notice = tr(STR_REMOTE_ERR_NAME);
    requestUpdate();
    return;
  }
  beginDownload(name);
  releaseEntries();
  beginTransfer(tr(STR_REMOTE_UPLOADING));

  remotefs::Transfer transfer;
  transfer.cancel = &cancelDownload;
  transfer.progress = [this](const size_t done, const size_t total) { transferProgress(done, total); };
  const remotefs::Error result = client->upload(path, currentDir, name, transfer);
  finishTransfer(result, std::string(tr(STR_REMOTE_UPLOADED)) + " " + name);
}

void RemoteBrowserActivity::finishTransfer(const remotefs::Error result, std::string doneNotice) {
  if (result == remotefs::Error::Ok) {
    notice = std::move(doneNotice);
    finishDownload(HttpDownloader::OK);
  } else if (result == remotefs::Error::Aborted) {
    notice.clear();
    finishDownload(HttpDownloader::ABORTED);
  } else {
    failWith(result);
  }
}

const char* RemoteBrowserActivity::title() const {
  if (state == State::BROWSING && currentDir.size() > 1) {
    const size_t slash = currentDir.rfind('/');
    return currentDir.c_str() + (slash == std::string::npos ? 0 : slash + 1);
  }
  return server.name.empty() ? tr(STR_REMOTE_SERVERS) : server.name.c_str();
}

// fui::ListProps::rowProvider: formats one row on demand, so nothing per entry is kept beyond the
// listing itself. The pointers stay valid until the next call, which is all the contract needs.
void RemoteBrowserActivity::provideRow(void* ctx, const uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<RemoteBrowserActivity*>(ctx);
  item.actionValue = static_cast<int16_t>(index);
  if (index == ROW_SAVE_TO) {
    item.label = tr(STR_REMOTE_SAVE_TO);
    item.value = self->saveFolder == "/" ? tr(STR_SD_CARD) : self->saveFolder.c_str();
    return;
  }
  if (index == ROW_UPLOAD) {
    item.label = tr(STR_REMOTE_UPLOAD_HERE);
    return;
  }
  if (index == ROW_NEW_FOLDER) {
    item.label = tr(STR_NEW_FOLDER);
    return;
  }
  const size_t entryIndex = static_cast<size_t>(index - ACTION_ROWS);
  if (entryIndex >= self->listing.entries.size()) return;
  const remotefiles::Entry& entry = self->listing.entries[entryIndex];
  item.label = entry.name.c_str();
  if (entry.isDir) {
    item.value = ">";
  } else {
    snprintf(self->rowValueBuf, sizeof(self->rowValueBuf), "%s", remotefiles::sizeText(entry.size).c_str());
    item.value = self->rowValueBuf;
  }
}

void RemoteBrowserActivity::buildScreen(UiScreen& screen) {
  screenHeader(screen, title());
  if (buildStatusScreen(screen, /*boldError=*/false, /*showDownloadTotal=*/true)) return;

  if (!notice.empty()) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    fui::TextStyle small = screen.theme().smallText;
    small.maxLines = 2;
    const int16_t lineHeight = screen.target().lineHeight(small.font);
    const fui::Rect band = screen.takeTop(static_cast<int16_t>(lineHeight * 2), static_cast<int16_t>(4));
    const auto pad = static_cast<int16_t>(metrics.contentSidePadding);
    screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), notice.c_str(), small);
  }

  fui::ListProps props;
  props.rowProvider = &RemoteBrowserActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 2;
  props.labelText = label;
  syncListViewport(screen, props);
  screen.list(props);
}

void RemoteBrowserActivity::drawFooter() {
  MappedInputManager::Labels labels;
  switch (state) {
    case State::BROWSING: {
      const int index = nav.selected;
      const size_t entryIndex = static_cast<size_t>(index - ACTION_ROWS);
      const bool file =
          index >= ACTION_ROWS && entryIndex < listing.entries.size() && !listing.entries[entryIndex].isDir;
      labels =
          mappedInput.mapLabels(tr(STR_BACK), file ? tr(STR_DOWNLOAD) : tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      break;
    }
    case State::DOWNLOADING:
      labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
      break;
    case State::ERROR:
      labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
      break;
    default:
      labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      break;
  }
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
