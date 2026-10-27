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
  std::string vaultName;   // folder under /Vaults
  std::string lastResult;  // shown on the LiveSync screen
};

// The saved profile; secrets (password, master key) are obfuscated with the device key on the SD
// card. That only stops a copy of the card working elsewhere; it isn't encryption.
bool load(Profile& out);
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

// True when the server certificate is checked against CA_CERT (false: encrypted but unchecked).
bool certificateChecked();

// Starts the next sync from the beginning (drops the checkpoint and the id->path map).
void resetSyncState(const Profile& profile);

// Closes the HTTPS session the transport keeps between requests (after a sync or a setup).
void endSession();

// Wipes the profile's secrets in RAM.
void wipeProfile(Profile& profile);

}  // namespace livesyncdev
