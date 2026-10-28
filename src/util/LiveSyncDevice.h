#pragma once

#include <LiveSyncEngine.h>

#include <string>

// LiveSync on the device: the saved profile (one LiveSync database pulled into one vault), the
// pending Setup URI from the web page, and the SD card and HTTPS adapters for the pull engine.
namespace livesyncdev {

constexpr const char* DIR = "/.vaultink/livesync";
constexpr const char* PROFILE = "/.vaultink/livesync/profile.json";
constexpr const char* PENDING_SETUP = "/.vaultink/livesync/setup-uri.txt";  // still encrypted
constexpr const char* CA_CERT = "/.vaultink/livesync/ca.pem";               // optional trust anchor

struct Profile {
  std::string serverUrl;  // couchDB_URI, no trailing slash
  std::string user;
  std::string password;
  std::string database;
  std::string saltB64;  // pbkdf2salt the master key belongs to
  livesync::Key master{};
  std::string vaultName;    // folder under /Vaults
  std::string lastResult;   // shown on the LiveSync screen
  bool checkCert = true;    // verify the server certificate (ca.pem if present, else the built-in roots)
  uint8_t autoHours = 0;    // sync on wake when this many hours passed since the last try; 0 = off
  int64_t lastAttempt = 0;  // epoch seconds of the last automatic try
  // Sending changes back (Phase 4c): ids come from the E2EE passphrase, which isn't kept; these
  // two values derived from it are (obfuscated like the password). Empty: set up before 0.28.0,
  // or a vault whose settings the device can't write for (no path obfuscation, new id scheme).
  std::string hashedPass;
  std::string passDigest;
  bool pushAllowed = false;  // set by profileFromSetup from the vault's settings (not saved)
  bool canPush() const { return !hashedPass.empty() && !passDigest.empty(); }
};

struct PushStats {
  int pushed = 0;
  int conflicts = 0;  // the server changed too: the pull keeps both versions
  int skipped = 0;
};

// Sends every note in the vault's changed list (the ones edited on the device) before a pull.
// Notes sent leave the list; a conflict stays until the pull has saved the server's version.
bool pushChanges(Profile& profile, PushStats& stats, std::string& error);

// Automatic sync is on and due (a trusted clock, the interval passed since the last try). Records
// this try first, so a sync that hangs or fails waits for the next interval.
bool autoSyncDue();

// The saved profile; secrets (password, master key) are obfuscated with the device key on the SD
// card. That only stops a copy of the card working elsewhere; it isn't encryption.
bool load(Profile& out);
// A saved profile exists (cheap: no secrets read).
bool hasProfile();
bool save(const Profile& profile);
void forget();

bool hasPendingSetup();
bool readPendingSetup(std::string& uri);
bool savePendingSetup(const std::string& uri);
void clearPendingSetup();

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
