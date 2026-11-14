#pragma once

// Where a list of saved servers was opened from, handed down to the screens it opens. A network
// session ends with Wi-Fi off and a restart (a clean heap), and the restart returns to the side
// the user came from. No device headers, so the host tests build it.
enum class RemoteOrigin {
  Home,      // Home > Transfer & Remotes
  Settings,  // Settings > Connectivity
};

namespace remoteorigin {

enum class ExitRestart { None, Home, Settings };

// What the SSH terminal does on its way out.
// wifiStarted: the screen got as far as its Wi-Fi step (it may have brought the radio up).
// radioOn: the radio is on now (the Wi-Fi picker's scan turns it on even when it is cancelled).
constexpr ExitRestart terminalExit(const RemoteOrigin origin, const bool wifiStarted, const bool radioOn) {
  if (!wifiStarted || !radioOn) return ExitRestart::None;
  return origin == RemoteOrigin::Settings ? ExitRestart::Settings : ExitRestart::Home;
}

}  // namespace remoteorigin
