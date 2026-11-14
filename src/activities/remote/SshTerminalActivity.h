#pragma once

#include <RemoteServer.h>

#include <memory>
#include <string>

#include "activities/Activity.h"

// The terminal itself is only in a device build with the SSH library. Without it the screen has no
// session, so the session's members (and the onExit and preventAutoSleep overrides, which would
// only repeat Activity's) are not declared at all, instead of being defined as empty stubs.
#if defined(VAULTINK_SSH) && VAULTINK_SSH && !defined(SIMULATOR)
#define VAULTINK_SSH_TERMINAL 1
#else
#define VAULTINK_SSH_TERMINAL 0
#endif

/**
 * The SSH terminal (docs/ssh.md): a shell on a server, on a VT100-subset screen.
 *
 * - Opened from a server's page (Settings > Network > SSH servers), never from a book, so the
 *   reader's memory is not loaded. Before anything is sent the free internal heap is checked
 *   (sshdevice::heapAllowsConnection) and a low heap refuses with a message.
 * - The password is typed here at every connection and wiped once the sign-in is over; with "This
 *   device's key" there is none. The server's key must be the pinned one: a first connection
 *   shows the fingerprint and asks, a changed key is a hard stop.
 * - Input: a Bluetooth keyboard (typed through textinput::global() with terminal keys on, so Ctrl
 *   combinations, Tab and the arrows work) and the key rows on the touch screen; "Kbd" opens the
 *   on-screen keyboard for one line.
 * - E-ink: output is batched and drawn with a fast refresh at most twice a second; a full refresh
 *   after the screen is erased or every 120 scrolled lines (sshterm::RedrawPolicy).
 * - The connection attempt has one limit for the key exchange and the sign-in together (30 s) and
 *   Back or a tap cancels it; a typed key the server does not take within 3 s ends the session
 *   with a message.
 * - The session ends when this screen closes, whatever closes it: Disconnect, sleep (onExit runs
 *   on the way to sleep) or USB Drive mode (which replaces this screen). Then Wi-Fi is switched
 *   off. Nothing here uses a timer or a wake-up. The lock screen only comes up after a wake, and
 *   by then the session is gone.
 * - Text: the terminal's own fixed-cell 1-bit font (lib/Ssh/SshTermFont: 6 x 12, 8 x 14, 8 x 16 or
 *   10 x 18 pixel cells, chosen with "Terminal text size" in the SSH servers list and read when
 *   the screen opens). Glyphs are copied pixel for pixel; the columns follow the cell width.
 * - Memory: the screen and 500 lines of scrollback live in one PSRAM block (4 bytes a cell: 80
 *   columns x (rows + 500) lines is about 180 KB); wolfSSH's 32 KB window and 16 KB packet buffers
 *   are single allocations, which this board's malloc also places in PSRAM.
 *
 * In a build without VAULTINK_SSH the screen only says that SSH is not in this build.
 */
class SshTerminalActivity final : public Activity {
 public:
  explicit SshTerminalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, RemoteServer server);
  ~SshTerminalActivity() override;

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
#if VAULTINK_SSH_TERMINAL
  void onExit() override;
  // Only the connection attempt keeps the device awake. An open session does not: the device's
  // usual auto-sleep applies, and sleep closes the session.
  bool preventAutoSleep() override;
#endif

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  RemoteServer server_;  // a copy; its password field holds the typed password until the sign-in is over

#if VAULTINK_SSH_TERMINAL
  void showMessage(const char* text);
  void askPassword();
  void startWifi();
  void connectNow();
  void askHostKey();
  void enableTyping();
  void sendBytes(const std::string& bytes);
  void scrollView(int pages);
  void openKeyboard();
  void askDisconnect();
  void pressKey(uint8_t key);
  bool keyAt(int x, int y, uint8_t& keyOut) const;
  void drawGrid();
  void drawKeys();
#endif
};
