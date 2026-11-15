#include "LiveSyncDevice.h"

#include <ArduinoJson.h>
#include <CaBundle.h>
#include <HalStorage.h>
#include <LiveSyncIds.h>
#include <LiveSyncPush.h>
#include <LiveSyncSetup.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <SecureHttpClient.h>
#include <TrustedTime.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/platform_util.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <memory>
#include <string_view>

#include "util/TaskWatchdog.h"
#include "util/VaultEdit.h"
#include "util/VaultFiles.h"

namespace livesyncdev {
namespace {

constexpr size_t MAX_PROFILE = 8 * 1024;
constexpr size_t MAX_PENDING = 48 * 1024;           // a Setup URI (32 KB at most) as JSON
constexpr size_t MAX_STATE_FILE = 2 * 1024 * 1024;  // the id->path map of a big vault
// One CouchDB answer (a page of changes, a batch of docs or chunks); lands in PSRAM. The engine
// parses it into a JSON document of about the same size and then decrypts a chunk out of that, so
// the cap is a quarter of the PSRAM; the engine sizes its chunk batches for 1.5 MB.
constexpr size_t MAX_RESPONSE = 2 * 1024 * 1024;

void wipe(std::string& s) {
  mbedtls_platform_zeroize(s.data(), s.size());
  s.clear();
}

std::string urlEncode(const std::string& s) {
  static constexpr char hexd[] = "0123456789ABCDEF";
  std::string out;
  for (const unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hexd[c >> 4];
      out += hexd[c & 15];
    }
  }
  return out;
}

// Writes `text` to `path` straight from the std::string (no Arduino String copy).
bool writeText(const std::string& path, const std::string& text) {
  HalFile f;
  if (!Storage.openFileForWrite("LSY", path, f)) return false;
  const bool ok = f.write(text.data(), text.size()) == text.size();
  f.flush();
  f.close();
  return ok;
}

std::string parentOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
}

livesync::Obfuscator obfuscator() {
  livesync::Obfuscator o;
  o.hide = [](const std::string& plain) { return std::string(obfuscation::obfuscateToBase64(plain).c_str()); };
  o.reveal = [](const char* encoded, bool* ok) { return obfuscation::deobfuscateFromBase64(encoded, ok); };
  return o;
}

std::string b64(const uint8_t* data, size_t len) { return livesync::base64Encode(data, len); }

// The file being streamed by Storage::begin/append/commit.
struct Writer {
  HalFile file;
  std::string path;
  std::string tmp;
};
Writer writer;

// One TLS session for the whole sync: repeated handshakes fragment the heap.
std::unique_ptr<freeink::SecureHttpClient> session;
bool failedUnverified = false;
std::string caPem;
livesync::ResponseInfo lastInfo;  // of the last answer, for the engine's error log

}  // namespace

void wipeProfile(Profile& p) {
  wipe(p.password);
  mbedtls_platform_zeroize(p.master, sizeof(p.master));
}

const livesync::ProfileFs& profileFs() {
  static const livesync::ProfileFs fs = [] {
    livesync::ProfileFs f;
    f.exists = [](const std::string& p) { return Storage.exists(p.c_str()); };
    f.readAll = [](const std::string& p, std::string& out) {
      return Storage.readFileToString("LSY", p, MAX_PENDING, out);
    };
    f.writeAll = [](const std::string& p, const std::string& text) {
      Storage.ensureDirectoryExists(parentOf(p).c_str());
      const std::string tmp = p + ".tmp";
      return writeText(tmp, text) && Storage.replaceFile(tmp.c_str(), p.c_str());
    };
    f.remove = [](const std::string& p) { return !Storage.exists(p.c_str()) || Storage.remove(p.c_str()); };
    f.rename = [](const std::string& from, const std::string& to) {
      Storage.ensureDirectoryExists(parentOf(to).c_str());
      return Storage.rename(from.c_str(), to.c_str());
    };
    return f;
  }();
  // Until it is done: a move that failed (a card error) is tried again on the next call, not
  // only after the next boot.
  static bool migrated = false;
  static bool failureLogged = false;
  if (!migrated) {
    migrated = livesync::migrateLegacy(fs);
    if (!migrated && !failureLogged) {
      failureLogged = true;
      LOG_ERR("LSY", "Could not move the old LiveSync profile to profiles/1 (tried again on the next use)");
    }
  }
  return fs;
}

namespace {
std::string stateBaseOf(const std::string& folder) {
  const std::string index = vaultfiles::indexPathFor(std::string(vaultfiles::VAULTS_ROOT) + "/" + folder);
  return index.substr(0, index.find_last_of('/'));
}
}  // namespace

livesync::SlotInfo slotInfo(const int slot, const bool withCanPush) {
  if (!withCanPush) return livesync::slotInfo(profileFs(), slot);
  const livesync::Obfuscator secrets = obfuscator();
  return livesync::slotInfo(profileFs(), slot, &secrets);
}

bool vaultFolderHasFiles(const std::string& folder) {
  if (folder.empty()) return false;
  HalFile dir = Storage.open((std::string(vaultfiles::VAULTS_ROOT) + "/" + folder).c_str());
  if (!dir || !dir.isDirectory()) return false;
  HalFile first = dir.openNextFile();
  const bool any = static_cast<bool>(first);
  if (first) first.close();
  dir.close();
  return any;
}

std::string cardSpelling(const std::string& folder) {
  HalFile root = Storage.open(vaultfiles::VAULTS_ROOT);
  if (!root || !root.isDirectory()) return folder;
  std::string found = folder;
  char name[256];
  for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (!entry.isDirectory()) continue;
    name[0] = '\0';
    entry.getName(name, sizeof(name));
    if (name == folder) {  // the exact name is there: nothing to choose
      found = folder;
      break;
    }
    if (livesync::sameFolder(name, folder)) found = name;
  }
  root.close();
  return found;
}

livesync::StateCheck prepareSyncState(const Profile& profile, const std::string& previousIdentity) {
  const livesync::StateCheck check = livesync::prepareSyncState(profileFs(), stateBaseOf(profile.vaultName),
                                                                livesync::identityOf(profile), previousIdentity);
  if (check == livesync::StateCheck::SetAside) {
    LOG_INF("LSY", "The sync state of another vault in %s was moved aside", profile.vaultName.c_str());
  } else if (check == livesync::StateCheck::Failed) {
    LOG_ERR("LSY", "The sync state in %s could not be prepared", profile.vaultName.c_str());
  }
  return check;
}

bool hasProfile(const int slot) {
  profileFs();
  return livesync::validSlot(slot) && Storage.exists(livesync::profileFile(slot).c_str());
}

bool hasProfile() {
  for (int slot = 1; slot <= MAX_PROFILES; ++slot) {
    if (hasProfile(slot)) return true;
  }
  return false;
}

bool load(const int slot, Profile& out) {
  out = Profile{};
  if (!hasProfile(slot)) return false;
  std::string text;
  if (!Storage.readFileToString("LSY", livesync::profileFile(slot), MAX_PROFILE, text)) return false;
  const bool ok = livesync::profileFromJson(text, obfuscator(), out);
  wipe(text);
  return ok;
}

bool save(const int slot, const Profile& p) {
  if (!livesync::validSlot(slot)) return false;
  std::string text = livesync::profileToJson(p, obfuscator());
  const bool ok = profileFs().writeAll(livesync::profileFile(slot), text);
  wipe(text);
  return ok;
}

void forget(const int slot) {
  const auto& fs = profileFs();
  // The folder's sync state stays: it keeps the name of the vault it belongs to, so that another
  // database set up into this folder later never takes over its unsent notes and revisions.
  const livesync::SlotInfo info = livesync::slotInfo(fs, slot);
  if (info.configured) livesync::rememberIdentity(fs, stateBaseOf(info.vault), info.identity);
  livesync::removeSlot(fs, slot);
}

bool profileFromSetup(const std::string& uri, const std::string& passphrase, Profile& out, std::string& e2ee,
                      std::string& why) {
  std::string json;
  // The time-bound form is tried for this week only when the clock is set (after Wi-Fi/NTP).
  const time_t now = time(nullptr);
  const int64_t week = now > 1700000000 ? static_cast<int64_t>(now) * 1000 / 604800000 : -1;
  const livesync::Result r = livesync::decodeSetupUri(uri, passphrase, week, json);
  if (r != livesync::Result::Ok) {
    why = r == livesync::Result::WrongKey ? "Wrong passphrase for this Setup URI."
                                          : "This doesn't look like a LiveSync Setup URI.";
    return false;
  }
  JsonDocument s;
  const bool parsed = deserializeJson(s, json) == DeserializationError::Ok;
  wipe(json);  // held the CouchDB password and the E2EE passphrase
  if (!parsed) {
    why = "The Setup URI's settings could not be read.";
    return false;
  }
  const std::string remote = s["remoteType"] | "";
  if (!remote.empty() && remote != "COUCHDB") {
    why = "Only CouchDB remotes are supported.";
    return false;
  }
  switch (livesync::checkSetupEncryption(s["encrypt"] | false, !s["E2EEAlgorithm"].isNull(),
                                         std::string_view(s["E2EEAlgorithm"] | ""))) {
    case livesync::SetupEncryption::Ok:
      break;
    case livesync::SetupEncryption::EncryptionOff:
      why = "End-to-end encryption is off in this Setup URI. Turn it on (V2) in LiveSync, then copy a new Setup URI.";
      return false;
    case livesync::SetupEncryption::LegacyAlgorithm:
      why = "This vault uses the old V1 encryption. Switch LiveSync to V2 first.";
      return false;
  }
  if (s["enableCompression"] | false) {
    why = "Turn off compression in LiveSync first.";
    return false;
  }
  out = Profile{};
  out.serverUrl = s["couchDB_URI"] | "";
  while (!out.serverUrl.empty() && out.serverUrl.back() == '/') out.serverUrl.pop_back();
  out.user = s["couchDB_USER"] | "";
  out.password = s["couchDB_PASSWORD"] | "";
  out.database = s["couchDB_DBNAME"] | "";
  e2ee = s["passphrase"] | "";
  // The device writes only the id schemes it was tested with: obfuscated paths, case-insensitive
  // names, xxhash64, and either the legacy id derivation (no key, path version 0) or scheme 1
  // (a 64-hex key; path version 0 or 1 = NFC). Anything else is refused.
  const std::string idKey = s["idDerivationKey"] | "";
  const int idVersion = s["idDerivationVersion"] | 0;
  const int pathVersion = s["pathNormalizationVersion"] | 0;
  const bool legacyIds = idKey.empty() && idVersion == 0 && pathVersion == 0;
  const bool v1Ids = idVersion == 1 && livesync::isIdKeyHex(idKey) && (pathVersion == 0 || pathVersion == 1);
  out.pushAllowed = (s["usePathObfuscation"] | false) && (legacyIds || v1Ids) &&
                    !(s["handleFilenameCaseSensitive"] | false) && std::string(s["hashAlg"] | "xxhash64") == "xxhash64";
  if (out.pushAllowed && v1Ids) {
    out.idKey = idKey;
    out.nfcPaths = pathVersion == 1;
  }
  for (JsonPair kv : s.as<JsonObject>()) {  // the parsed copy holds the secrets too
    if (kv.value().is<const char*>()) {
      char* v = const_cast<char*>(kv.value().as<const char*>());
      mbedtls_platform_zeroize(v, strlen(v));
    }
  }
  out.vaultName = out.database;
  if (out.serverUrl.empty() || out.database.empty() || e2ee.empty()) {
    why = "The Setup URI has no server, database or passphrase.";
    return false;
  }
  return true;
}

bool finishSetup(Profile& p, const std::string& e2ee, std::string& why) {
  std::string body;
  const int status = transport(p).request("GET", "_local/obsidian_livesync_sync_parameters", "", body);
  JsonDocument doc;
  if (status != 200 || deserializeJson(doc, body) != DeserializationError::Ok) {
    why = status == 401 || status == 403 ? "The server refused the user name or password."
          : status < 0                   ? "Could not reach the server."
                                         : "The database has no LiveSync encryption settings.";
    return false;
  }
  p.saltB64 = doc["pbkdf2salt"] | "";
  if (p.pushAllowed && !p.idsV1()) {
    p.hashedPass = livesync::hashedPassphrase(e2ee);
    p.passDigest = livesync::passphraseDigest(e2ee);
  }
  std::string salt;
  if (!livesync::base64Decode(p.saltB64, salt) ||
      !livesync::deriveMaster(e2ee, reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), p.master)) {
    why = "The database's encryption salt could not be read.";
    return false;
  }
  return true;
}

livesync::Transport transport(const Profile& p) {
  livesync::Transport t;
  const std::string base = p.serverUrl + "/" + urlEncode(p.database) + "/";
  const std::string userPass = p.user + ":" + p.password;
  const std::string authHeader = "Basic " + b64(reinterpret_cast<const uint8_t*>(userPass.data()), userPass.size());
  const bool check = p.checkCert;
  t.request = [base, authHeader, check](const char* method, const std::string& path, const std::string& body,
                                        std::string& response) {
    if (!session) session.reset(new (std::nothrow) freeink::SecureHttpClient());
    if (!session) return -1;
    auto& http = *session;
    http.setUserAgent("VaultInk");
    if (!check) {
      http.setInsecure();  // encrypted, not checked: the user turned the check off
    } else {
      // Certificate dates need the real time; a cold boot without an RTC reading starts near 1970.
      if (trustedtime::trustedNow() == 0) trustedtime::syncNow(8000);
      // Your own ca.pem replaces the built-in roots; one that can't be read stops the request
      // rather than quietly trusting the public roots instead.
      if (caPem.empty() && Storage.exists(CA_CERT) &&
          (!Storage.readFileToString("LSY", CA_CERT, 16 * 1024, caPem) || caPem.empty())) {
        LOG_ERR("LSY", "%s could not be read", CA_CERT);
        caPem.clear();
        failedUnverified = true;
        return -1;
      }
      http.setCACert(caPem.empty() ? ca_bundle::PEM : caPem.c_str());
    }
    http.setTimeout(30000);
    lastInfo = livesync::ResponseInfo{};
    lastInfo.complete = false;
    if (!http.begin(base + path)) return -1;
    http.addHeader("Authorization", authHeader);
    http.addHeader("Accept", "application/json");
    if (!body.empty()) http.addHeader("Content-Type", "application/json");
    // Streamed into `response` (capped); every piece and every wait feeds the watchdog, since a
    // slow server can take up to the timeout.
    response.clear();
    bool tooBig = false;
    bool noMemory = false;
    const auto onData = [&response, &tooBig, &noMemory, &http](const uint8_t* data, size_t len) {
      resetTaskWatchdogIfSubscribed();
      lastInfo.received += len;
      const size_t need = response.size() + len;
      const size_t announced = http.hasContentLength() ? http.getContentLength() : 0;
      if (need > MAX_RESPONSE || announced > MAX_RESPONSE) {
        tooBig = true;
        return false;
      }
      if (need > response.capacity()) {
        // The buffer grows here, checked first: an allocation that fails inside std::string resets
        // the device. With a Content-Length it is taken once, else it doubles.
        const size_t want = std::min(MAX_RESPONSE, std::max({need, announced, response.capacity() * 2}));
        if (want > 64 * 1024 && heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT) < want + want / 4) {
          noMemory = true;
          return false;
        }
        response.reserve(want);
      }
      response.append(reinterpret_cast<const char*>(data), len);
      return true;
    };
    const auto keepAlive = [] {
      resetTaskWatchdogIfSubscribed();
      return false;
    };
    const int status =
        http.sendRequest(method, reinterpret_cast<const uint8_t*>(body.data()), body.size(), onData, keepAlive);
    lastInfo.contentType = http.getHeader("content-type");
    lastInfo.contentLength = http.hasContentLength() ? static_cast<int64_t>(http.getContentLength()) : -1;
    lastInfo.chunked = http.getHeader("transfer-encoding").find("chunked") != std::string::npos;
    // A body that stops early (the socket closed, a read waited out the timeout) still comes back
    // with its status: the engine must not parse it as if it were whole.
    lastInfo.complete = status >= 0 && http.responseComplete();
    if (tooBig) {
      LOG_ERR("LSY", "Response over %u bytes", static_cast<unsigned>(MAX_RESPONSE));
      return livesync::STATUS_TOO_BIG;
    }
    if (noMemory) {
      LOG_ERR("LSY", "No memory for a response (%u bytes read, largest free block %u)",
              static_cast<unsigned>(lastInfo.received),
              static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)));
      return livesync::STATUS_NO_MEMORY;
    }
    failedUnverified = check && status < 0;
    return status;
  };
  t.info = [] { return lastInfo; };
  return t;
}

livesync::FileStore storage() {
  livesync::FileStore s;
  s.exists = [](const std::string& p) { return Storage.exists(p.c_str()); };
  s.readAll = [](const std::string& p, std::string& out) {
    return Storage.exists(p.c_str()) && Storage.readFileToString("LSY", p, MAX_STATE_FILE, out);
  };
  s.writeAll = [](const std::string& p, const std::string& text) {
    Storage.ensureDirectoryExists(parentOf(p).c_str());
    const std::string tmp = p + ".tmp";
    return writeText(tmp, text) && Storage.replaceFile(tmp.c_str(), p.c_str());
  };
  s.begin = [](const std::string& p) {
    if (writer.file) {  // an earlier file was never committed: drop its partial copy
      writer.file.close();
      Storage.remove(writer.tmp.c_str());
    }
    Storage.ensureDirectoryExists(parentOf(p).c_str());
    writer.path = p;
    writer.tmp = p + ".lstmp";
    return Storage.openFileForWrite("LSY", writer.tmp, writer.file);
  };
  s.append = [](const char* bytes, size_t len) { return writer.file.write(bytes, len) == len; };
  s.commit = [](const bool keep) {
    writer.file.flush();
    writer.file.close();
    if (!keep) {
      Storage.remove(writer.tmp.c_str());
      return true;
    }
    return Storage.replaceFile(writer.tmp.c_str(), writer.path.c_str());
  };
  s.move = [](const std::string& from, const std::string& to) {
    Storage.ensureDirectoryExists(parentOf(to).c_str());
    if (Storage.exists(to.c_str())) Storage.remove(to.c_str());
    return Storage.rename(from.c_str(), to.c_str());
  };
  return s;
}

livesync::SyncOptions options(const Profile& p) {
  livesync::SyncOptions o;
  o.vaultDir = std::string(vaultfiles::VAULTS_ROOT) + "/" + p.vaultName;
  const std::string index = vaultfiles::indexPathFor(o.vaultDir);
  o.stateDir = index.substr(0, index.find_last_of('/')) + "/livesync";
  o.saltB64 = p.saltB64;
  // Notes changed on the device stay; remote changes to them land beside them as conflict copies.
  o.locallyChanged = vaultedit::changedLookup(o.vaultDir);
  const time_t now = static_cast<time_t>(trustedtime::trustedNow());
  char date[16] = "undated";
  if (now > 0) {
    struct tm t;
    localtime_r(&now, &t);
    strftime(date, sizeof(date), "%Y-%m-%d", &t);
  }
  o.conflictDate = date;
  // A conflict copy holds the server's text: it is sent back up too, so Obsidian keeps both.
  const std::string vaultDir = o.vaultDir;
  o.onConflictCopy = [vaultDir](const std::string& rel) { vaultedit::markChanged(vaultDir + "/" + rel); };
  // A log entry holds 255 characters: a longer line goes out in pieces.
  o.logError = [](const std::string& line) {
    for (size_t at = 0; at < line.size(); at += 200) LOG_ERR("LSY", "%s", line.substr(at, 200).c_str());
  };
  return o;
}

bool pushChanges(Profile& p, PushStats& stats, std::string& error) {
  if (!p.canPush()) return true;
  const auto o = options(p);
  const auto changed = vaultedit::changedList(o.vaultDir);
  if (changed.empty()) return true;
  const auto fs = storage();
  // What the last pull saw: id -> path and id -> _rev.
  const auto ids = livesync::loadMap(fs, o.stateDir + "/files.tsv");
  auto revs = livesync::loadMap(fs, o.stateDir + "/revs.tsv");
  const auto net = transport(p);
  const livesync::PushIdentity identity{p.hashedPass, p.passDigest, p.idKey, p.nfcPaths};
  const livesync::RandomFill random = [](uint8_t* out, size_t len) { esp_fill_random(out, len); };
  const time_t now = static_cast<time_t>(trustedtime::trustedNow());
  const int64_t nowMs = now > 0 ? static_cast<int64_t>(now) * 1000 : 0;
  for (const std::string& rel : changed) {
    resetTaskWatchdogIfSubscribed();
    livesync::PushItem item;
    item.path = rel;
    for (const auto& [id, path] : ids) {
      if (path == rel) item.knownId = id;
    }
    // A note made here and sent before any pull brought it back has no map entry yet: its id
    // is the one the plugin would give it, and its revision is in revs.tsv already.
    if (item.knownId.empty()) item.knownId = livesync::noteIdFor(identity, rel);
    if (!item.knownId.empty()) {
      const auto it = revs.find(item.knownId);
      if (it != revs.end()) item.knownRev = it->second;
    }
    const std::string full = o.vaultDir + "/" + rel;
    item.deleted = !Storage.exists(full.c_str());
    if (!item.deleted && !vaultedit::readNote(full, item.content)) {
      ++stats.skipped;  // too big for the device to send (over 256 KB): stays on the list
      continue;
    }
    std::string newId, newRev;
    switch (livesync::pushNote(net, p.master, identity, random, item, nowMs, newId, newRev, error)) {
      case livesync::PushResult::Pushed:
        ++stats.pushed;
        // The new revision is saved before the note leaves the list, so a power cut in between
        // can't leave an old revision for the next edit to build on.
        if (!newRev.empty()) revs[newId] = newRev;
        if (!livesync::saveMap(fs, o.stateDir + "/revs.tsv", revs)) {
          error = "Could not save the sync state on the SD card.";
          return false;
        }
        vaultedit::unmarkChanged(o.vaultDir, rel);
        break;
      case livesync::PushResult::Conflict:
        ++stats.conflicts;  // the pull saves the server's text beside it; the next sync sends ours
        break;
      case livesync::PushResult::Skipped:
        if (!error.empty()) {
          ++stats.skipped;
          error.clear();
        } else {
          vaultedit::unmarkChanged(o.vaultDir, rel);  // nothing to send (gone on both sides)
        }
        break;
      case livesync::PushResult::Failed:
        livesync::saveMap(fs, o.stateDir + "/revs.tsv", revs);
        return false;
    }
  }
  livesync::saveMap(fs, o.stateDir + "/revs.tsv", revs);
  return true;
}

// The companion app drops this tiny file on the card while File Transfer is open (the web server cannot start a sync
// itself: it would restart the device). Leaving File Transfer restarts the reader, and the wake that follows runs the
// sync once, as if it were due.
constexpr char SYNC_REQUEST[] = "/.vaultink/sync-request";

uint8_t autoSyncDue() {
  const bool requested = Storage.exists(SYNC_REQUEST);
  if (requested) Storage.remove(SYNC_REQUEST);
  const int64_t now = trustedtime::trustedNow();
  uint8_t mask = 0;
  for (int slot = 1; slot <= MAX_PROFILES; ++slot) {
    if (!hasProfile(slot)) continue;
    Profile p;
    if (!load(slot, p)) continue;
    const bool due = p.autoHours > 0 && now > 0 && now - p.lastAttempt >= static_cast<int64_t>(p.autoHours) * 3600;
    if (due) {
      p.lastAttempt = now;
      save(slot, p);
    }
    wipeProfile(p);
    if (due || requested) mask |= static_cast<uint8_t>(1u << (slot - 1));
  }
  return mask;
}

bool hasOwnCaCert() { return Storage.exists(CA_CERT); }

bool lastFailedUnverified() { return failedUnverified; }

void clearLastFailure() { failedUnverified = false; }

void resetSyncState(const Profile& profile) {
  const auto o = options(profile);
  Storage.remove((o.stateDir + "/checkpoint").c_str());
  Storage.remove((o.stateDir + "/files.tsv").c_str());
  Storage.remove(skippedListPath(profile).c_str());  // everything is pulled again anyway
}

std::string skippedListPath(const Profile& profile) { return options(profile).stateDir + "/" + livesync::SKIPPED_FILE; }

void endSession() {
  if (session) session->end();
  session.reset();  // drops the Authorization header and the TLS state with it
  caPem.clear();
}

}  // namespace livesyncdev
