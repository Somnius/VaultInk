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
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "network/HttpDownloader.h"
#include "util/TaskWatchdog.h"
#include "util/VaultEdit.h"
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

LiveSyncActivity::LiveSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool autoSync,
                                   const ReturnTo returnTo, const bool leaveOnError, const uint8_t syncMask)
    : UiListActivity("LiveSync", renderer, mappedInput),
      syncMask(syncMask),
      autoSyncPending(autoSync),
      leaveOnError(leaveOnError),
      returnTo(returnTo) {}

const char* LiveSyncActivity::headerTitle() const { return tr(STR_LIVESYNC); }

void LiveSyncActivity::onEnter() {
  UiListActivity::onEnter();
  livesync::setDefaultYield(keyRounds, nullptr);
  openList();
}

// The list of profiles, or straight to the page of the only one.
void LiveSyncActivity::openList() {
  int used = 0, only = 0;
  for (int s = 1; s <= livesyncdev::MAX_PROFILES; ++s) {
    if (livesync::slotInfo(livesyncdev::profileFs(), s).inUse()) {
      ++used;
      only = s;
    }
  }
  direct = used == 1;
  slot = direct ? only : 0;
  activeNav().requestSelection(0);
  rebuildRows();
}

void LiveSyncActivity::onExit() {
  livesync::setDefaultYield(nullptr, nullptr);
  mbedtls_platform_zeroize(passphrase.data(), passphrase.size());
  passphrase.clear();
  dropStaged();
  livesyncdev::wipeProfile(profile);
  livesyncdev::endSession();
  UiListActivity::onExit();
  if (wifiUsed) {
    // TLS and Wi-Fi leave the heap fragmented: start clean, as the other sync screens do.
    WiFi.disconnect(false);
    delay(30);
    switch (returnTo) {
      case ReturnTo::Reader:
        silentRestartToReader();
        break;
      case ReturnTo::Home:
        silentRestart();
        break;
      case ReturnTo::Settings:
        silentRestartToSettings();
        break;
    }
  }
}

void LiveSyncActivity::loop() {
  // The tile's sync starts from the first loop, once the screen is up (not from onEnter).
  if (autoSyncPending) {
    autoSyncPending = false;
    if (livesyncdev::hasProfile()) {
      withWifi(&LiveSyncActivity::syncAllFromTile);
      return;
    }
    if (leaveOnError) {  // woke with Wi-Fi up but no usable profile: restart turns the radio off
      wifiUsed = true;
      finish();
      return;
    }
  }
  UiListActivity::loop();
}

void LiveSyncActivity::syncAllFromTile() {
  syncAll();
  // Synced: straight back to where the tile was tapped. A failure stays on screen to be read.
  if (lastSyncOk || leaveOnError) finish();
}

void LiveSyncActivity::onBackButton() {
  if (slot != 0 && !direct) {
    message.clear();
    openList();
    return;
  }
  finish();
}

void LiveSyncActivity::rebuildRows() {
  livesyncdev::wipeProfile(profile);
  rows.clear();
  const auto& fs = livesyncdev::profileFs();
  if (slot == 0) {
    configured = false;
    pending = false;
    bool anyConfigured = false;
    for (int s = 1; s <= livesyncdev::MAX_PROFILES; ++s) {
      const livesync::SlotInfo info = livesync::slotInfo(fs, s);
      if (!info.inUse()) continue;
      anyConfigured = anyConfigured || info.configured;
      char name[48];
      snprintf(name, sizeof(name), tr(STR_LIVESYNC_PROFILE_N), s);
      rows.push_back({Row::OpenProfile, s, info.vault.empty() ? std::string(name) : info.vault,
                      info.pending              ? std::string(tr(STR_LIVESYNC_FINISH_DESC))
                      : info.lastResult.empty() ? std::string(tr(STR_LIVESYNC_NEVER))
                                                : info.lastResult});
    }
    if (anyConfigured) rows.push_back({Row::SyncAll, 0, tr(STR_LIVESYNC_SYNC_ALL), tr(STR_LIVESYNC_SYNC_ALL_DESC)});
  } else {
    configured = livesyncdev::load(slot, profile);
    livesync::PendingSetup waiting;
    pending = livesync::readPending(fs, slot, waiting);
    mbedtls_platform_zeroize(waiting.uri.data(), waiting.uri.size());
    const auto add = [this](const Row kind, const StrId label, std::string subtitle) {
      rows.push_back({kind, slot, I18N.get(label), std::move(subtitle)});
    };
    if (pending) add(Row::FinishSetup, StrId::STR_LIVESYNC_FINISH, tr(STR_LIVESYNC_FINISH_DESC));
    if (configured) {
      add(Row::SyncNow, StrId::STR_LIVESYNC_SYNC_NOW,
          profile.lastResult.empty() ? std::string(tr(STR_LIVESYNC_NEVER)) : profile.lastResult);
      add(Row::Vault, StrId::STR_LIVESYNC_VAULT, std::string(vaultfiles::VAULTS_ROOT) + "/" + profile.vaultName);
      add(Row::Server, StrId::STR_LIVESYNC_SERVER,
          profile.serverUrl + " · " + profile.database +
              (profile.checkCert ? "" : std::string(" · ") + tr(STR_LIVESYNC_CERT_UNCHECKED)));
      add(Row::CertCheck, StrId::STR_LIVESYNC_CERT_CHECK,
          !profile.checkCert            ? tr(STR_LIVESYNC_CERT_OFF)
          : livesyncdev::hasOwnCaCert() ? tr(STR_LIVESYNC_CERT_OWN)
                                        : tr(STR_LIVESYNC_CERT_BUILTIN));
      char text[96];
      snprintf(text, sizeof(text), tr(STR_LIVESYNC_AUTO_EVERY), profile.autoHours);
      add(Row::AutoSync, StrId::STR_LIVESYNC_AUTO, profile.autoHours == 0 ? tr(STR_LIVESYNC_AUTO_OFF) : text);
    }
    if (pending || configured) add(Row::Forget, StrId::STR_LIVESYNC_FORGET, tr(STR_LIVESYNC_FORGET_DESC));
  }
  rowItems.assign(rows.size(), fui::ListItem{});
  for (size_t i = 0; i < rows.size(); ++i) {
    rowItems[i].label = rows[i].label.c_str();
    rowItems[i].subtitle = rows[i].subtitle.c_str();
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
  switch (rows[index].kind) {
    case Row::OpenProfile:
      slot = rows[index].slot;
      message.clear();
      activeNav().requestSelection(0);
      rebuildRows();
      break;
    case Row::SyncAll:
      syncMask = livesyncdev::ALL_PROFILES;
      withWifi(&LiveSyncActivity::syncAll);
      break;
    case Row::FinishSetup:
      askPassphrase();
      break;
    case Row::SyncNow:
      withWifi(&LiveSyncActivity::syncNow);
      break;
    case Row::Forget:
      livesyncdev::forget(slot);
      message = tr(STR_LIVESYNC_FORGOTTEN);
      openList();
      break;
    case Row::CertCheck:
      profile.checkCert = !profile.checkCert;
      livesyncdev::save(slot, profile);
      message.clear();
      rebuildRows();
      break;
    case Row::AutoSync: {
      // Off -> every 6 hours -> every 24 hours -> off.
      profile.autoHours = profile.autoHours == 0 ? 6 : profile.autoHours == 6 ? 24 : 0;
      livesyncdev::save(slot, profile);
      message.clear();
      rebuildRows();
      break;
    }
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
    unlockSetup();
  });
}

void LiveSyncActivity::dropStaged() {
  livesyncdev::wipeProfile(staged);
  staged = livesyncdev::Profile{};
  mbedtls_platform_zeroize(stagedE2ee.data(), stagedE2ee.size());
  stagedE2ee.clear();
  stagedPlan = livesync::FinishPlan{};
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
      dropStaged();
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

void LiveSyncActivity::unlockSetup() {
  std::string why;
  livesync::PendingSetup waiting;
  const auto& fs = livesyncdev::profileFs();
  dropStaged();
  progress.begin(renderer, tr(STR_LIVESYNC_UNLOCKING));
  bool ok = livesync::readPending(fs, slot, waiting) &&
            livesyncdev::profileFromSetup(waiting.uri, passphrase, staged, stagedE2ee, why);
  mbedtls_platform_zeroize(passphrase.data(), passphrase.size());
  passphrase.clear();
  mbedtls_platform_zeroize(waiting.uri.data(), waiting.uri.size());
  // The profile: this one, or the one that already syncs this vault (a Setup URI posted again
  // without a profile number), which keeps its folder and its sync state. The folder: the name
  // given with the Setup URI, else the one that profile has, else the database's name. Never
  // another profile's folder: two databases in one folder would overwrite each other's notes.
  const std::string identity = livesync::identityOf(staged);
  if (ok) {
    switch (livesync::planFinish(fs, slot, waiting.vault, identity, staged.database, stagedPlan)) {
      case livesync::SetupError::Ok:
        break;
      case livesync::SetupError::DuplicateVault: {
        char text[400];
        snprintf(text, sizeof(text), tr(STR_LIVESYNC_FOLDER_TAKEN),
                 (waiting.vault.empty() ? livesync::defaultVaultFolder(staged.database) : waiting.vault).c_str());
        why = text;
        ok = false;
        break;
      }
      default:
        ok = false;
        break;
    }
  }
  if (!ok) {
    dropStaged();
    showMessage(why.empty() ? std::string(tr(STR_LIVESYNC_SETUP_FAILED)) : why);
    return;
  }
  // A folder this profile didn't sync until now: the spelling the card already has for it, so
  // the notes, their index and the sync state are all filed under one name.
  if (stagedPlan.previousIdentity.empty()) stagedPlan.folder = livesyncdev::cardSpelling(stagedPlan.folder);
  staged.vaultName = stagedPlan.folder;
  // The same vault set up again keeps the settings made on this screen.
  livesyncdev::Profile old;
  if (livesyncdev::load(stagedPlan.slot, old) && livesync::identityOf(old) == identity) {
    staged.checkCert = old.checkCert;
    staged.autoHours = old.autoHours;
    staged.lastAttempt = old.lastAttempt;
  }
  livesyncdev::wipeProfile(old);
  // A folder that already holds files and wasn't synced with this vault by this profile: say what
  // the first sync does to them, before anything is fetched. Never a refusal.
  const bool sameVault = stagedPlan.previousIdentity == identity;
  if (!sameVault && livesyncdev::vaultFolderHasFiles(stagedPlan.folder)) {
    auto dialog = makeUniqueNoThrow<ConfirmationActivity>(
        renderer, mappedInput, std::string(vaultfiles::VAULTS_ROOT) + "/" + stagedPlan.folder,
        tr(STR_LIVESYNC_FOLDER_HAS_FILES));
    if (!dialog) {
      dropStaged();
      showMessage(tr(STR_LIVESYNC_SETUP_FAILED));
      return;
    }
    startActivityForResult(std::move(dialog), [this](const ActivityResult& result) {
      if (result.isCancelled) {  // the Setup URI keeps waiting
        dropStaged();
        message.clear();
        rebuildRows();
        return;
      }
      withWifi(&LiveSyncActivity::finishSetup);
    });
    return;
  }
  withWifi(&LiveSyncActivity::finishSetup);
}

void LiveSyncActivity::finishSetup() {
  std::string why;
  const auto& fs = livesyncdev::profileFs();
  if (stagedPlan.slot == 0) {  // nothing was unlocked
    showMessage(tr(STR_LIVESYNC_SETUP_FAILED));
    return;
  }
  progress.begin(renderer, tr(STR_LIVESYNC_UNLOCKING));
  livesyncdev::clearLastFailure();
  bool ok = livesyncdev::finishSetup(staged, stagedE2ee, why);
  livesyncdev::endSession();
  // The folder's sync state, before the profile is saved: kept when it is this same vault's (the
  // notes not sent yet stay), moved aside when it is another database's, whose unsent notes and
  // revisions must never reach this one.
  if (ok && livesyncdev::prepareSyncState(staged, stagedPlan.previousIdentity) == livesync::StateCheck::Failed) {
    why = tr(STR_LIVESYNC_STATE_STUCK);
    ok = false;
  }
  const int target = stagedPlan.slot;
  const bool saved = ok && livesyncdev::save(target, staged);
  dropStaged();
  if (!saved) {
    showMessage(why.empty() ? std::string(tr(STR_LIVESYNC_SETUP_FAILED)) : why);
    return;
  }
  fs.remove(livesync::pendingFile(slot));
  if (target != slot) {  // the waiting slot was only a second copy of the profile just updated
    slot = target;
    int used = 0;
    for (int s = 1; s <= livesyncdev::MAX_PROFILES; ++s) used += livesync::slotInfo(fs, s).inUse() ? 1 : 0;
    direct = used == 1;
    activeNav().requestSelection(0);
  }
  showMessage(tr(STR_LIVESYNC_READY));
}

namespace {
// Sets a flag for a scope: every return path clears it.
struct Busy {
  explicit Busy(bool& flag) : flag(flag) { flag = true; }
  ~Busy() { flag = false; }
  Busy(const Busy&) = delete;
  Busy& operator=(const Busy&) = delete;
  bool& flag;
};
}  // namespace

void LiveSyncActivity::syncNow() {
  const Busy busy(syncing);
  progress.begin(renderer, tr(STR_LIVESYNC_SYNCING));
  lastSyncOk = syncProfile(slot, profile);
  showMessage(profile.lastResult);
}

void LiveSyncActivity::syncAll() {
  const Busy busy(syncing);
  int total = 0, good = 0;
  std::string onlyResult;
  for (int s = 1; s <= livesyncdev::MAX_PROFILES; ++s) {
    if (!(syncMask & (1u << (s - 1))) || !livesyncdev::hasProfile(s)) continue;
    livesyncdev::Profile p;
    ++total;
    if (!livesyncdev::load(s, p)) {
      // A profile file that can't be read (a damaged file, a card from another reader): a
      // failure with its own line, not a vault quietly left out. The file is left as it is.
      LOG_ERR("LSY", "Profile %d could not be read", s);
      char text[160];
      snprintf(text, sizeof(text), tr(STR_LIVESYNC_PROFILE_UNREADABLE), s);
      onlyResult = text;
      continue;
    }
    progress.begin(renderer, (std::string(tr(STR_LIVESYNC_SYNCING)) + " " + p.vaultName).c_str());
    // The sync before this one closed its HTTPS session, but it may have left the heap in pieces:
    // a new TLS session needs one free block of this size (the limit the downloads use too).
    if (total > 1 && ESP.getMaxAllocHeap() < HttpDownloader::MIN_TLS_MAX_ALLOC) {
      LOG_ERR("LSY", "Profile %d not synced: largest free block %u", s, static_cast<unsigned>(ESP.getMaxAllocHeap()));
      p.lastResult = tr(STR_LIVESYNC_LOW_MEMORY);
      livesyncdev::save(s, p);
    } else if (syncProfile(s, p)) {
      ++good;
    }
    onlyResult = p.lastResult;
    livesyncdev::wipeProfile(p);
  }
  lastSyncOk = total > 0 && good == total;
  if (total == 1) {
    showMessage(onlyResult);
    return;
  }
  char summary[256];
  snprintf(summary, sizeof(summary), tr(STR_LIVESYNC_ALL_DONE), good, total);
  showMessage(summary);
}

bool LiveSyncActivity::syncProfile(const int profileSlot, livesyncdev::Profile& profile) {
  // The vault folder was deleted (or never written): the saved checkpoint would skip everything.
  if (!Storage.exists(livesyncdev::options(profile).vaultDir.c_str())) livesyncdev::resetSyncState(profile);
  livesync::SyncStats stats;
  livesyncdev::PushStats sent;
  std::string error;
  livesyncdev::clearLastFailure();  // the profile before this one may have failed
  // Notes edited here go up first, then the server's changes come down.
  bool ok = livesyncdev::pushChanges(profile, sent, error);
  const auto options = livesyncdev::options(profile);
  if (ok) {
    ok = livesync::syncOnce(livesyncdev::transport(profile), livesyncdev::storage(), profile.master, options, stats,
                            error, [](int done, const std::string& path) {
                              LOG_DBG("LSY", "%d %s", done, path.c_str());
                              progress.set(done % 100);
                            });
  }
  // A note that changed on both sides: the pull just saved the server's text beside it, so the
  // device's version (and that copy) can go up now.
  if (ok && sent.conflicts > 0) {
    livesyncdev::PushStats again;
    ok = livesyncdev::pushChanges(profile, again, error);
    sent.pushed += again.pushed;
  }
  livesyncdev::endSession();
  char summary[200];
  if (ok) {
    snprintf(summary, sizeof(summary), tr(STR_LIVESYNC_DONE), stats.written, stats.trashed);
    profile.lastResult = summary;
    if (sent.pushed > 0) {
      snprintf(summary, sizeof(summary), tr(STR_LIVESYNC_SENT), sent.pushed);
      profile.lastResult += summary;
      if (profile.idsV1()) profile.lastResult += tr(STR_LIVESYNC_IDS_V1);
    }
    if (!profile.canPush() && !vaultedit::changedList(options.vaultDir).empty()) {
      profile.lastResult += tr(STR_LIVESYNC_CANT_SEND);
    }
    if (stats.conflicts > 0) {
      snprintf(summary, sizeof(summary), tr(STR_LIVESYNC_CONFLICTS), stats.conflicts);
      profile.lastResult += summary;
    }
    if (stats.skipped > 0) {
      snprintf(summary, sizeof(summary), tr(STR_LIVESYNC_SKIPPED), stats.skipped,
               livesyncdev::skippedListPath(profile).c_str());
      profile.lastResult += summary;
    }
  } else {
    profile.lastResult = error;
    // A certificate the device can't vouch for looks like a dead connection: say what to try.
    if (livesyncdev::lastFailedUnverified()) profile.lastResult += std::string(" ") + tr(STR_LIVESYNC_CERT_HINT);
  }
  livesyncdev::save(profileSlot, profile);
  return ok;
}
