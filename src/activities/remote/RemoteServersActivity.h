#pragma once

#include <string>
#include <vector>

#include "RemoteServerStore.h"
#include "activities/UiListActivity.h"

/**
 * Settings > Network > WebDAV and FTP servers: the saved servers and "Add Server". A server opens
 * its own page (RemoteServerActivity) to connect, edit or delete it.
 */
class RemoteServerListActivity final : public UiListActivity {
 public:
  explicit RemoteServerListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  // Rebuilt only when the store reloads (onEnter() and after the server page closes).
  std::vector<freeink::ui::ListItem> rowItems_;
  std::vector<std::string> rowSubtitles_;
  void rebuildRowItems();
};

/**
 * One server: Connect, then the fields (name, protocol, address, user name, password, Check
 * certificate for WebDAV) and Delete. Each edit is saved at once, as on the OPDS server page.
 */
class RemoteServerActivity final : public UiListActivity {
 public:
  // serverIndex: index into RemoteServerStore, or -1 for a new server.
  explicit RemoteServerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int serverIndex = -1);

  void onEnter() override;

 private:
  enum class Row : uint8_t { Connect, Name, Protocol, Address, Username, Password, CheckCert, Delete };
  static constexpr int MAX_ROWS = 8;

  int serverIndex;
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
};
