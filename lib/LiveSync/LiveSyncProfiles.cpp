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

// The lower-case form of a letter below U+0800 (simple one-to-one folding).
uint32_t foldLetter(const uint32_t c) {
  if (c < 0x80) return c >= 'A' && c <= 'Z' ? c + 0x20 : c;
  if (c >= 0xC0 && c <= 0xDE) return c == 0xD7 ? c : c + 0x20;  // Latin-1 (not the times sign)
  if (c >= 0x100 && c <= 0x17F) {                               // Latin Extended-A: pairs
    if (c == 0x130 || c == 0x131 || c == 0x138 || c == 0x149 || c == 0x17F) return c;
    if (c == 0x178) return 0xFF;
    const bool upperIsOdd = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
    return ((c & 1) != 0) == upperIsOdd ? c + 1 : c;
  }
  if (c == 0x386) return 0x3AC;  // Greek capitals with tonos
  if (c >= 0x388 && c <= 0x38A) return c + 0x25;
  if (c == 0x38C) return 0x3CC;
  if (c == 0x38E || c == 0x38F) return c + 0x3F;
  if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 0x20;
  if (c == 0x3C2) return 0x3C3;                   // final sigma: both are capital sigma on the card
  if (c >= 0x400 && c <= 0x40F) return c + 0x50;  // Cyrillic
  if (c >= 0x410 && c <= 0x42F) return c + 0x20;
  if ((c >= 0x460 && c <= 0x481) || (c >= 0x48A && c <= 0x4BF) || (c >= 0x4D0 && c <= 0x52F)) {
    return (c & 1) != 0 ? c : c + 1;
  }
  if (c == 0x4C0) return 0x4CF;
  if (c >= 0x4C1 && c <= 0x4CE) return (c & 1) != 0 ? c + 1 : c;
  return c;
}

std::string trimmed(std::string text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
  return text;
}

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

// Names the card (and a PC reading it) reserves: CON, PRN, AUX, NUL, COM1-9, LPT1-9, also with an extension.
bool reservedFolderName(std::string_view name) {
  std::string base(name.substr(0, name.find('.')));
  for (char& c : base) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
  if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return true;
  return base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) && base[3] >= '1' &&
         base[3] <= '9';
}

bool validVaultFolder(std::string_view name) {
  if (name.empty() || name.size() > MAX_VAULT_FOLDER) return false;
  if (name.front() == '.' || name.front() == ' ' || name.back() == '.' || name.back() == ' ') return false;
  for (const unsigned char c : name) {
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80;
    const bool digit = c >= '0' && c <= '9';
    if (!letter && !digit && c != ' ' && c != '-' && c != '_' && c != '.') return false;
  }
  if (reservedFolderName(name)) return false;
  return true;
}

std::string defaultVaultFolder(const std::string& database) {
  // The database name when it is a name the card keeps as typed; else "LiveSync" (a trailing dot or space, or a
  // reserved name, would be changed by the card and could land two profiles in one folder).
  const bool plain =
      !database.empty() && database.back() != '.' && database.back() != ' ' && !reservedFolderName(database);
  return safeVaultPath(database) && database.find('/') == std::string::npos && plain ? database
                                                                                     : std::string("LiveSync");
}

std::string foldFolderName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  for (size_t i = 0; i < name.size(); ++i) {
    const auto b = static_cast<unsigned char>(name[i]);
    const unsigned char next = i + 1 < name.size() ? static_cast<unsigned char>(name[i + 1]) : 0;
    if (b >= 0xC2 && b <= 0xDF && (next & 0xC0) == 0x80) {  // a two-byte letter: U+0080..U+07FF
      const uint32_t folded = foldLetter((static_cast<uint32_t>(b & 0x1F) << 6) | (next & 0x3F));
      out += static_cast<char>(0xC0 | (folded >> 6));
      out += static_cast<char>(0x80 | (folded & 0x3F));
      ++i;
    } else {
      out += static_cast<char>(b < 0x80 ? foldLetter(b) : b);
    }
  }
  return out;
}

bool sameFolder(std::string_view a, std::string_view b) {
  return a.size() == b.size() && foldFolderName(a) == foldFolderName(b);
}

bool migrateLegacy(const ProfileFs& fs) {
  bool done = true;
  if (fs.exists(LEGACY_PROFILE) && !fs.exists(profileFile(1))) {
    // One rename: the profile is in one place or the other, never in neither.
    if (!fs.rename(LEGACY_PROFILE, profileFile(1))) done = false;
  }
  if (fs.exists(LEGACY_PENDING) && !fs.exists(pendingFile(1))) {
    PendingSetup pending;
    if (!fs.readAll(LEGACY_PENDING, pending.uri)) {
      done = false;
    } else {
      while (!pending.uri.empty() && (pending.uri.back() == '\n' || pending.uri.back() == '\r')) pending.uri.pop_back();
      if (!writePending(fs, 1, pending)) {
        done = false;
      } else {
        fs.remove(LEGACY_PENDING);
      }
    }
  }
  return done;
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

std::string identityOf(const std::string& serverUrl, const std::string& database, const std::string& user) {
  return serverUrl + "\n" + database + "\n" + user;
}

SlotInfo slotInfo(const ProfileFs& fs, const int slot, const Obfuscator* secrets) {
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
      const std::string server = doc["server"] | "";
      info.configured = !info.vault.empty() && !server.empty();
      if (info.configured) info.identity = identityOf(server, doc["database"] | "", doc["user"] | "");
    }
    if (secrets && info.configured) {
      Profile profile;
      info.canPush = profileFromJson(text, *secrets, profile) && profile.canPush();
      wipeSecrets(profile);
    }
    wipe(text);  // held the obfuscated password and key
  }
  PendingSetup pending;
  if (readPending(fs, slot, pending)) {
    info.pending = true;
    info.pendingVault = pending.vault;
    if (!info.configured) info.vault = pending.vault;
  }
  return info;
}

int slotWithIdentity(const ProfileFs& fs, const std::string& identity, const int exceptSlot) {
  for (int slot = 1; slot <= MAX_PROFILES; ++slot) {
    if (slot == exceptSlot) continue;
    const SlotInfo info = slotInfo(fs, slot);
    if (info.configured && info.identity == identity) return slot;
  }
  return 0;
}

int nextFreeSlot(const ProfileFs& fs) {
  for (int slot = 1; slot <= MAX_PROFILES; ++slot) {
    if (!fs.exists(profileFile(slot)) && !fs.exists(pendingFile(slot))) return slot;
  }
  return 0;
}

bool folderTaken(const ProfileFs& fs, std::string_view folder, const int exceptSlot, const int alsoExcept) {
  for (int slot = 1; slot <= MAX_PROFILES; ++slot) {
    if (slot == exceptSlot || slot == alsoExcept) continue;
    const SlotInfo info = slotInfo(fs, slot);  // each file is read once
    if (info.configured && sameFolder(info.vault, folder)) return true;
    // A setup still waiting has reserved the folder it asked for.
    if (info.pending && !info.pendingVault.empty() && sameFolder(info.pendingVault, folder)) return true;
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
                              const std::string& database, std::string& folder, const int alsoExcept) {
  folder.clear();
  if (!validSlot(slot)) return SetupError::BadProfile;
  std::string name = asked;
  if (!name.empty() && !validVaultFolder(name)) return SetupError::BadVault;
  std::string savedFolder, savedDatabase;
  {
    std::string text;
    JsonDocument doc;
    if (fs.exists(profileFile(slot)) && fs.readAll(profileFile(slot), text) &&
        deserializeJson(doc, text) == DeserializationError::Ok) {
      savedFolder = doc["vault"] | "";
      savedDatabase = doc["database"] | "";
    }
    wipe(text);
  }
  // Setting the same database up again keeps its folder (a custom name isn't lost).
  if (name.empty() && database == savedDatabase) name = savedFolder;
  if (name.empty()) name = defaultVaultFolder(database);
  // The same folder typed with other capitals: the saved spelling, so its sync state (filed under
  // the exact name) and the notes not sent yet stay with it.
  if (!savedFolder.empty() && sameFolder(savedFolder, name)) name = savedFolder;
  if (folderTaken(fs, name, slot, alsoExcept)) return SetupError::DuplicateVault;
  folder = name;
  return SetupError::Ok;
}

SetupError planFinish(const ProfileFs& fs, const int pendingSlot, const std::string& asked, const std::string& identity,
                      const std::string& database, FinishPlan& plan) {
  plan = FinishPlan{};
  if (!validSlot(pendingSlot)) return SetupError::BadProfile;
  int target = pendingSlot;
  if (!slotInfo(fs, pendingSlot).configured) {
    const int same = slotWithIdentity(fs, identity, pendingSlot);
    if (same != 0) target = same;
  }
  std::string folder;
  const SetupError error =
      resolveVaultFolder(fs, target, asked, database, folder, target == pendingSlot ? 0 : pendingSlot);
  if (error != SetupError::Ok) return error;
  const SlotInfo saved = slotInfo(fs, target);
  plan.slot = target;
  plan.folder = folder;
  if (saved.configured && saved.vault == folder) plan.previousIdentity = saved.identity;
  return SetupError::Ok;
}

StateCheck prepareSyncState(const ProfileFs& fs, const std::string& stateBase, const std::string& identity,
                            const std::string& previousIdentity) {
  const std::string live = stateBase + "/livesync";
  const std::string marker = live + "/identity";
  std::string known;
  const bool hasMarker = fs.exists(marker) && fs.readAll(marker, known);
  known = hasMarker ? trimmed(known) : previousIdentity;
  const bool hasState =
      fs.exists(live + "/checkpoint") || fs.exists(live + "/files.tsv") || fs.exists(live + "/revs.tsv");
  if (!identity.empty() && known == identity) {
    // The same vault set up again: the notes not sent yet and their revisions stay. Only the
    // pull starts from the beginning (the checkpoint and the id->path map).
    if (!fs.remove(live + "/checkpoint") || !fs.remove(live + "/files.tsv")) return StateCheck::Failed;
    if (!hasMarker && !fs.writeAll(marker, identity)) return StateCheck::Failed;
    return StateCheck::Kept;
  }
  if (known.empty() && !hasState) {
    return fs.writeAll(marker, identity) ? StateCheck::Fresh : StateCheck::Failed;
  }
  // Another vault's state. The list of notes not sent goes first and the identity last: after a
  // power cut in between, the next try still finds the old identity and finishes the job.
  std::string aside;
  for (int n = 1; n <= 999 && aside.empty(); ++n) {
    std::string candidate = stateBase + "/livesync-old-" + std::to_string(n);
    if (!fs.exists(candidate)) aside = std::move(candidate);
  }
  if (aside.empty()) return StateCheck::Failed;
  const auto setAside = [&fs, &aside](const std::string& from, const char* name) {
    if (!fs.exists(from)) return true;
    if (fs.rename(from, aside + "/" + name)) return true;
    return fs.remove(from) && !fs.exists(from);
  };
  if (!setAside(stateBase + "/changed.txt", "changed.txt") || !setAside(live + "/revs.tsv", "revs.tsv") ||
      !setAside(live + "/files.tsv", "files.tsv") || !setAside(live + "/checkpoint", "checkpoint")) {
    return StateCheck::Failed;
  }
  // The trash holds whole notes: moved, never deleted (left where it is when the move fails).
  if (fs.exists(live + "/trash")) fs.rename(live + "/trash", aside + "/trash");
  if (!setAside(marker, "identity")) return StateCheck::Failed;
  return fs.writeAll(marker, identity) ? StateCheck::SetAside : StateCheck::Failed;
}

void rememberIdentity(const ProfileFs& fs, const std::string& stateBase, const std::string& identity) {
  const std::string marker = stateBase + "/livesync/identity";
  if (!identity.empty() && !fs.exists(marker)) fs.writeAll(marker, identity);
}

}  // namespace livesync
