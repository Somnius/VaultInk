#pragma once

#include <LiveSyncEngine.h>
#include <LiveSyncProfiles.h>

#include <string>

// LiveSync on the device: the saved profiles (each one LiveSync database pulled into one vault), the
// pending Setup URIs from the web page, and the SD card and HTTPS adapters for the pull engine.
namespace livesyncdev {

constexpr const char* DIR = "/.vaultink/livesync";
constexpr const char* CA_CERT = "/.vaultink/livesync/ca.pem";  // optional trust anchor, shared by all profiles
constexpr int MAX_PROFILES = livesync::MAX_PROFILES;           // slots 1..4
constexpr uint8_t ALL_PROFILES = 0x0F;                         // slot n is bit n-1

// One LiveSync database pulled into one folder under /Vaults (lib/LiveSync/LiveSyncProfiles.h).
using Profile = livesync::Profile;

struct PushStats {
  int pushed = 0;
  int conflicts = 0;  // the server changed too: the pull keeps both versions
  int skipped = 0;
};

// Sends every note in the vault's changed list (the ones edited on the device) before a pull.
// Notes sent leave the list; a conflict stays until the pull has saved the server's version.
bool pushChanges(Profile& profile, PushStats& stats, std::string& error);

// The profiles to sync on this wake, as a bit mask (slot n is bit n-1; 0 = none): every saved
// profile when the companion asked with the sync-request file, else those with automatic sync on
// and due (a trusted clock, the interval passed since the last try). Records the try first, so a
// sync that hangs or fails waits for the next interval.
uint8_t autoSyncDue();

// The profile files on the SD card. The first call moves a profile.json from before profiles to
// profile 1 (lib/LiveSync/LiveSyncProfiles.h).
const livesync::ProfileFs& profileFs();

// A saved profile; secrets (password, master key) are obfuscated with the device key on the SD
// card. That only stops a copy of the card working elsewhere; it isn't encryption.
bool load(int slot, Profile& out);
// A saved profile exists (cheap: no secrets read); without a slot, in any of them.
bool hasProfile(int slot);
bool hasProfile();
bool save(int slot, const Profile& profile);
// Removes the profile and its pending setup. The vault's files and sync state stay on the card.
void forget(int slot);

// Decodes the Setup URI with its passphrase into a profile (no network). `why` is shown on failure.
bool profileFromSetup(const std::string& uri, const std::string& passphrase, Profile& out, std::string& e2eePassphrase,
                      std::string& why);
// Fetches the database's salt and derives the master key (310,000 PBKDF2 rounds: seconds).
bool finishSetup(Profile& profile, const std::string& e2eePassphrase, std::string& why);

livesync::Transport transport(const Profile& profile);
livesync::FileStore storage();
livesync::SyncOptions options(const Profile& profile);

// True when /.vaultink/livesync/ca.pem replaces the built-in roots for checking the server.
bool hasOwnCaCert();

// True when the last request failed before any HTTP answer while the certificate was being
// checked (a certificate the roots don't vouch for, a wrong name, or no network).
bool lastFailedUnverified();

// Starts the next sync from the beginning (drops the checkpoint and the id->path map).
void resetSyncState(const Profile& profile);

// Closes the HTTPS session the transport keeps between requests (after a sync or a setup).
void endSession();

// Wipes the profile's secrets in RAM.
void wipeProfile(Profile& profile);

}  // namespace livesyncdev
