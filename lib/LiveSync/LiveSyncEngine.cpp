#include "LiveSyncEngine.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <vector>

namespace livesync {
namespace {

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

// Ids that are never vault files: chunks, control docs, design docs, internal files.
bool skipId(const std::string& id) {
  return id.empty() || id[0] == '_' || id.rfind("h:", 0) == 0 || id.rfind("i:", 0) == 0 || id.rfind("ix:", 0) == 0 ||
         id.rfind("ps:", 0) == 0 || id == "obsydian_livesync_version" || id == "syncinfo";
}

// The id->path map: one "id\tpath" line per file written by an earlier sync.
std::map<std::string, std::string> loadMap(const Storage& fs, const std::string& file) {
  std::map<std::string, std::string> map;
  std::string text;
  if (!fs.readAll(file, text)) return map;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    const size_t tab = text.find('\t', pos);
    if (tab != std::string::npos && tab < end) map[text.substr(pos, tab - pos)] = text.substr(tab + 1, end - tab - 1);
    pos = end + 1;
  }
  return map;
}

bool saveMap(const Storage& fs, const std::string& file, const std::map<std::string, std::string>& map) {
  std::string text;
  for (const auto& [id, path] : map) text += id + "\t" + path + "\n";
  return fs.writeAll(file, text);
}

// POST _all_docs?include_docs=true {"keys": ids}; fills `docs` with the rows that have a doc.
bool fetchDocs(const Transport& net, const std::vector<std::string>& ids, JsonDocument& docs, std::string& error) {
  JsonDocument req;
  JsonArray keys = req["keys"].to<JsonArray>();
  for (const auto& id : ids) keys.add(id);
  std::string body, response;
  serializeJson(req, body);
  const int status = net.request("POST", "_all_docs?include_docs=true", body, response);
  if (status != 200) {
    error = "Server answered " + std::to_string(status) + " to a document request.";
    return false;
  }
  if (deserializeJson(docs, response) != DeserializationError::Ok) {
    error = "The server's answer could not be read (too big or not JSON).";
    return false;
  }
  return true;
}

class Run {
 public:
  Run(const Transport& net, const Storage& fs, const Key master, const SyncOptions& o, SyncStats& stats,
      std::string& error, const SyncProgress& progress)
      : net(net), fs(fs), master(master), o(o), stats(stats), error(error), progress(progress) {}

  bool run() {
    if (!checkRemote()) return false;
    const std::string seqFile = o.stateDir + "/checkpoint";
    const std::string mapFile = o.stateDir + "/files.tsv";
    std::string since;
    if (!fs.readAll(seqFile, since) || since.empty()) since = "0";
    map = loadMap(fs, mapFile);
    while (true) {
      std::string response;
      const int status = net.request(
          "GET", "_changes?since=" + urlEncode(since) + "&limit=" + std::to_string(o.changesPage), "", response);
      if (status != 200) {
        error = "Server answered " + std::to_string(status) + " to the change list.";
        return false;
      }
      JsonDocument changes;
      if (deserializeJson(changes, response) != DeserializationError::Ok) {
        error = "The change list could not be read.";
        return false;
      }
      std::vector<std::string> files;
      for (JsonObjectConst row : changes["results"].as<JsonArrayConst>()) {
        const std::string id = row["id"] | "";
        if (skipId(id)) continue;
        if (row["deleted"] | false) {
          if (!trash(id)) return false;
        } else {
          files.push_back(id);
        }
      }
      for (size_t i = 0; i < files.size(); i += static_cast<size_t>(o.docsBatch)) {
        const std::vector<std::string> batch(files.begin() + static_cast<std::ptrdiff_t>(i),
                                             files.begin() + static_cast<std::ptrdiff_t>(std::min(
                                                                 files.size(), i + static_cast<size_t>(o.docsBatch))));
        if (!applyDocs(batch)) return false;
      }
      // The page is applied: only now do the map and the checkpoint move on.
      const std::string last = changes["last_seq"].is<const char*>() ? std::string(changes["last_seq"] | "")
                                                                      : std::to_string(changes["last_seq"] | 0);
      if (!saveMap(fs, mapFile, map) || !fs.writeAll(seqFile, last)) {
        error = "Could not save the sync state on the SD card.";
        return false;
      }
      ++stats.pages;
      const int pending = changes["pending"] | 0;
      if (changes["results"].as<JsonArrayConst>().size() == 0 || pending == 0 || last == since) return true;
      since = last;
    }
  }

 private:
  bool checkRemote() {
    std::string response;
    int status = net.request("GET", "obsydian_livesync_version", "", response);
    if (status == 200) {
      JsonDocument v;
      std::string why;
      if (deserializeJson(v, response) != DeserializationError::Ok ||
          !remoteVersionSupported(v.as<JsonObjectConst>(), &why)) {
        error = "This database uses a newer LiveSync (" + why + "). Update VaultInk.";
        return false;
      }
    } else if (status != 404) {
      error = status == 401 || status == 403 ? "The server refused the user name or password."
                                             : "Server answered " + std::to_string(status) + ".";
      return false;
    }
    status = net.request("GET", "_local/obsidian_livesync_sync_parameters", "", response);
    JsonDocument p;
    if (status != 200 || deserializeJson(p, response) != DeserializationError::Ok) {
      error = "The database has no LiveSync encryption settings (is end-to-end encryption on?).";
      return false;
    }
    if (std::string(p["pbkdf2salt"] | "") != o.saltB64) {
      error = "The database's passphrase changed. Import the setup again.";
      return false;
    }
    return true;
  }

  // A file deleted on the server (or replaced by another path) goes to the trash, not away.
  bool trash(const std::string& id) {
    const auto it = map.find(id);
    if (it == map.end()) return true;
    const std::string from = o.vaultDir + "/" + it->second;
    if (fs.exists(from) && !fs.move(from, o.stateDir + "/trash/" + it->second)) {
      error = "Could not move " + it->second + " to the trash.";
      return false;
    }
    map.erase(it);
    ++stats.trashed;
    return true;
  }

  bool applyDocs(const std::vector<std::string>& ids) {
    JsonDocument docs;
    if (!fetchDocs(net, ids, docs, error)) return false;
    for (JsonObjectConst row : docs["rows"].as<JsonArrayConst>()) {
      JsonObjectConst doc = row["doc"];
      if (doc.isNull()) continue;  // deleted since the change list was read
      FileEntry entry;
      DocKind kind;
      const Result r = readFileDoc(doc, master, entry, kind);
      if (r == Result::WrongKey) {
        error = "A file could not be decrypted: the passphrase doesn't match.";
        return false;
      }
      if (r != Result::Ok) {
        error = "A file record could not be read (" + entry.id + ").";
        return false;
      }
      if (kind != DocKind::File) continue;
      if (entry.deleted) {
        if (!trash(entry.id)) return false;
        continue;
      }
      if (!safeVaultPath(entry.path)) {
        error = "The database holds a path VaultInk won't write: " + entry.path;
        return false;
      }
      if (entry.size > o.maxFileBytes) {
        ++stats.skipped;
        continue;
      }
      if (!writeFile(entry)) return false;
    }
    return true;
  }

  bool writeFile(const FileEntry& entry) {
    if (progress) progress(stats.written + stats.trashed, entry.path);
    // The file's chunks, a few at a time; kept only while this file is written.
    std::map<std::string, std::pair<std::string, bool>> chunks;
    for (size_t i = 0; i < entry.children.size(); i += static_cast<size_t>(o.chunkBatch)) {
      std::vector<std::string> batch;
      for (size_t j = i; j < std::min(entry.children.size(), i + static_cast<size_t>(o.chunkBatch)); ++j) {
        if (!chunks.count(entry.children[j])) batch.push_back(entry.children[j]);
      }
      if (batch.empty()) continue;
      JsonDocument docs;
      if (!fetchDocs(net, batch, docs, error)) return false;
      for (JsonObjectConst row : docs["rows"].as<JsonArrayConst>()) {
        JsonObjectConst doc = row["doc"];
        if (doc.isNull()) continue;
        chunks[doc["_id"] | ""] = {std::string(doc["data"] | ""), doc["e_"] | false};
      }
    }
    const std::string target = o.vaultDir + "/" + entry.path;
    if (!fs.begin(target)) {
      error = "Could not write " + entry.path + " (is the SD card full?).";
      return false;
    }
    const Result r = assemble(
        entry, master,
        [&](const std::string& id, std::string& data, bool& encrypted) {
          const auto it = chunks.find(id);
          if (it == chunks.end()) return false;
          data = it->second.first;
          encrypted = it->second.second;
          return true;
        },
        [&](const char* bytes, size_t len) { return fs.append(bytes, len); });
    if (r != Result::Ok) {
      fs.commit(false);
      error = r == Result::Unsupported ? "This database uses compression. Turn it off in LiveSync and sync again."
                                       : "The file " + entry.path + " is incomplete on the server.";
      return false;
    }
    if (!fs.commit(true)) {
      error = "Could not save " + entry.path + " (is the SD card full?).";
      return false;
    }
    // The same id under a new path (renamed in Obsidian): the old copy goes to the trash.
    const auto old = map.find(entry.id);
    if (old != map.end() && old->second != entry.path && !trash(entry.id)) return false;
    map[entry.id] = entry.path;
    ++stats.written;
    return true;
  }

  const Transport& net;
  const Storage& fs;
  const uint8_t* master;
  const SyncOptions& o;
  SyncStats& stats;
  std::string& error;
  const SyncProgress& progress;
  std::map<std::string, std::string> map;
};

}  // namespace

bool safeVaultPath(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.size() > 1024) return false;
  size_t start = 0;
  while (start <= path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string::npos) end = path.size();
    const std::string part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") return false;
    for (const unsigned char c : part) {
      if (c < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
        return false;
      }
    }
    start = end + 1;
  }
  return true;
}

bool syncOnce(const Transport& net, const Storage& fs, const Key master, const SyncOptions& options, SyncStats& stats,
              std::string& error, const SyncProgress& progress) {
  stats = SyncStats{};
  error.clear();
  Run run(net, fs, master, options, stats, error, progress);
  return run.run();
}

}  // namespace livesync
