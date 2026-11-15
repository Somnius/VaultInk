#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/LiveSyncDevice.h"

// Settings -> Vaults -> LiveSync: the LiveSync profiles (up to four, each a database pulled into its
// own vault folder) and "Sync all"; a profile's page finishes a Setup URI pasted on the web page
// (asks its passphrase here), syncs it ("Sync now") and holds its settings. With one profile the
// screen opens straight on its page.
class LiveSyncActivity final : public UiListActivity {
 public:
  // Where the silent restart after Wi-Fi lands. The quick-settings Sync tile starts a sync right
  // away (autoSync) and comes back to the book or note it was opened over.
  enum class ReturnTo : uint8_t { Settings, Reader, Home };
  // `leaveOnError`: an automatic sync on wake goes back to reading even when it fails (the result
  // waits on each profile's Sync now row). `syncMask`: the profiles an automatic sync runs
  // (slot n is bit n-1).
  explicit LiveSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool autoSync = false,
                            ReturnTo returnTo = ReturnTo::Settings, bool leaveOnError = false,
                            uint8_t syncMask = livesyncdev::ALL_PROFILES);
  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  enum class Row : uint8_t { FinishSetup, SyncNow, Vault, Server, CertCheck, AutoSync, Forget, OpenProfile, SyncAll };
  struct RowEntry {
    Row kind;
    int slot;  // OpenProfile: the profile it opens
    std::string label;
    std::string subtitle;
  };

  int listCount() const override { return static_cast<int>(rows.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  void onBackButton() override;

  void rebuildRows();
  void askPassphrase();
  // Connects to Wi-Fi first when needed, then runs `then`.
  void withWifi(void (LiveSyncActivity::*then)());
  void finishSetup();
  void syncNow();
  // Pushes and pulls one profile and saves its result line. Never two at once: the HTTPS session
  // of one sync is closed before the next starts.
  bool syncProfile(int profileSlot, livesyncdev::Profile& p);
  // Every saved profile in `syncMask`, one after the other; a failed one doesn't stop the next.
  void syncAll();
  void syncAllFromTile();
  void openList();
  void showMessage(const std::string& text);

  int slot = 0;         // the profile whose page is open; 0 = the list
  bool direct = false;  // the only profile: Back leaves the screen, there is no list to go back to
  uint8_t syncMask = livesyncdev::ALL_PROFILES;
  livesyncdev::Profile profile;
  bool configured = false;
  bool pending = false;
  bool wifiUsed = false;
  bool autoSyncPending = false;
  bool lastSyncOk = false;
  bool leaveOnError = false;
  ReturnTo returnTo = ReturnTo::Settings;
  std::string passphrase;  // only between the keyboard and the setup; wiped right after
  std::string message;     // shown above the rows (last result, errors)
  std::vector<RowEntry> rows;
  std::vector<freeink::ui::ListItem> rowItems;
};
