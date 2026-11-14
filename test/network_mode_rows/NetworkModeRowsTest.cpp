#include <gtest/gtest.h>

#include <vector>

#include "src/activities/network/NetworkModeRows.h"

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
