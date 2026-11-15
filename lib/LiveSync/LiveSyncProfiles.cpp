#include "LiveSyncProfiles.h"

#include <ArduinoJson.h>
#include <mbedtls/platform_util.h>

#include <cstring>

#include "LiveSyncEngine.h"
#include "LiveSyncIds.h"

namespace livesync {
namespace {

void wipe(std::string& s) {
  mbedtls_platform_zeroize(s.data(), s.size());
  s.clear();
}

void wipeSecrets(Profile& p) {
  wipe(p.password);
  mbedtls_platform_zeroize(p.master, sizeof(p.master));
}

char lower(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool looksLikeSetupUri(const std::string& uri) {
  return uri.size() <= MAX_SETUP_URI &&
         (uri.rfind("obsidian://setuplivesync?settings=", 0) == 0 || (uri.rfind("%$", 0) == 0 && uri.size() > 2));
}

}  // namespace

std::string profileToJson(const Profile& p, const Obfuscator& o) {
  JsonDocument doc;
  doc["server"] = p.serverUrl;
  doc["user"] = p.user;
  doc["password"] = o.hide(p.password);
  doc["database"] = p.database;
  doc["salt"] = p.saltB64;
  doc["key"] = o.hide(std::string(reinterpret_cast<const char*>(p.master), sizeof(p.master)));
  doc["vault"] = p.vaultName;
  doc["last"] = p.lastResult;
  doc["checkCert"] = p.checkCert;
  doc["autoHours"] = p.autoHours;
  doc["lastAttempt"] = p.lastAttempt;
  if (!p.idsV1() && p.canPush()) {
    doc["pushIdA"] = o.hide(p.hashedPass);
    doc["pushIdB"] = o.hide(p.passDigest);
  }
  if (p.idsV1()) {
    doc["pushIdK"] = o.hide(p.idKey);
    doc["pushNfc"] = p.nfcPaths;
  }
  std::string text;
  serializeJson(doc, text);
  return text;
}

bool profileFromJson(const std::string& text, const Obfuscator& o, Profile& out) {
  out = Profile{};
  JsonDocument doc;
  if (deserializeJson(doc, text) != DeserializationError::Ok) return false;
  out.serverUrl = doc["server"] | "";
  out.user = doc["user"] | "";
  out.database = doc["database"] | "";
  out.saltB64 = doc["salt"] | "";
  out.vaultName = doc["vault"] | "";
  out.lastResult = doc["last"] | "";
  out.checkCert = doc["checkCert"] | true;
  out.autoHours = doc["autoHours"] | 0;
  out.lastAttempt = doc["lastAttempt"] | static_cast<int64_t>(0);
  bool okPush = true;
  out.hashedPass = o.reveal(doc["pushIdA"] | "", &okPush);
  out.passDigest = o.reveal(doc["pushIdB"] | "", &okPush);
  if (!okPush) out.hashedPass.clear(), out.passDigest.clear();
  bool okIdKey = true;
  out.idKey = o.reveal(doc["pushIdK"] | "", &okIdKey);
  if (!okIdKey || !isIdKeyHex(out.idKey)) out.idKey.clear();
  out.nfcPaths = doc["pushNfc"] | false;
  bool ok = true, okKey = true;
  out.password = o.reveal(doc["password"] | "", &ok);
  std::string key = o.reveal(doc["key"] | "", &okKey);
  const bool valid =
      ok && okKey && key.size() == sizeof(out.master) && !out.serverUrl.empty() && !out.vaultName.empty();
  if (valid) memcpy(out.master, key.data(), sizeof(out.master));
  wipe(key);
  if (!valid) wipeSecrets(out);
  return valid;
}

std::string profileFile(const int slot) {
  return std::string(PROFILES_ROOT) + "/profiles/" + std::to_string(slot) + ".json";
}

std::string pendingFile(const int slot) {
  return std::string(PROFILES_ROOT) + "/profiles/" + std::to_string(slot) + ".setup.json";
}

bool validVaultFolder(std::string_view name) {
  if (name.empty() || name.size() > MAX_VAULT_FOLDER) return false;
  if (name.front() == '.' || name.front() == ' ' || name.back() == '.' || name.back() == ' ') return false;
  for (const unsigned char c : name) {
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
    const bool digit = c >= '0' && c <= '9';
    if (!letter && !digit && c != ' ' && c != '-' && c != '_' && c != '.') return false;
  }
  return true;
}

std::string defaultVaultFolder(const std::string& database) {
  return safeVaultPath(database) && database.find('/') == std::string::npos ? database : std::string("LiveSync");
}

bool sameFolder(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

void migrateLegacy(const ProfileFs& fs) {
  if (fs.exists(LEGACY_PROFILE) && !fs.exists(profileFile(1))) {
    // One rename: the profile is in one place or the other, never in neither.
    fs.rename(LEGACY_PROFILE, profileFile(1));
  }
  if (fs.exists(LEGACY_PENDING) && !fs.exists(pendingFile(1))) {
    PendingSetup pending;
    if (fs.readAll(LEGACY_PENDING, pending.uri)) {
      while (!pending.uri.empty() && (pending.uri.back() == '\n' || pending.uri.back() == '\r')) pending.uri.pop_back();
      if (writePending(fs, 1, pending)) fs.remove(LEGACY_PENDING);
    }
  }
}

bool readPending(const ProfileFs& fs, const int slot, PendingSetup& out) {
  out = PendingSetup{};
  std::string text;
  if (!validSlot(slot) || !fs.exists(pendingFile(slot)) || !fs.readAll(pendingFile(slot), text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text) != DeserializationError::Ok) return false;
  out.uri = doc["uri"] | "";
  out.vault = doc["vault"] | "";
  return !out.uri.empty();
}

bool writePending(const ProfileFs& fs, const int slot, const PendingSetup& pending) {
  if (!validSlot(slot)) return false;
  JsonDocument doc;
  doc["uri"] = pending.uri;
  doc["vault"] = pending.vault;
  std::string text;
  serializeJson(doc, text);
  return fs.writeAll(pendingFile(slot), text);
}

SlotInfo slotInfo(const ProfileFs& fs, const int slot) {
  SlotInfo info;
  if (!validSlot(slot)) return info;
  std::string text;
  if (fs.exists(profileFile(slot)) && fs.readAll(profileFile(slot), text)) {
    JsonDocument doc;
    if (deserializeJson(doc, text) == DeserializationError::Ok) {
      info.vault = doc["vault"] | "";
      info.lastResult = doc["last"] | "";
      info.lastAttempt = doc["lastAttempt"] | static_cast<int64_t>(0);
      info.autoHours = doc["autoHours"] | 0;
      info.configured = !info.vault.empty() && !std::string(doc["server"] | "").empty();
    }
    wipe(text);  // held the obfuscated password and key
  }
  PendingSetup pending;
  if (readPending(fs, slot, pending)) {
    info.pending = true;
    if (!info.configured) info.vault = pending.vault;
  }
  return info;
}

int nextFreeSlot(const ProfileFs& fs) {
  for (int slot = 1; slot <= MAX_PROFILES; ++slot) {
    if (!fs.exists(profileFile(slot)) && !fs.exists(pendingFile(slot))) return slot;
  }
  return 0;
}

bool folderTaken(const ProfileFs& fs, std::string_view folder, const int exceptSlot) {
  for (int slot = 1; slot <= MAX_PROFILES; ++slot) {
    if (slot == exceptSlot) continue;
    const SlotInfo info = slotInfo(fs, slot);
    if (info.configured && sameFolder(info.vault, folder)) return true;
    // A setup still waiting has reserved the folder it asked for.
    PendingSetup pending;
    if (info.pending && readPending(fs, slot, pending) && !pending.vault.empty() && sameFolder(pending.vault, folder)) {
      return true;
    }
  }
  return false;
}

void removeSlot(const ProfileFs& fs, const int slot) {
  if (!validSlot(slot)) return;
  fs.remove(profileFile(slot));
  fs.remove(pendingFile(slot));
}

const char* setupErrorText(const SetupError error) {
  switch (error) {
    case SetupError::Ok:
      return "OK";
    case SetupError::BadJson:
      return "Invalid JSON";
    case SetupError::NotSetupUri:
      return "Not a LiveSync Setup URI";
    case SetupError::BadVault:
      return "Vault folder name: up to 64 letters, digits, spaces, '-', '_' or '.', no dot or space at either end";
    case SetupError::BadProfile:
      return "Profile must be 1 to 4";
    case SetupError::Full:
      return "All 4 LiveSync profiles are in use: remove one on the device first";
    case SetupError::DuplicateVault:
      return "Another LiveSync profile already uses this vault folder";
    case SetupError::WriteFailed:
      return "Could not write to the SD card";
  }
  return "";
}

SetupError parseSetupBody(const std::string& body, SetupRequest& out) {
  out = SetupRequest{};
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok || !doc.is<JsonObject>()) return SetupError::BadJson;
  out.uri = doc["uri"] | "";
  if (!looksLikeSetupUri(out.uri)) return SetupError::NotSetupUri;
  if (!doc["vault"].isNull()) {
    if (!doc["vault"].is<const char*>()) return SetupError::BadVault;
    out.vault = doc["vault"].as<const char*>();
    if (!out.vault.empty() && !validVaultFolder(out.vault)) return SetupError::BadVault;
  }
  if (!doc["profile"].isNull()) {
    if (!doc["profile"].is<int>() || !validSlot(doc["profile"].as<int>())) return SetupError::BadProfile;
    out.profile = doc["profile"].as<int>();
  }
  return SetupError::Ok;
}

SetupError acceptSetup(const ProfileFs& fs, const SetupRequest& request, int& slot) {
  slot = 0;
  if (!looksLikeSetupUri(request.uri)) return SetupError::NotSetupUri;
  if (!request.vault.empty() && !validVaultFolder(request.vault)) return SetupError::BadVault;
  if (request.profile != 0 && !validSlot(request.profile)) return SetupError::BadProfile;
  int target = request.profile;
  for (int s = 1; target == 0 && s <= MAX_PROFILES; ++s) {
    PendingSetup waiting;
    if (readPending(fs, s, waiting) && waiting.uri == request.uri) target = s;
  }
  if (target == 0) target = nextFreeSlot(fs);
  if (target == 0) return SetupError::Full;
  if (!request.vault.empty() && folderTaken(fs, request.vault, target)) return SetupError::DuplicateVault;
  if (!writePending(fs, target, PendingSetup{request.uri, request.vault})) return SetupError::WriteFailed;
  slot = target;
  return SetupError::Ok;
}

SetupError resolveVaultFolder(const ProfileFs& fs, const int slot, const std::string& asked,
                              const std::string& database, std::string& folder) {
  folder.clear();
  if (!validSlot(slot)) return SetupError::BadProfile;
  std::string name = asked;
  if (!name.empty() && !validVaultFolder(name)) return SetupError::BadVault;
  if (name.empty()) {
    // Setting the same database up again keeps its folder (a custom name isn't lost).
    std::string text;
    JsonDocument doc;
    if (fs.exists(profileFile(slot)) && fs.readAll(profileFile(slot), text) &&
        deserializeJson(doc, text) == DeserializationError::Ok && database == (doc["database"] | "")) {
      name = doc["vault"] | "";
    }
    wipe(text);
  }
  if (name.empty()) name = defaultVaultFolder(database);
  if (folderTaken(fs, name, slot)) return SetupError::DuplicateVault;
  folder = name;
  return SetupError::Ok;
}

}  // namespace livesync
