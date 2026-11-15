#include "LiveSyncEngine.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>
#include <vector>

// A chunk's `data` is one JSON string, and it can pass 65,535 characters. ArduinoJson keeps a
// string's length in 2 bytes on 32-bit boards unless told otherwise, and then answers NoMemory to
// the whole reply (platformio.ini sets 4).
static_assert(ARDUINOJSON_STRING_LENGTH_SIZE >= 4, "build with -DARDUINOJSON_STRING_LENGTH_SIZE=4");

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
}  // namespace

std::map<std::string, std::string> loadMap(const FileStore& fs, const std::string& file) {
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

bool saveMap(const FileStore& fs, const std::string& file, const std::map<std::string, std::string>& map) {
  std::string text;
  for (const auto& [id, path] : map) text += id + "\t" + path + "\n";
  return fs.writeAll(file, text);
}

namespace {

// Printable text for a log line or the skip list: control bytes become '?', cut at `max` bytes on a
// UTF-8 boundary.
std::string clean(const std::string& text, size_t max) {
  std::string out = text.substr(0, max);
  if (text.size() > max) {
    while (!out.empty() && (static_cast<unsigned char>(text[out.size()]) & 0xC0) == 0x80) out.pop_back();
  }
  for (char& c : out) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) c = '?';
  }
  return out;
}

}  // namespace

std::string describeResponse(const char* kind, const int status, const ResponseInfo& info, const std::string& body,
                             const char* jsonError, const bool showBody) {
  std::string line = std::string(kind) + ": ";
  if (status == STATUS_TOO_BIG) {
    line += "answer over the size cap";
  } else if (status == STATUS_NO_MEMORY) {
    line += "no memory for the answer";
  } else if (status < 0) {
    line += "no HTTP answer (" + std::to_string(status) + ")";
  } else {
    line += "HTTP " + std::to_string(status);
  }
  line += ", type " + (info.contentType.empty() ? std::string("none") : clean(info.contentType, 40));
  line += ", length ";
  if (info.chunked) {
    line += "chunked";
  } else if (info.contentLength >= 0) {
    line += std::to_string(info.contentLength);
  } else {
    line += "none";
  }
  line += ", got " + std::to_string(info.received) + " bytes";
  line += info.complete ? ", body complete" : ", body ended early";
  if (jsonError) line += std::string(", JSON ") + jsonError;
  if (showBody && !body.empty()) {
    static constexpr char hexd[] = "0123456789ABCDEF";
    line += ", starts \"";
    for (size_t i = 0; i < body.size() && i < 80; ++i) {
      const unsigned char c = static_cast<unsigned char>(body[i]);
      if (c == '\n') {
        line += "\\n";
      } else if (c == '\r') {
        line += "\\r";
      } else if (c == '\t') {
        line += "\\t";
      } else if (c == '"' || c == '\\') {
        line += '\\';
        line += static_cast<char>(c);
      } else if (c >= 0x20 && c < 0x7F) {
        line += static_cast<char>(c);
      } else {
        line += "\\x";
        line += hexd[c >> 4];
        line += hexd[c & 15];
      }
    }
    line += '"';
  }
  return line;
}

size_t chunksPerRequest(const FileEntry& file, const SyncOptions& options) {
  const uint64_t most = static_cast<uint64_t>(std::max(1, options.chunkBatch));
  if (file.children.empty()) return static_cast<size_t>(most);
  const uint64_t average = file.size / file.children.size();
  // As stored: base64 of the bytes (4/3), base64 again around the ciphertext (4/3), and about 500
  // bytes of IV, salt, tag and the row's own JSON. Doubled: the splitter's chunks are uneven.
  const uint64_t perChunk = (average * 16 / 9 + 512) * 2;
  return static_cast<size_t>(std::clamp<uint64_t>(options.maxResponseBytes / perChunk, 1, most));
}

namespace {

enum class Fetch : uint8_t { Ok, Unusable, Abort };
enum class Wrote : uint8_t { Yes, Skipped, Abort };

class Run {
 public:
  Run(const Transport& net, const FileStore& fs, const Key master, const SyncOptions& o, SyncStats& stats,
      std::string& error, const SyncProgress& progress)
      : net(net), fs(fs), master(master), o(o), stats(stats), error(error), progress(progress) {}

  bool run() {
    if (!checkRemote()) return false;
    const std::string seqFile = o.stateDir + "/checkpoint";
    const std::string mapFile = o.stateDir + "/files.tsv";
    std::string since;
    if (!fs.readAll(seqFile, since) || since.empty()) since = "0";
    map = loadMap(fs, mapFile);
    const std::string revFile = o.stateDir + "/revs.tsv";
    revs = loadMap(fs, revFile);
    // The files the last sync left out come first; the ones that fail again go back on the list.
    const std::vector<std::string> again = skippedIds();
    if (!again.empty()) {
      if (!applyInBatches(again)) return false;
      if (!saveSkips() || !saveMap(fs, mapFile, map) || !saveMap(fs, revFile, revs)) {
        error = "Could not save the sync state on the SD card.";
        return false;
      }
    }
    while (true) {
      std::string response;
      const int status = net.request(
          "GET", "_changes?since=" + urlEncode(since) + "&limit=" + std::to_string(o.changesPage), "", response);
      const ResponseInfo info = infoFor(response);
      if (status != 200) {
        log(describeResponse("changes", status, info, response, nullptr, true));
        error = status < 0 ? std::string("No answer to the change list request.")
                           : "Server answered " + std::to_string(status) + " to the change list.";
        return false;
      }
      if (!info.complete) {
        // The end of the body was not read (a chunked body's last "0" line, say), even if what
        // came parses: the page is not used.
        log(describeResponse("changes", status, info, response, nullptr, true));
        error = "The change list was cut short.";
        return false;
      }
      JsonDocument changes;
      const DeserializationError parsed = deserializeJson(changes, response);
      if (parsed != DeserializationError::Ok) {
        log(describeResponse("changes", status, info, response, parsed.c_str(), true));
        error = "The change list could not be read.";
        return false;
      }
      response = std::string();
      std::vector<std::string> files;
      for (JsonObjectConst row : changes["results"].as<JsonArrayConst>()) {
        const std::string id = row["id"] | "";
        if (skipId(id)) continue;
        if (row["deleted"] | false) {
          if (!trash(id)) return false;
          forget(id);
        } else {
          files.push_back(id);
        }
      }
      if (!applyInBatches(files)) return false;
      // The page is applied: only now do the map, the skip list and the checkpoint move on.
      const std::string last = changes["last_seq"].is<const char*>() ? std::string(changes["last_seq"] | "")
                                                                     : std::to_string(changes["last_seq"] | 0);
      if (!saveSkips() || !saveMap(fs, mapFile, map) || !saveMap(fs, revFile, revs) || !fs.writeAll(seqFile, last)) {
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
  struct Skip {
    std::string path;
    std::string reason;
  };

  void log(const std::string& line) const {
    if (o.logError) o.logError(line);
  }

  ResponseInfo infoFor(const std::string& response) const {
    if (net.info) return net.info();
    ResponseInfo info;
    info.received = response.size();
    return info;
  }

  bool checkRemote() {
    std::string response;
    int status = net.request("GET", "obsydian_livesync_version", "", response);
    if (status == 200) {
      JsonDocument v;
      std::string why;
      const DeserializationError parsed = deserializeJson(v, response);
      if (parsed != DeserializationError::Ok) {
        log(describeResponse("version", status, infoFor(response), response, parsed.c_str(), true));
      }
      if (parsed != DeserializationError::Ok || !remoteVersionSupported(v.as<JsonObjectConst>(), &why)) {
        error = "This database uses a newer LiveSync (" + why + "). Update VaultInk.";
        return false;
      }
    } else if (status != 404) {
      log(describeResponse("version", status, infoFor(response), response, nullptr, true));
      error = status == 401 || status == 403 ? "The server refused the user name or password."
              : status < 0                   ? std::string("No answer from the server.")
                                             : "Server answered " + std::to_string(status) + ".";
      return false;
    }
    status = net.request("GET", "_local/obsidian_livesync_sync_parameters", "", response);
    JsonDocument p;
    const DeserializationError parsed =
        status == 200 ? deserializeJson(p, response) : DeserializationError(DeserializationError::EmptyInput);
    if (status != 200 || parsed != DeserializationError::Ok) {
      log(describeResponse("parameters", status, infoFor(response), response, status == 200 ? parsed.c_str() : nullptr,
                           true));
      error = "The database has no LiveSync encryption settings (is end-to-end encryption on?).";
      return false;
    }
    if (std::string(p["pbkdf2salt"] | "") != o.saltB64) {
      error = "The database's passphrase changed. Import the setup again.";
      return false;
    }
    return true;
  }

  // The ids in skipped.txt ("path<TAB>reason<TAB>id" lines). The list starts empty for this sync.
  std::vector<std::string> skippedIds() {
    skipFile = o.stateDir + "/" + SKIPPED_FILE;
    std::vector<std::string> ids;
    std::string text;
    if (!fs.readAll(skipFile, text)) return ids;
    hadSkipFile = true;
    size_t pos = 0;
    while (pos < text.size()) {
      size_t end = text.find('\n', pos);
      if (end == std::string::npos) end = text.size();
      const std::string line = text.substr(pos, end - pos);
      const size_t tab = line.rfind('\t');
      if (tab != std::string::npos && tab + 1 < line.size()) ids.push_back(line.substr(tab + 1));
      pos = end + 1;
    }
    return ids;
  }

  bool saveSkips() {
    if (skips.empty() && !hadSkipFile) return true;
    std::string text;
    for (const auto& [id, s] : skips) text += s.path + "\t" + s.reason + "\t" + id + "\n";
    hadSkipFile = true;
    return fs.writeAll(skipFile, text);
  }

  void skip(const std::string& id, const std::string& path, const std::string& reason) {
    const std::string shown = clean(path, 300);
    log("skipped \"" + shown + "\": " + reason);
    const auto it = skips.find(id);
    if (it != skips.end()) {
      it->second = Skip{shown, clean(reason, 100)};
    } else if (skips.size() < o.maxSkipList) {
      skips[id] = Skip{shown, clean(reason, 100)};
    } else {
      // No room to remember this file: the sync stops before the checkpoint moves past it.
      listFull = true;
    }
    stats.skipped = static_cast<int>(skips.size());
  }

  void forget(const std::string& id) {
    skips.erase(id);
    stats.skipped = static_cast<int>(skips.size());
  }

  // The answer to a request for one chunk that is too long to parse: the answer itself, and where
  // the chunk's stored text is in it.
  struct Raw {
    size_t expected = 0;  // about how long the answer will be
    std::string text;
    size_t at = 0;
    size_t len = 0;
    bool encrypted = false;
    bool used = false;
  };

  // Finds the one chunk `id` in an `_all_docs` answer without parsing the chunk's text: the answer
  // with that text cut out is small and is parsed as usual, which says whether it is one row, for
  // this id, with a `data` string. A chunk as LiveSync stores it ("%=" and base64) has no escapes;
  // text that has any is left to the full parser. False: not found that way.
  static bool sliceChunk(const std::string& response, const std::string& id, Raw& raw) {
    static constexpr std::string_view KEY = "\"data\":\"";
    const size_t key = response.find(KEY);
    if (key == std::string::npos) return false;
    const size_t start = key + KEY.size();
    const size_t end = response.find('"', start);
    if (end == std::string::npos || memchr(response.data() + start, '\\', end - start)) return false;
    if (start + (response.size() - end) > 16 * 1024) return false;
    std::string shell(response, 0, start);
    shell.append(response, end, std::string::npos);
    JsonDocument small;
    if (deserializeJson(small, shell) != DeserializationError::Ok) return false;
    const JsonArrayConst rows = small["rows"].as<JsonArrayConst>();
    if (rows.size() != 1) return false;
    const JsonObjectConst doc = rows[0]["doc"];
    if (id != (doc["_id"] | "") || !doc["data"].is<JsonString>() || doc["data"].as<JsonString>().size() != 0) {
      return false;
    }
    raw.at = start;
    raw.len = end - start;
    raw.encrypted = doc["e_"] | false;
    return true;
  }

  // One POST _all_docs?include_docs=true {"keys": ids}. False with the answer logged, a short
  // reason in `why`, and `retry` set when the same request may work a second time. With `raw` (one
  // chunk asked for) an answer too long to parse is handed over in it, and `docs` stays empty.
  bool fetchOnce(const char* kind, const std::vector<std::string>& ids, const std::string* file, JsonDocument& docs,
                 std::string& why, int& status, bool& retry, Raw* raw) {
    std::string body, response;
    {
      JsonDocument req;
      JsonArray keys = req["keys"].to<JsonArray>();
      for (const auto& id : ids) keys.add(id);
      serializeJson(req, body);
    }
    static constexpr char PATH[] = "_all_docs?include_docs=true";
    status = raw && net.requestOne ? net.requestOne("POST", PATH, body, response, raw->expected)
                                   : net.request("POST", PATH, body, response);
    const ResponseInfo info = infoFor(response);
    const char* jsonError = nullptr;
    bool showBody = true;
    retry = true;
    sizeTrouble = false;
    if (status == 200 && info.complete && response.size() > o.maxParsedBytes) {
      if (raw && sliceChunk(response, ids[0], *raw)) {
        raw->text = std::move(response);
        raw->used = true;
        return true;
      }
      why = "the server's answer was too big";
      retry = false;
      sizeTrouble = true;
    } else if (status == 200 && info.complete) {
      const DeserializationError parsed = deserializeJson(docs, response);
      if (parsed == DeserializationError::Ok && docs["rows"].is<JsonArrayConst>()) return true;
      const bool noMemory = parsed == DeserializationError::NoMemory;
      jsonError = parsed == DeserializationError::Ok ? "Ok but no rows" : parsed.c_str();
      showBody = !noMemory;
      retry = !noMemory;
      sizeTrouble = noMemory;
      why = noMemory ? "not enough memory for the server's answer"
                     : std::string("the server's answer was not readable JSON (") + jsonError + ")";
    } else if (status == 200) {
      why = "the server's answer was cut short";
    } else if (status == STATUS_TOO_BIG) {
      why = "the server's answer was too big";
      retry = false;
      sizeTrouble = true;
    } else if (status == STATUS_NO_MEMORY) {
      why = "not enough memory for the server's answer";
      retry = false;
      sizeTrouble = true;
    } else {
      why = "the server answered " + std::to_string(status);
    }
    docs.clear();
    std::string line = describeResponse(kind, status, info, response, jsonError, showBody);
    if (file) line += ", file \"" + clean(*file, 200) + "\"";
    log(line);
    return false;
  }

  // The docs for `ids`, tried twice. Unusable: this request cannot be answered but the server is
  // there (a small request still works), so only what was asked for is left out. Abort: the server
  // cannot be used (no answer, a refused login, or request after request failing while the small
  // one still works, as behind a proxy that refuses them); `error` is set. After Unusable,
  // `sizeTrouble` says the answer was too big for the device: asking for less may work.
  Fetch fetch(const char* kind, const std::vector<std::string>& ids, const std::string* file, JsonDocument& docs,
              std::string& why, Raw* raw = nullptr) {
    int status = 0;
    bool retry = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
      if (fetchOnce(kind, ids, file, docs, why, status, retry, raw)) {
        failedInARow = 0;
        return Fetch::Ok;
      }
      if (status == 401 || status == 403) {
        error = "The server refused the user name or password.";
        return Fetch::Abort;
      }
      if (!retry) break;
    }
    std::string small;
    const int probe = net.request("GET", "obsydian_livesync_version", "", small);
    if (probe == 200 || probe == 404) {
      // An answer too big for the device is about that one file. Anything else, time after time
      // with no request answered in between, is the server (or a proxy before it) refusing them
      // all: stopping keeps the checkpoint, where going on would skip every file and call it done.
      if (sizeTrouble || ++failedInARow <= o.maxFailuresInARow) return Fetch::Unusable;
      error = std::to_string(failedInARow) + " document requests in a row failed (" + why +
              "). The sync stopped; nothing was left out.";
      return Fetch::Abort;
    }
    error = status == 200 ? "The connection to the server was lost."
            : status < 0  ? "No answer to a document request."
                          : "Server answered " + std::to_string(status) + " to a document request.";
    return Fetch::Abort;
  }

  bool applyInBatches(const std::vector<std::string>& ids) {
    const size_t step = static_cast<size_t>(std::max(1, o.docsBatch));
    for (size_t i = 0; i < ids.size(); i += step) {
      const std::vector<std::string> batch(ids.begin() + static_cast<std::ptrdiff_t>(i),
                                           ids.begin() + static_cast<std::ptrdiff_t>(std::min(ids.size(), i + step)));
      if (!applyDocs(batch)) return false;
      if (listFull) {
        error = "More than " + std::to_string(o.maxSkipList) + " files could not be fetched (see " + SKIPPED_FILE +
                "). The sync stopped here so none of the others is lost.";
        return false;
      }
    }
    return true;
  }

  // A file deleted on the server (or replaced by another path) goes to the trash, not away.
  bool trash(const std::string& id) {
    const auto it = map.find(id);
    if (it == map.end()) return true;
    const std::string from = o.vaultDir + "/" + it->second;
    if (o.locallyChanged && o.locallyChanged(it->second)) {  // changed here: it stays, still known
      ++stats.conflicts;
      return true;
    }
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
    std::string why;
    switch (fetch("docs", ids, nullptr, docs, why)) {
      case Fetch::Ok:
        break;
      case Fetch::Abort:
        return false;
      case Fetch::Unusable:
        // One record in the batch may be the trouble: ask for each alone and leave only that one out.
        if (ids.size() == 1) {
          skip(ids[0], ids[0], why);
          return true;
        }
        for (const auto& id : ids) {
          if (!applyDocs({id})) return false;
        }
        return true;
    }
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
        const std::string id = doc["_id"] | "";
        skip(id, id, "its record could not be read");
        continue;
      }
      if (kind != DocKind::File) continue;
      const std::string rev = doc["_rev"] | "";
      if (entry.deleted) {
        if (!trash(entry.id)) return false;
        revs[entry.id] = rev;
        forget(entry.id);
        continue;
      }
      if (!safeVaultPath(entry.path)) {
        skip(entry.id, entry.path, "its name cannot be stored on the SD card");
        continue;
      }
      if (entry.size > o.maxFileBytes) {
        skip(entry.id, entry.path, "bigger than " + std::to_string(o.maxFileBytes / (1024 * 1024)) + " MB");
        continue;
      }
      switch (writeFile(entry)) {
        case Wrote::Abort:
          return false;
        case Wrote::Skipped:
          break;
        case Wrote::Yes:
          revs[entry.id] = rev;
          forget(entry.id);
          break;
      }
    }
    return true;
  }

  // Drops the half-written file and puts the file on the skip list.
  Wrote leaveOut(const FileEntry& entry, const std::string& reason) {
    fs.commit(false);
    skip(entry.id, entry.path, reason);
    return Wrote::Skipped;
  }

  // After one chunk went to the sink. False with what writeFile returns when the file stops here.
  bool chunkWritten(const FileEntry& entry, const Result r, const bool cardFailed, Wrote& stop) {
    if (cardFailed) {
      fs.commit(false);
      error = "Could not write " + entry.path + " (is the SD card full?).";
      stop = Wrote::Abort;
      return false;
    }
    if (r == Result::Unsupported) {
      fs.commit(false);
      error = "This database uses compression. Turn it off in LiveSync and sync again.";
      stop = Wrote::Abort;
      return false;
    }
    if (r != Result::Ok) {
      stop = leaveOut(
          entry, r == Result::WrongKey ? "a piece of it could not be decrypted" : "a piece of it could not be decoded");
      return false;
    }
    return true;
  }

  Wrote writeFile(const FileEntry& entry) {
    if (progress) progress(stats.written + stats.trashed, entry.path);
    const bool conflict = o.locallyChanged && o.locallyChanged(entry.path);
    const std::string target = o.vaultDir + "/" + (conflict ? conflictPath(entry.path, o.conflictDate) : entry.path);
    if (!fs.begin(target)) {
      error = "Could not write " + entry.path + " (is the SD card full?).";
      return Wrote::Abort;
    }
    bool cardFailed = false;
    const ByteSink sink = [&](const char* bytes, size_t len) {
      if (!fs.append(bytes, len)) cardFailed = true;
      return !cardFailed;
    };
    // A batch of chunks is fetched, written out and dropped before the next one is asked for, so
    // only one answer is in memory whatever the file's size.
    size_t step = chunksPerRequest(entry, o);
    for (size_t i = 0; i < entry.children.size();) {
      const size_t end = std::min(entry.children.size(), i + step);
      std::vector<std::string> batch;
      for (size_t j = i; j < end; ++j) {
        if (std::find(batch.begin(), batch.end(), entry.children[j]) == batch.end()) batch.push_back(entry.children[j]);
      }
      JsonDocument docs;
      std::string why;
      // One chunk alone may be longer than a batch is allowed to be (a picture stored as one piece).
      Raw raw;
      if (batch.size() == 1 && !entry.children.empty()) {
        const uint64_t average = entry.size / entry.children.size();
        raw.expected = static_cast<size_t>((entry.binary ? average * 16 / 9 : average * 4 / 3) + 2048);
      }
      const Fetch fetched = fetch("chunks", batch, &entry.path, docs, why, batch.size() == 1 ? &raw : nullptr);
      if (fetched == Fetch::Abort) {
        fs.commit(false);
        return Wrote::Abort;
      }
      if (fetched == Fetch::Unusable) {
        // Too big for the device (uneven chunks, little memory left): the same chunks in smaller
        // batches, down to one at a time, before the file is given up. What was written stays.
        if (sizeTrouble && end - i > 1) {
          step = (end - i) / 2;
          log("chunks: asking again, " + std::to_string(step) + " per request, file \"" + clean(entry.path, 200) +
              "\"");
          continue;
        }
        return leaveOut(entry, why);
      }
      if (raw.used) {
        const Result r =
            appendChunkInPlace(entry.binary, master, raw.text.data() + raw.at, raw.len, raw.encrypted, sink);
        Wrote stop;
        if (!chunkWritten(entry, r, cardFailed, stop)) return stop;
        i = end;
        continue;
      }
      std::map<std::string, JsonObjectConst> got;
      for (JsonObjectConst row : docs["rows"].as<JsonArrayConst>()) {
        JsonObjectConst doc = row["doc"];
        if (!doc.isNull()) got[doc["_id"] | ""] = doc;
      }
      for (size_t j = i; j < end; ++j) {
        const auto it = got.find(entry.children[j]);
        if (it == got.end() || !it->second["data"].is<JsonString>()) {
          return leaveOut(entry, "a piece of it is missing on the server");
        }
        const JsonString data = it->second["data"].as<JsonString>();
        const Result r = appendChunk(entry.binary, master, std::string_view(data.c_str(), data.size()),
                                     it->second["e_"] | false, sink);
        Wrote stop;
        if (!chunkWritten(entry, r, cardFailed, stop)) return stop;
      }
      i = end;
    }
    if (!fs.commit(true)) {
      error = "Could not save " + entry.path + " (is the SD card full?).";
      return Wrote::Abort;
    }
    if (conflict) {
      map[entry.id] = entry.path;
      ++stats.conflicts;
      if (o.onConflictCopy) o.onConflictCopy(conflictPath(entry.path, o.conflictDate));
      return Wrote::Yes;
    }
    // The same id under a new path (renamed in Obsidian): the old copy goes to the trash.
    const auto old = map.find(entry.id);
    if (old != map.end() && old->second != entry.path && !trash(entry.id)) return Wrote::Abort;
    map[entry.id] = entry.path;
    ++stats.written;
    return Wrote::Yes;
  }

  const Transport& net;
  const FileStore& fs;
  const uint8_t* master;
  const SyncOptions& o;
  SyncStats& stats;
  std::string& error;
  const SyncProgress& progress;
  std::map<std::string, std::string> map;
  std::map<std::string, std::string> revs;  // id -> the _rev last pulled (what a push builds on)
  std::map<std::string, Skip> skips;        // id -> the files this sync left out
  std::string skipFile;
  bool hadSkipFile = false;
  bool listFull = false;     // a file could not be listed: the sync stops (see applyInBatches)
  bool sizeTrouble = false;  // the last request failed because its answer was too big for the device
  int failedInARow = 0;      // requests that failed, one after the other, with the server still there
};

}  // namespace

std::string conflictPath(const std::string& path, const std::string& date) {
  const size_t slash = path.find_last_of('/');
  const size_t dot = path.find_last_of('.');
  const size_t at = dot != std::string::npos && (slash == std::string::npos || dot > slash + 1) ? dot : path.size();
  return path.substr(0, at) + " (conflict " + date + ")" + path.substr(at);
}

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

bool syncOnce(const Transport& net, const FileStore& fs, const Key master, const SyncOptions& options, SyncStats& stats,
              std::string& error, const SyncProgress& progress) {
  stats = SyncStats{};
  error.clear();
  Run run(net, fs, master, options, stats, error, progress);
  return run.run();
}

}  // namespace livesync
