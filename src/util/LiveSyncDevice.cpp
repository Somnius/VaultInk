#include "LiveSyncDevice.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <LiveSyncIds.h>
#include <LiveSyncPush.h>
#include <Logging.h>
#include <ObfuscationUtils.h>
#include <SecureHttpClient.h>
#include <TrustedTime.h>
#include <esp_random.h>
#include <mbedtls/platform_util.h>

#include <cctype>
#include <cstring>
#include <ctime>
#include <memory>

#include "util/CaBundle.h"
#include "util/TaskWatchdog.h"
#include "util/VaultEdit.h"
#include "util/VaultFiles.h"

namespace livesyncdev {
namespace {

constexpr size_t MAX_PROFILE = 8 * 1024;
constexpr size_t MAX_STATE_FILE = 2 * 1024 * 1024;  // the id->path map of a big vault
// One CouchDB answer (a page of changes, a batch of docs or chunks); lands in PSRAM.
constexpr size_t MAX_RESPONSE = 6 * 1024 * 1024;

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

}  // namespace

void wipeProfile(Profile& p) {
  wipe(p.password);
  mbedtls_platform_zeroize(p.master, sizeof(p.master));
}

bool hasProfile() { return Storage.exists(PROFILE); }

bool load(Profile& out) {
  std::string text;
  if (!Storage.readFileToString("LSY", PROFILE, MAX_PROFILE, text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text) != DeserializationError::Ok) return false;
  out = Profile{};
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
  out.hashedPass = obfuscation::deobfuscateFromBase64(doc["pushIdA"] | "", &okPush);
  out.passDigest = obfuscation::deobfuscateFromBase64(doc["pushIdB"] | "", &okPush);
  if (!okPush) out.hashedPass.clear(), out.passDigest.clear();
  bool ok = true, okKey = true;
  out.password = obfuscation::deobfuscateFromBase64(doc["password"] | "", &ok);
  std::string key = obfuscation::deobfuscateFromBase64(doc["key"] | "", &okKey);
  wipe(text);
  const bool valid =
      ok && okKey && key.size() == sizeof(out.master) && !out.serverUrl.empty() && !out.vaultName.empty();
  if (valid) memcpy(out.master, key.data(), sizeof(out.master));
  wipe(key);
  if (!valid) wipeProfile(out);
  return valid;
}

bool save(const Profile& p) {
  JsonDocument doc;
  doc["server"] = p.serverUrl;
  doc["user"] = p.user;
  doc["password"] = obfuscation::obfuscateToBase64(p.password);
  doc["database"] = p.database;
  doc["salt"] = p.saltB64;
  doc["key"] = obfuscation::obfuscateToBase64(std::string(reinterpret_cast<const char*>(p.master), sizeof(p.master)));
  doc["vault"] = p.vaultName;
  doc["last"] = p.lastResult;
  doc["checkCert"] = p.checkCert;
  doc["autoHours"] = p.autoHours;
  doc["lastAttempt"] = p.lastAttempt;
  if (p.canPush()) {
    doc["pushIdA"] = obfuscation::obfuscateToBase64(p.hashedPass);
    doc["pushIdB"] = obfuscation::obfuscateToBase64(p.passDigest);
  }
  std::string text;
  serializeJson(doc, text);
  Storage.ensureDirectoryExists(DIR);
  const std::string tmp = std::string(PROFILE) + ".tmp";
  return writeText(tmp, text) && Storage.replaceFile(tmp.c_str(), PROFILE);
}

void forget() {
  Storage.remove(PROFILE);
  clearPendingSetup();
}

bool hasPendingSetup() { return Storage.exists(PENDING_SETUP); }
bool readPendingSetup(std::string& uri) { return Storage.readFileToString("LSY", PENDING_SETUP, 64 * 1024, uri); }
bool savePendingSetup(const std::string& uri) {
  Storage.ensureDirectoryExists(DIR);
  return Storage.writeFile(PENDING_SETUP, String(uri.c_str()));
}
void clearPendingSetup() { Storage.remove(PENDING_SETUP); }

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
  if (!(s["encrypt"] | false) || std::string(s["E2EEAlgorithm"] | "") != "v2") {
    why = "Turn on end-to-end encryption (V2) in LiveSync first.";
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
  // The device writes only the id scheme it was tested with: obfuscated paths, the legacy id
  // derivation, case-insensitive names.
  const std::string idKey = s["idDerivationKey"] | "";
  out.pushAllowed = (s["usePathObfuscation"] | false) && idKey.empty() && (s["idDerivationVersion"] | 0) == 0 &&
                    (s["pathNormalizationVersion"] | 0) == 0 && !(s["handleFilenameCaseSensitive"] | false) &&
                    std::string(s["hashAlg"] | "xxhash64") == "xxhash64";
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
  if (p.pushAllowed) {
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
    if (!http.begin(base + path)) return -1;
    http.addHeader("Authorization", authHeader);
    http.addHeader("Accept", "application/json");
    if (!body.empty()) http.addHeader("Content-Type", "application/json");
    // Streamed into `response` (capped); every piece and every wait feeds the watchdog, since a
    // slow server can take up to the timeout.
    response.clear();
    bool tooBig = false;
    const auto onData = [&response, &tooBig](const uint8_t* data, size_t len) {
      resetTaskWatchdogIfSubscribed();
      if (response.size() + len > MAX_RESPONSE) {
        tooBig = true;
        return false;
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
    if (tooBig) {
      LOG_ERR("LSY", "Response over %u bytes", static_cast<unsigned>(MAX_RESPONSE));
      return -2;
    }
    failedUnverified = check && status < 0;
    return status;
  };
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
  const livesync::PushIdentity identity{p.hashedPass, p.passDigest};
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
    if (item.knownId.empty()) item.knownId = livesync::noteId(p.passDigest, rel);
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

bool autoSyncDue() {
  if (!hasProfile()) return false;
  Profile p;
  if (!load(p)) return false;
  const int64_t now = trustedtime::trustedNow();
  const bool due = p.autoHours > 0 && now > 0 && now - p.lastAttempt >= static_cast<int64_t>(p.autoHours) * 3600;
  if (due) {
    p.lastAttempt = now;
    save(p);
  }
  wipeProfile(p);
  return due;
}

bool hasOwnCaCert() { return Storage.exists(CA_CERT); }

bool lastFailedUnverified() { return failedUnverified; }

void resetSyncState(const Profile& profile) {
  const auto o = options(profile);
  Storage.remove((o.stateDir + "/checkpoint").c_str());
  Storage.remove((o.stateDir + "/files.tsv").c_str());
}

void endSession() {
  if (session) session->end();
  session.reset();  // drops the Authorization header and the TLS state with it
  caPem.clear();
}

}  // namespace livesyncdev
