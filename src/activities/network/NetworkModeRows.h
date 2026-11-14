#pragma once

#include <array>
#include <cstdint>

// What a row of Home > Transfer & Remotes (NetworkModeSelectionActivity) starts. The numbers are
// not stored anywhere, and a row's position is not its mode: the screen looks the mode up in the
// row table below (UiListActivity hands activateIndex() the row index for a tap and for Confirm).
enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, USB_DRIVE, REMOTE_SERVERS, SSH_TERMINAL };

// The rows of that screen, in the order they are shown. No device headers, so the host tests build it.
namespace networkmodes {

inline constexpr int MAX_ROWS = 6;

struct Rows {
  std::array<NetworkMode, MAX_ROWS> modes{};
  int count = 0;
};

// usbDrive: the board can show the SD card as a USB drive (FREEINK_CAP_USB_MSC).
// ssh: the build has SSH (sshdevice::available()).
constexpr Rows buildRows(const bool usbDrive, const bool ssh) {
  Rows rows;
  rows.modes[rows.count++] = NetworkMode::JOIN_NETWORK;
  rows.modes[rows.count++] = NetworkMode::CONNECT_CALIBRE;
  rows.modes[rows.count++] = NetworkMode::CREATE_HOTSPOT;
  if (usbDrive) rows.modes[rows.count++] = NetworkMode::USB_DRIVE;
  rows.modes[rows.count++] = NetworkMode::REMOTE_SERVERS;
  if (ssh) rows.modes[rows.count++] = NetworkMode::SSH_TERMINAL;
  return rows;
}

// The row a mode is shown at, or -1 when the screen does not show it.
constexpr int rowOf(const Rows& rows, const NetworkMode mode) {
  for (int i = 0; i < rows.count; i++) {
    if (rows.modes[i] == mode) return i;
  }
  return -1;
}

// The row the cursor starts on: the row of `selected`, or the first row when it is not shown.
constexpr int startRow(const Rows& rows, const NetworkMode selected) {
  const int row = rowOf(rows, selected);
  return row < 0 ? 0 : row;
}

}  // namespace networkmodes
