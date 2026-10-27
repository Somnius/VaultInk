#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/LiveSyncDevice.h"

// Settings -> Vaults -> LiveSync: finishes a Setup URI pasted on the web page (asks its passphrase
// here), then pulls the LiveSync database into its vault ("Sync now"), read only.
class LiveSyncActivity final : public UiListActivity {
 public:
  explicit LiveSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;

 private:
  enum class Row : uint8_t { FinishSetup, SyncNow, Vault, Server, Forget };

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
  void showMessage(const std::string& text);

  livesyncdev::Profile profile;
  bool configured = false;
  bool pending = false;
  bool wifiUsed = false;
  std::string passphrase;  // only between the keyboard and the setup; wiped right after
  std::string message;     // shown above the rows (last result, errors)
  std::vector<Row> rows;
  std::vector<std::string> subtitles;
  std::vector<freeink::ui::ListItem> rowItems;
};
