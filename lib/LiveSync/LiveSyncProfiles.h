#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "LiveSyncCrypto.h"

// The LiveSync profiles of one reader: up to four, each one CouchDB database pulled into its own
// folder under /Vaults. Files (all under /.vaultink/livesync):
//   profiles/<n>.json        the saved profile n (1..4), secrets obfuscated by the caller
//   profiles/<n>.setup.json  a Setup URI posted for profile n, still encrypted, with the folder name asked for
//   profile.json             the single profile of firmware before profiles: moved to profiles/1.json
//   setup-uri.txt            its pending Setup URI: becomes profiles/1.setup.json
// The SD card comes in through ProfileFs, so the whole store runs in host tests.
namespace livesync {

constexpr int MAX_PROFILES = 4;
constexpr size_t MAX_VAULT_FOLDER = 64;  // bytes
constexpr size_t MAX_SETUP_URI = 32 * 1024;
constexpr const char* PROFILES_ROOT = "/.vaultink/livesync";
constexpr const char* LEGACY_PROFILE = "/.vaultink/livesync/profile.json";
constexpr const char* LEGACY_PENDING = "/.vaultink/livesync/setup-uri.txt";

struct Profile {
  std::string serverUrl;  // couchDB_URI, no trailing slash
  std::string user;
  std::string password;
  std::string database;
  std::string saltB64;  // pbkdf2salt the master key belongs to
  Key master{};
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
  // Id scheme 1 (idDerivationVersion 1): the 64-hex id key from the Setup URI, obfuscated when
  // saved, instead of the two values above; nfcPaths = pathNormalizationVersion 1.
  std::string idKey;
  bool nfcPaths = false;
  bool pushAllowed = false;  // set from the vault's settings at setup (not saved)
  bool idsV1() const { return !idKey.empty(); }
  bool canPush() const { return idsV1() || (!hashedPass.empty() && !passDigest.empty()); }
};

// How the caller hides a secret in the profile file (the device: its hardware key, then base64).
struct Obfuscator {
  std::function<std::string(const std::string& plain)> hide;
  // Sets *ok to false when `encoded` can't be read back.
  std::function<std::string(const char* encoded, bool* ok)> reveal;
};

std::string profileToJson(const Profile& profile, const Obfuscator& obfuscator);
// Also reads a profile.json written before profiles (the same keys). False when it has no server,
// no folder or a key that can't be read; `out` is then wiped.
bool profileFromJson(const std::string& text, const Obfuscator& obfuscator, Profile& out);

struct ProfileFs {
  std::function<bool(const std::string& path)> exists;
  std::function<bool(const std::string& path, std::string& out)> readAll;
  std::function<bool(const std::string& path, const std::string& text)> writeAll;  // makes folders, replaces
  std::function<bool(const std::string& path)> remove;
  std::function<bool(const std::string& from, const std::string& to)> rename;  // makes folders
};

std::string profileFile(int slot);
std::string pendingFile(int slot);
inline bool validSlot(const int slot) { return slot >= 1 && slot <= MAX_PROFILES; }

// A folder name a new setup may ask for under /Vaults: 1..64 bytes of letters (non-Latin ones
// too), digits, space, '-', '_' and '.'; no '/', not "." or "..", no dot or space at either end
// (a FAT card drops those at the end, and a leading dot hides the folder).
bool validVaultFolder(std::string_view name);
// The folder when none was asked for: the database name, as before profiles ("LiveSync" when the
// card can't hold that name).
std::string defaultVaultFolder(const std::string& database);
// Folder names that are one folder on the card (FAT ignores the case of A-Z).
bool sameFolder(std::string_view a, std::string_view b);

// Moves the files of firmware before profiles to profile 1. Nothing is deleted before its copy
// exists; when profile 1 is already there the old files are left alone. Safe to call every time.
void migrateLegacy(const ProfileFs& fs);

struct PendingSetup {
  std::string uri;    // still encrypted
  std::string vault;  // the folder asked for; empty = the default
};
bool readPending(const ProfileFs& fs, int slot, PendingSetup& out);
bool writePending(const ProfileFs& fs, int slot, const PendingSetup& pending);

// What a list of profiles shows; no secret is decoded for it.
struct SlotInfo {
  bool configured = false;  // a saved profile
  bool pending = false;     // a Setup URI waiting for its passphrase
  std::string vault;        // the saved profile's folder, else the folder asked for
  std::string lastResult;
  int64_t lastAttempt = 0;
  uint8_t autoHours = 0;
  bool inUse() const { return configured || pending; }
};
SlotInfo slotInfo(const ProfileFs& fs, int slot);

// The first slot with neither a profile nor a pending setup; 0 when all four are taken.
int nextFreeSlot(const ProfileFs& fs);
// Another slot (not `exceptSlot`) already syncs into, or waits to sync into, this folder.
bool folderTaken(const ProfileFs& fs, std::string_view folder, int exceptSlot);
void removeSlot(const ProfileFs& fs, int slot);

enum class SetupError : uint8_t {
  Ok,
  BadJson,
  NotSetupUri,
  BadVault,        // a folder name the card can't hold
  BadProfile,      // not 1..4
  Full,            // four profiles already
  DuplicateVault,  // another profile uses the folder
  WriteFailed,
};
const char* setupErrorText(SetupError error);

// The body of POST /api/livesync/setup: {"uri": "...", "vault": "Folder" (optional), "profile": n (optional)}.
struct SetupRequest {
  std::string uri;
  std::string vault;
  int profile = 0;  // 0 = not given
};
SetupError parseSetupBody(const std::string& body, SetupRequest& out);

// Saves the request as a pending setup. Without a profile number it takes the slot that already
// waits with the same URI (posting twice doesn't use two slots), else the next free one. `slot`
// gets the profile it went to.
SetupError acceptSetup(const ProfileFs& fs, const SetupRequest& request, int& slot);

// The folder a setup being finished syncs into: the one asked for; else, when the slot already
// syncs this database, the folder it has; else the database name. DuplicateVault when another
// profile has that folder.
SetupError resolveVaultFolder(const ProfileFs& fs, int slot, const std::string& asked, const std::string& database,
                              std::string& folder);

}  // namespace livesync
