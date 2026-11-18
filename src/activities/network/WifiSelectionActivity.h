#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "network/WifiAutoJoin.h"
#include "util/ButtonNavigator.h"

struct Rect;
struct ThemeMetrics;
struct WifiCredential;

// Structure to hold WiFi network information
struct WifiNetworkInfo {
  std::string ssid;
  int32_t rssi;
  int32_t channel = 0;  // of the strongest access point with this name; for the log
  bool isEncrypted;
  bool hasSavedPassword;             // Whether we have saved credentials for this network
  bool isHiddenPlaceholder = false;  // Synthetic "Add hidden network..." list entry
  bool isRescanRow = false;          // Synthetic "Scan again" list entry
};

// WiFi selection states
enum class WifiSelectionState {
  AUTO_CONNECTING,    // Trying to connect to the last known network
  SCANNING,           // Scanning for networks
  NETWORK_LIST,       // Displaying available networks
  HIDDEN_SSID_ENTRY,  // Entering SSID for a hidden network
  PASSWORD_ENTRY,     // Entering password for selected network
  CONNECTING,         // Attempting to connect
  CONNECTED,          // Successfully connected
  SAVE_PROMPT,        // Asking user if they want to save the password
  CONNECTION_FAILED,  // Connection failed
  FORGET_PROMPT       // Asking user if they want to forget the network
};

/**
 * WifiSelectionActivity is responsible for scanning WiFi APs and connecting to them.
 * It will:
 * - Enter scanning mode on entry
 * - List available WiFi networks
 * - Allow selection and launch KeyboardEntryActivity for password if needed
 * - Save the password if requested
 * - Call onComplete callback when connected or cancelled
 *
 * The onComplete callback receives true if connected successfully, false if cancelled.
 */
class WifiSelectionActivity final : public Activity, private UiAppHost {
  ButtonNavigator buttonNavigator;

  WifiSelectionState state = WifiSelectionState::SCANNING;
  size_t selectedNetworkIndex = 0;
  std::vector<WifiNetworkInfo> networks;
  // Number of real (scanned) networks, excluding the synthetic "Scan again" and hidden-network entries
  size_t realNetworkCount = 0;

  // Row buffers derived from `networks`, rebuilt only when it changes
  // (processWifiScanResults()) instead of on every repaint — buildListScreen()
  // used to re-derive a "+ * ||||" status string per network on every render
  // (cursor move, tap flash, ...).
  std::vector<std::string> networkStatuses;
  std::vector<freeink::ui::ListItem> networkRowItems;
  void rebuildNetworkRowItems();

  // Selected network for connection
  std::string selectedSSID;
  bool selectedRequiresPassword = false;

  // Connection result
  std::string connectedIP;
  std::string connectionError;

  // Password to potentially save (from keyboard or saved credentials)
  std::string enteredPassword;

  // Cached MAC address string for display
  std::string cachedMacAddress;

  // Whether network was connected using a saved password (skip save prompt)
  bool usedSavedPassword = false;

  // Whether to attempt auto-connect on entry
  const bool allowAutoConnect;

  // Whether we are attempting to auto-connect or auto-scan saved networks.
  bool autoConnecting = false;

  // True from the Confirm press that stops auto-connect until that button is released.
  bool manualNetworkListRequested = false;

  // Saved SSIDs already attempted during the current auto-connect session.
  std::vector<std::string> autoAttemptedSsids;

  // Saved SSIDs whose automatic try ran out of time; wifijoin::nextStep gives those one more try.
  std::vector<std::string> autoTimedOutSsids;

  // Saved networks to try after the last scan, in order (wifijoin::savedInRange, or blindOrder
  // when the scan failed or listed nothing).
  std::vector<std::string> autoOrder;

  // Scans started in a row by the current automatic join.
  int scanTries = 0;

  // What the last scan gave; a scan after one that failed or listed nothing restarts the radio first.
  wifijoin::Scan lastScan = wifijoin::Scan::Ok;

  // Why the list is shown after an automatic join gave up; drawn in the band under the header.
  wifijoin::Notice listNotice = wifijoin::Notice::None;

  // Save/forget prompt selection (0 = Yes, 1 = No)
  int savePromptSelection = 0;
  int forgetPromptSelection = 0;

  // Connection timeout
  static constexpr unsigned long CONNECTION_TIMEOUT_MS = 15000;
  static constexpr unsigned long AUTO_CONNECTION_TIMEOUT_MS = 7000;
  unsigned long connectionStartTime = 0;
  unsigned long scanStartTime = 0;
  // When the current automatic join started (its first scan); second tries are held to
  // wifijoin::SECOND_TRY_BUDGET_MS from here.
  unsigned long autoJoinStartTime = 0;

  // Records the radio's reason for each disconnect while this screen is up, for the log line of a
  // join that gave up. 0 = not registered.
  int disconnectEventId = 0;

  // The UiAppHost app hosts the network list and the save/forget prompts
  // (themed rows and dialogs, touch routing); every other state keeps its
  // legacy centered-text rendering.
  // Viewport memory (top/visibleRows) for the network list; `selected` is
  // mirrored from selectedNetworkIndex at build/move time.
  freeink::ui::ListNav listNav;

  static void listScreen(UiScreen& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onScanEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onPromptEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiScreen& screen);
  void buildPromptDialog(UiScreen& screen);

  void renderNetworkList(const Rect* screen, const ThemeMetrics* metrics);
  void renderPasswordEntry(const Rect* screen, const ThemeMetrics* metrics) const;
  void renderConnecting(const Rect* screen, const ThemeMetrics* metrics) const;
  void renderConnected(const Rect* screen, const ThemeMetrics* metrics) const;
  void renderConnectionFailed(const Rect* screen, const ThemeMetrics* metrics) const;

  void startWifiScan(bool autoScan = false, bool retry = false);
  void processWifiScanResults();
  void appendActionRows();
  void selectNetwork(int index);
  void promptHiddenSsid();
  void promptPasswordEntry();
  void attemptConnection();
  void checkConnectionStatus();
  void tryAutoConnectCredential(const WifiCredential& cred);
  bool tryNextSavedNetworkFromScan();
  void handleAutoConnectFailure(bool retryable);
  void logJoinGaveUp(const char* why, int status) const;
  void showNetworkListFromAutoConnect();
  static std::string getSignalStrengthIndicator(int32_t rssi);

  void onComplete(bool connected);

 public:
  explicit WifiSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool autoConnect = true);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }
};
