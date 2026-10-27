#include "LiveSyncActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <mbedtls/platform_util.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/TaskWatchdog.h"
#include "util/VaultFiles.h"

namespace fui = freeink::ui;

namespace {

// A popup with a progress bar the long steps (key derivation, the pull) fill in.
struct Progress {
  GfxRenderer* renderer = nullptr;
  Rect popup{};
  int last = -1;
  void begin(GfxRenderer& r, const char* text) {
    renderer = &r;
    RenderLock lock;
    popup = GUI.drawPopup(r, text);
    last = -1;
  }
  void set(const int percent) {
    resetTaskWatchdogIfSubscribed();
    if (!renderer || percent == last) return;
    last = percent;
    RenderLock lock;
    GUI.fillPopupProgress(*renderer, popup, percent);
  }
};
Progress progress;

void keyRounds(const int done, void*) { progress.set(done * 100 / livesync::PBKDF2_ITERATIONS); }

}  // namespace

LiveSyncActivity::LiveSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("LiveSync", renderer, mappedInput) {}

const char* LiveSyncActivity::headerTitle() const { return tr(STR_LIVESYNC); }

void LiveSyncActivity::onEnter() {
  UiListActivity::onEnter();
  livesync::setDefaultYield(keyRounds, nullptr);
  rebuildRows();
}

void LiveSyncActivity::onExit() {
  livesync::setDefaultYield(nullptr, nullptr);
  mbedtls_platform_zeroize(passphrase.data(), passphrase.size());
  passphrase.clear();
  livesyncdev::wipeProfile(profile);
  livesyncdev::endSession();
  UiListActivity::onExit();
  if (wifiUsed) {
    // TLS and Wi-Fi leave the heap fragmented: start clean, as the other sync screens do.
    WiFi.disconnect(false);
    delay(30);
    silentRestartToSettings();
  }
}

void LiveSyncActivity::onBackButton() { finish(); }

void LiveSyncActivity::rebuildRows() {
  livesyncdev::wipeProfile(profile);
  configured = livesyncdev::load(profile);
  pending = livesyncdev::hasPendingSetup();
  rows.clear();
  subtitles.clear();
  if (pending) {
    rows.push_back(Row::FinishSetup);
    subtitles.emplace_back(tr(STR_LIVESYNC_FINISH_DESC));
  }
  if (configured) {
    rows.push_back(Row::SyncNow);
    subtitles.push_back(profile.lastResult.empty() ? std::string(tr(STR_LIVESYNC_NEVER)) : profile.lastResult);
    rows.push_back(Row::Vault);
    subtitles.push_back(std::string(vaultfiles::VAULTS_ROOT) + "/" + profile.vaultName);
    rows.push_back(Row::Server);
    subtitles.push_back(
        profile.serverUrl + " · " + profile.database +
        (livesyncdev::certificateChecked() ? "" : std::string(" · ") + tr(STR_LIVESYNC_CERT_UNCHECKED)));
    rows.push_back(Row::Forget);
    subtitles.emplace_back(tr(STR_LIVESYNC_FORGET_DESC));
  }
  rowItems.assign(rows.size(), fui::ListItem{});
  for (size_t i = 0; i < rows.size(); ++i) {
    static constexpr StrId LABELS[] = {StrId::STR_LIVESYNC_FINISH, StrId::STR_LIVESYNC_SYNC_NOW,
                                       StrId::STR_LIVESYNC_VAULT, StrId::STR_LIVESYNC_SERVER,
                                       StrId::STR_LIVESYNC_FORGET};
    rowItems[i].label = I18N.get(LABELS[static_cast<int>(rows[i])]);
    rowItems[i].subtitle = subtitles[i].c_str();
    rowItems[i].actionValue = static_cast<int16_t>(i);
  }
  requestUpdate();
}

void LiveSyncActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  auto text = screen.theme().bodyText;
  text.maxLines = 8;
  if (!message.empty()) {
    const auto box = screen.body().inset(fui::Insets{0, 16, 0, 16});
    screen.target().text(fui::Rect{box.x, box.y, box.width, 90}, message.c_str(), text);
    screen.spacer(96);
  }
  if (rows.empty()) {
    text.align = fui::TextAlign::Center;
    const auto body = screen.body().inset(fui::Insets{40, 24, 0, 24});
    screen.target().text(body, tr(STR_LIVESYNC_HELP), text);
    return;
  }
  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.subtitleText = screen.theme().smallText;
  syncListViewport(screen, props);
  screen.list(props);
}

void LiveSyncActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rows.size())) return;
  switch (rows[index]) {
    case Row::FinishSetup:
      askPassphrase();
      break;
    case Row::SyncNow:
      withWifi(&LiveSyncActivity::syncNow);
      break;
    case Row::Forget:
      livesyncdev::forget();
      message = tr(STR_LIVESYNC_FORGOTTEN);
      rebuildRows();
      break;
    case Row::Vault:
    case Row::Server:
      break;
  }
}

void LiveSyncActivity::askPassphrase() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_LIVESYNC_PASSPHRASE), "", 128,
                                                           InputType::Password);
  if (!keyboard) return;
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    passphrase = std::get<KeyboardResult>(result.data).text;
    withWifi(&LiveSyncActivity::finishSetup);
  });
}

void LiveSyncActivity::withWifi(void (LiveSyncActivity::*then)()) {
  wifiUsed = true;
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);  // modem sleep stalls long transfers
    (this->*then)();
    return;
  }
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) return;
  startActivityForResult(std::move(wifi), [this, then](const ActivityResult& result) {
    if (result.isCancelled) {
      mbedtls_platform_zeroize(passphrase.data(), passphrase.size());
      passphrase.clear();
      return;
    }
    WiFi.setSleep(false);
    (this->*then)();
  });
}

void LiveSyncActivity::showMessage(const std::string& text) {
  message = text;
  rebuildRows();
}

void LiveSyncActivity::finishSetup() {
  std::string uri, e2ee, why;
  livesyncdev::Profile fresh;
  progress.begin(renderer, tr(STR_LIVESYNC_UNLOCKING));
  const bool ok = livesyncdev::readPendingSetup(uri) &&
                  livesyncdev::profileFromSetup(uri, passphrase, fresh, e2ee, why) &&
                  livesyncdev::finishSetup(fresh, e2ee, why);
  mbedtls_platform_zeroize(passphrase.data(), passphrase.size());
  passphrase.clear();
  mbedtls_platform_zeroize(e2ee.data(), e2ee.size());
  mbedtls_platform_zeroize(uri.data(), uri.size());
  livesyncdev::endSession();
  if (!ok) {
    livesyncdev::wipeProfile(fresh);
    showMessage(why.empty() ? std::string(tr(STR_LIVESYNC_SETUP_FAILED)) : why);
    return;
  }
  // The vault folder is named after the database; a name the SD card can't hold gets a plain one.
  if (!livesync::safeVaultPath(fresh.vaultName) || fresh.vaultName.find('/') != std::string::npos) {
    fresh.vaultName = "LiveSync";
  }
  const bool saved = livesyncdev::save(fresh);
  livesyncdev::wipeProfile(fresh);
  if (!saved) {
    showMessage(tr(STR_LIVESYNC_SETUP_FAILED));
    return;
  }
  livesyncdev::clearPendingSetup();
  livesyncdev::resetSyncState(fresh);  // a new setup pulls everything again
  showMessage(tr(STR_LIVESYNC_READY));
}

void LiveSyncActivity::syncNow() {
  progress.begin(renderer, tr(STR_LIVESYNC_SYNCING));
  // The vault folder was deleted (or never written): the saved checkpoint would skip everything.
  if (!Storage.exists(livesyncdev::options(profile).vaultDir.c_str())) livesyncdev::resetSyncState(profile);
  livesync::SyncStats stats;
  std::string error;
  const auto options = livesyncdev::options(profile);
  const bool ok = livesync::syncOnce(livesyncdev::transport(profile), livesyncdev::storage(), profile.master, options,
                                     stats, error, [](int done, const std::string& path) {
                                       LOG_DBG("LSY", "%d %s", done, path.c_str());
                                       progress.set(done % 100);
                                     });
  livesyncdev::endSession();
  char summary[160];
  if (ok) {
    snprintf(summary, sizeof(summary), tr(STR_LIVESYNC_DONE), stats.written, stats.trashed);
    profile.lastResult = summary;
  } else {
    profile.lastResult = error;
  }
  livesyncdev::save(profile);
  showMessage(profile.lastResult);
}
