#pragma once

#include <string>
#include <vector>

#include "RemoteOrigin.h"
#include "RemoteServerStore.h"
#include "activities/UiListActivity.h"

/**
 * Home > Transfer & Remotes > Remote servers: the saved servers and "Add Server". A server opens
 * its own page (RemoteServerActivity) to connect, edit or delete it.
 *
 * Home > Transfer & Remotes > SSH terminal is the same list showing the SSH servers only
 * (`sshOnly`); a server added there starts as an SSH server. Both lists are views of the one
 * RemoteServerStore. Settings > Connectivity > "WebDAV and FTP servers" and "SSH servers" open the
 * same two lists.
 */
class RemoteServerListActivity final : public UiListActivity {
 public:
  // origin: where the list was opened from (the restart after an SSH terminal session returns there).
  explicit RemoteServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool sshOnly = false,
                                    RemoteOrigin origin = RemoteOrigin::Settings);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Rebuilt only when the store reloads (onEnter() and after the server page closes).
  std::vector<freeink::ui::ListItem> rowItems_;
  std::vector<std::string> rowSubtitles_;
  // The store index of each server row (the SSH list leaves the other servers out).
  std::vector<int16_t> rowServer_;
  bool sshOnly_;
  RemoteOrigin origin_;
  int forgetAllRow_ = -1;  // the row of "Forget all host keys" in the SSH list, -1 when it is not shown
  // The row of "Terminal text size" in the SSH list (-1 when it is not shown) and its value text.
  int textSizeRow_ = -1;
  std::string textSizeValue_;
  void rebuildRowItems();
};

/**
 * One server: Connect, then the fields (name, protocol, address, user name, password, Check
 * certificate for WebDAV) and Delete. Each edit is saved at once, as on the OPDS server page.
 *
 * An SSH server has Terminal and "Browse files (SFTP)" in place of Connect, no password row (the
 * password is typed at every connection and never saved), "Sign in with" (password or the device's
 * key), "Show public key" and "Forget the server's key" (docs/ssh.md).
 */
class RemoteServerActivity final : public UiListActivity {
 public:
  // serverIndex: index into RemoteServerStore, or -1 for a new server.
  // newAsSsh: a new server starts as an SSH server (opened from the SSH list).
  // origin: where the list was opened from, handed on to the terminal.
  explicit RemoteServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int serverIndex = -1,
                                bool newAsSsh = false, RemoteOrigin origin = RemoteOrigin::Settings);

  void onEnter() override;

 private:
  enum class Row : uint8_t {
    Connect,
    Terminal,
    Name,
    Protocol,
    Address,
    Username,
    Password,
    CheckCert,
    SignIn,
    PublicKey,
    ForgetHost,
    Delete
  };
  static constexpr int MAX_ROWS = 12;

  int serverIndex;
  bool newAsSsh;
  RemoteOrigin origin;
  RemoteServer editServer;
  bool isNewServer = false;
  bool showSaveError = false;
  // The rows on the screen now: which ones depends on the protocol and on the server being saved.
  Row rows_[MAX_ROWS]{};
  int rowCount_ = 0;
  freeink::ui::ListItem rowItems_[MAX_ROWS]{};

  int listCount() const override { return rowCount_; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void drawFooter() override;

  void layoutRows();
  bool saveServer();
  void editText(Row row);
  void toggleSignIn();
  void showPublicKey();
  void forgetHostKey();
  void openTerminal();
};
