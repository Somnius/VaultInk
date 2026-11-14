#include <gtest/gtest.h>

#include <vector>

#include "src/activities/network/NetworkModeRows.h"
#include "src/activities/remote/RemoteOrigin.h"

namespace {
std::vector<NetworkMode> shown(const bool usbDrive, const bool ssh) {
  const networkmodes::Rows rows = networkmodes::buildRows(usbDrive, ssh);
  return {rows.modes.begin(), rows.modes.begin() + rows.count};
}
}  // namespace

// The rows of Home > Transfer & Remotes: a row's mode comes from this table, never from its position.

TEST(NetworkModeRowsTest, AllRowsWithUsbAndSsh) {
  EXPECT_EQ(shown(true, true), (std::vector<NetworkMode>{NetworkMode::JOIN_NETWORK, NetworkMode::CONNECT_CALIBRE,
                                                         NetworkMode::CREATE_HOTSPOT, NetworkMode::USB_DRIVE,
                                                         NetworkMode::REMOTE_SERVERS, NetworkMode::SSH_TERMINAL}));
}

TEST(NetworkModeRowsTest, NoUsbDrive) {
  // The fourth row is Remote servers here, not USB Drive (the old code cast the index to the mode).
  EXPECT_EQ(shown(false, true), (std::vector<NetworkMode>{NetworkMode::JOIN_NETWORK, NetworkMode::CONNECT_CALIBRE,
                                                          NetworkMode::CREATE_HOTSPOT, NetworkMode::REMOTE_SERVERS,
                                                          NetworkMode::SSH_TERMINAL}));
}

TEST(NetworkModeRowsTest, NoSsh) {
  EXPECT_EQ(shown(true, false), (std::vector<NetworkMode>{NetworkMode::JOIN_NETWORK, NetworkMode::CONNECT_CALIBRE,
                                                          NetworkMode::CREATE_HOTSPOT, NetworkMode::USB_DRIVE,
                                                          NetworkMode::REMOTE_SERVERS}));
}

TEST(NetworkModeRowsTest, NeitherUsbNorSsh) {
  EXPECT_EQ(shown(false, false), (std::vector<NetworkMode>{NetworkMode::JOIN_NETWORK, NetworkMode::CONNECT_CALIBRE,
                                                           NetworkMode::CREATE_HOTSPOT, NetworkMode::REMOTE_SERVERS}));
}

TEST(NetworkModeRowsTest, RowOf) {
  const networkmodes::Rows all = networkmodes::buildRows(true, true);
  EXPECT_EQ(networkmodes::rowOf(all, NetworkMode::JOIN_NETWORK), 0);
  EXPECT_EQ(networkmodes::rowOf(all, NetworkMode::USB_DRIVE), 3);
  EXPECT_EQ(networkmodes::rowOf(all, NetworkMode::REMOTE_SERVERS), 4);
  EXPECT_EQ(networkmodes::rowOf(all, NetworkMode::SSH_TERMINAL), 5);

  const networkmodes::Rows plain = networkmodes::buildRows(false, false);
  EXPECT_EQ(networkmodes::rowOf(plain, NetworkMode::REMOTE_SERVERS), 3);
  EXPECT_EQ(networkmodes::rowOf(plain, NetworkMode::USB_DRIVE), -1);
  EXPECT_EQ(networkmodes::rowOf(plain, NetworkMode::SSH_TERMINAL), -1);
}

TEST(NetworkModeRowsTest, NeverMoreThanTheStorage) {
  for (const bool usb : {false, true}) {
    for (const bool ssh : {false, true}) {
      const networkmodes::Rows rows = networkmodes::buildRows(usb, ssh);
      EXPECT_LE(rows.count, networkmodes::MAX_ROWS);
      EXPECT_EQ(rows.count, 4 + (usb ? 1 : 0) + (ssh ? 1 : 0));
    }
  }
}

// Coming back from a list, the cursor is on the row that was opened, not on Join a Network.
TEST(NetworkModeRowsTest, StartRow) {
  const networkmodes::Rows all = networkmodes::buildRows(true, true);
  EXPECT_EQ(networkmodes::startRow(all, NetworkMode::JOIN_NETWORK), 0);
  EXPECT_EQ(networkmodes::startRow(all, NetworkMode::CONNECT_CALIBRE), 1);
  EXPECT_EQ(networkmodes::startRow(all, NetworkMode::REMOTE_SERVERS), 4);
  EXPECT_EQ(networkmodes::startRow(all, NetworkMode::SSH_TERMINAL), 5);

  const networkmodes::Rows plain = networkmodes::buildRows(false, false);
  EXPECT_EQ(networkmodes::startRow(plain, NetworkMode::REMOTE_SERVERS), 3);
  // A mode the screen does not show: the first row.
  EXPECT_EQ(networkmodes::startRow(plain, NetworkMode::SSH_TERMINAL), 0);
  EXPECT_EQ(networkmodes::startRow(plain, NetworkMode::USB_DRIVE), 0);
}

// Leaving the SSH terminal: the restart goes back to the side the list was opened from.
TEST(RemoteOriginTest, TerminalExit) {
  using remoteorigin::ExitRestart;
  using remoteorigin::terminalExit;
  EXPECT_EQ(terminalExit(RemoteOrigin::Home, true, true), ExitRestart::Home);
  EXPECT_EQ(terminalExit(RemoteOrigin::Settings, true, true), ExitRestart::Settings);
  // Left before the Wi-Fi step (no password typed): nothing to switch off, no restart.
  EXPECT_EQ(terminalExit(RemoteOrigin::Home, false, false), ExitRestart::None);
  EXPECT_EQ(terminalExit(RemoteOrigin::Settings, false, false), ExitRestart::None);
  // The Wi-Fi step was reached but the radio never came up: no restart.
  EXPECT_EQ(terminalExit(RemoteOrigin::Home, true, false), ExitRestart::None);
  // A radio that some other screen left on is not this screen's to switch off.
  EXPECT_EQ(terminalExit(RemoteOrigin::Home, false, true), ExitRestart::None);
}
