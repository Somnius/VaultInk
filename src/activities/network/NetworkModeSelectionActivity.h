#pragma once

#include <BoardConfig.h>

#include "NetworkModeRows.h"
#include "activities/UiListActivity.h"

/**
 * NetworkModeSelectionActivity is Home > Transfer & Remotes. It presents the user with a choice:
 * - "Join a Network" - Connect to an existing WiFi network (STA mode)
 * - "Connect to Calibre" - Use Calibre wireless device transfers
 * - "Create Hotspot" - Create an Access Point that others can connect to (AP mode)
 * - "USB Drive" - on boards that can show the SD card as a USB drive
 * - "Remote servers" - the saved WebDAV, FTP and SFTP servers (RemoteServerListActivity)
 * - "SSH terminal" - the SSH servers only, in builds with SSH
 *
 * Which rows are shown, and in what order, is networkmodes::buildRows().
 *
 * The onModeSelected callback is called with the user's choice.
 * The onCancel callback is called if the user presses back.
 *
 * The header stays on GUI.drawHeader for the battery indicator.
 */
class NetworkModeSelectionActivity final : public UiListActivity {
 public:
  explicit NetworkModeSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onModeSelected(NetworkMode mode);
  void onCancel();

 private:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override { onCancel(); }
  const char* headerTitle() const override;

  // Row storage: the rows never change while the screen is open, so they are built once in the
  // constructor instead of every buildScreen() call, into fixed-capacity storage (no heap).
  networkmodes::Rows rows_;
  freeink::ui::ListItem rowItems_[networkmodes::MAX_ROWS]{};
};
