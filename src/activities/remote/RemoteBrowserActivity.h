#pragma once

#include <RemoteFiles.h>

#include <memory>
#include <string>

#include "RemoteServerStore.h"
#include "activities/CatalogActivity.h"
#include "network/RemoteFsClient.h"

/**
 * Browses a WebDAV, FTP or SFTP server: tap a folder to enter it, tap a file to download it into the
 * chosen SD folder, "Upload a file here" to send one picked from the card, "New folder".
 *
 * Wi-Fi comes up when this screen opens (a user action) and goes down when it closes
 * (CatalogActivity::onExit); nothing here uses a timer or wakes the device. A server that is not
 * encrypted (FTP, or WebDAV over http://) asks first, every time the screen opens.
 *
 * SFTP (docs/ssh.md): the password is typed when the screen opens (never saved; with the device's
 * key there is none), a first connection shows the server's key fingerprint and asks, and a
 * changed key stops with "Host key changed".
 */
class RemoteBrowserActivity final : public CatalogActivity {
 public:
  explicit RemoteBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RemoteServer server);

  void onEnter() override;
  void onExit() override;

 private:
  // The rows above the entries.
  enum : int { ROW_SAVE_TO = 0, ROW_UPLOAD = 1, ROW_NEW_FOLDER = 2, ACTION_ROWS = 3 };

  RemoteServer server;  // a copy: safe even if the store changes while browsing
  std::unique_ptr<remotefs::Client> client;
  remotefiles::Listing listing;  // read by the render task through provideRow(): change under the render lock
  std::string currentDir = "/";
  std::string saveFolder;  // where downloads go on the card
  std::string notice;      // one line above the list: what just happened, or that the list is cut
  char rowValueBuf[24]{};
  bool plainConfirmed = false;

  int listCount() const override {
    return state == State::BROWSING ? ACTION_ROWS + static_cast<int>(listing.entries.size()) : 0;
  }
  bool hasSearch() const override { return false; }
  void performSearch(const std::string&) override {}
  void activateIndex(int index) override;
  void buildScreen(UiScreen& screen) override;
  void drawFooter() override;
  void startBrowse() override;
  void retryBrowse() override;
  void downloadFinished(bool cancelled) override;
  void onBackButton() override;

  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);
  void connect();
  // SFTP: the password for this visit, then connect().
  void askSshPassword();
  // SFTP, first connection: show the fingerprint; yes pins the key and reads the folder again.
  void askHostKey();
  void wipePassword();
  void releaseEntries();
  void failWith(remotefs::Error error);
  void chooseSaveFolder();
  void chooseUpload();
  void askNewFolder();
  void download(const std::string& name, uint64_t size);
  void upload(const std::string& localPath);
  void finishTransfer(remotefs::Error result, std::string doneNotice);
  const char* title() const;
};
