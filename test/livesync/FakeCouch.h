#pragma once
// Shared by the LiveSync host tests.
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "LiveSyncEngine.h"

// A fake CouchDB serving the recorded LiveSync database (test/livesync/fixture), and an in-memory SD.
namespace {

std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

struct FakeCouch {
  std::vector<std::string> order;           // _changes order
  std::map<std::string, std::string> docs;  // id -> doc JSON
  std::map<std::string, bool> deleted;
  std::string params, version;
  int requests = 0;
  int failChangesAfter = -1;  // fail the Nth _changes call

  FakeCouch() {
    JsonDocument all;
    deserializeJson(all, readFile(std::string(LIVESYNC_FIXTURE_DIR) + "/docs.json"));
    for (JsonObjectConst d : all.as<JsonArrayConst>()) {
      const std::string id = d["_id"] | "";
      std::string json;
      serializeJson(d, json);
      docs[id] = json;
      order.push_back(id);
      if (id == "obsydian_livesync_version") version = json;
    }
    params = readFile(std::string(LIVESYNC_FIXTURE_DIR) + "/sync_parameters.json");
  }

  // A later change to `id`: it moves to the end of the feed.
  void touch(const std::string& id, bool del) {
    *std::find(order.begin(), order.end(), id) = "";  // its old place in the feed is gone
    order.push_back(id);
    deleted[id] = del;
  }

  // A file made here (not in the recorded database): a record with a plain path and its chunks,
  // each one an encrypted "%=" string as the plugin stores it. Returns the chunk ids.
  std::vector<std::string> addFile(const livesync::Key master, const std::string& path, const std::string& bytes,
                                   size_t pieceSize) {
    const std::string n = std::to_string(order.size());
    const livesync::RandomFill random = [this](uint8_t* out, size_t len) {
      for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>((seed = seed * 1664525u + 1013904223u) >> 24);
    };
    JsonDocument file;
    file["_id"] = "f:made" + n;
    file["_rev"] = "1-made";
    file["path"] = path;
    file["type"] = "newnote";
    file["size"] = bytes.size();
    JsonArray children = file["children"].to<JsonArray>();
    std::vector<std::string> ids;
    for (size_t at = 0; at < bytes.size(); at += pieceSize) {
      const std::string slice = bytes.substr(at, pieceSize);
      std::string data;
      livesync::encrypt(master, livesync::base64Encode(reinterpret_cast<const uint8_t*>(slice.data()), slice.size()),
                        random, data);
      longestData = std::max(longestData, data.size());
      JsonDocument chunk;
      const std::string id = "h:made" + n + "-" + std::to_string(ids.size());
      chunk["_id"] = id;
      chunk["type"] = "leaf";
      chunk["data"] = data;
      chunk["e_"] = true;
      serializeJson(chunk, docs[id]);
      children.add(id);
      ids.push_back(id);
    }
    serializeJson(file, docs["f:made" + n]);
    order.push_back("f:made" + n);
    return ids;
  }

  uint32_t seed = 1;
  size_t longestData = 0;
  // Called for each `_all_docs` request before the database answers; a result other than 0 is the
  // status returned (with whatever it put in `out` and `info`).
  std::function<int(const std::string& body, std::string& out)> intercept;
  livesync::ResponseInfo info;  // what the transport reports about the last answer
  bool down = false;            // no request gets an answer
  size_t biggestAnswer = 0;
  size_t mostKeys = 0;
  int docRequests = 0;

  int handle(const char* method, const std::string& path, const std::string& body, std::string& out) {
    info = livesync::ResponseInfo{};
    info.contentType = "application/json";
    info.chunked = true;
    out.clear();
    int status = -1;
    if (!down) {
      if (intercept && path.rfind("_all_docs", 0) == 0) status = intercept(body, out);
      if (status == 0 || !(intercept && path.rfind("_all_docs", 0) == 0)) status = answer(method, path, body, out);
    }
    info.received = out.size();
    info.complete = status >= 0;
    biggestAnswer = std::max(biggestAnswer, out.size());
    return status;
  }

  int answer(const char* method, const std::string& path, const std::string& body, std::string& out) {
    ++requests;
    if (path == "obsydian_livesync_version") return out = version, 200;
    if (path == "_local/obsidian_livesync_sync_parameters") return out = params, 200;
    if (path.rfind("_changes", 0) == 0) {
      static int calls = 0;
      if (failChangesAfter >= 0 && calls++ >= failChangesAfter) return -1;
      const size_t s = path.find("since=") + 6;
      const int since = std::stoi(path.substr(s, path.find('&', s) - s));
      const int limit = std::stoi(path.substr(path.find("limit=") + 6));
      JsonDocument res;
      JsonArray rows = res["results"].to<JsonArray>();
      int i = since;
      for (; i < static_cast<int>(order.size()) && i < since + limit; ++i) {
        if (order[i].empty()) continue;
        JsonObject r = rows.add<JsonObject>();
        r["id"] = order[i];
        if (deleted[order[i]]) r["deleted"] = true;
      }
      res["last_seq"] = std::to_string(i);
      res["pending"] = static_cast<int>(order.size()) - i;
      serializeJson(res, out);
      return 200;
    }
    if (std::string(method) == "POST" && path.rfind("_all_docs", 0) == 0) {
      JsonDocument req, res;
      deserializeJson(req, body);
      ++docRequests;
      mostKeys = std::max(mostKeys, req["keys"].as<JsonArrayConst>().size());
      JsonArray rows = res["rows"].to<JsonArray>();
      for (JsonVariantConst k : req["keys"].as<JsonArrayConst>()) {
        const std::string id = k | "";
        JsonObject r = rows.add<JsonObject>();
        r["key"] = id;
        if (docs.count(id) && !deleted[id]) {
          JsonDocument d;
          deserializeJson(d, docs[id]);
          r["doc"] = d;
        } else {
          r["error"] = "not_found";
        }
      }
      serializeJson(res, out);
      return 200;
    }
    return 404;
  }
};

struct MemSd {
  std::map<std::string, std::string> files;
  std::string pendingPath, pending;
  livesync::FileStore fs() {
    livesync::FileStore s;
    s.exists = [this](const std::string& p) { return files.count(p) > 0; };
    s.readAll = [this](const std::string& p, std::string& out) {
      if (!files.count(p)) return false;
      out = files[p];
      return true;
    };
    s.writeAll = [this](const std::string& p, const std::string& t) { return files[p] = t, true; };
    s.begin = [this](const std::string& p) { return pendingPath = p, pending.clear(), true; };
    s.append = [this](const char* b, size_t n) { return pending.append(b, n), true; };
    s.commit = [this](bool keep) {
      if (keep) files[pendingPath] = pending;
      return true;
    };
    s.move = [this](const std::string& from, const std::string& to) {
      files[to] = files[from];
      files.erase(from);
      return true;
    };
    return s;
  }
};

struct Fixture {
  FakeCouch couch;
  MemSd sd;
  livesync::Key master{};
  livesync::SyncOptions options;
  livesync::Transport net;
  Fixture() {
    JsonDocument p;
    deserializeJson(p, couch.params);
    options.saltB64 = p["pbkdf2salt"] | "";
    std::string salt;
    livesync::base64Decode(options.saltB64, salt);
    livesync::deriveMaster("vaultink-demo-pass", reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), master);
    options.vaultDir = "/Vaults/Demo";
    options.stateDir = "/.vaultink/x/livesync";
    options.changesPage = 7;  // several pages over the 36 docs
    net.request = [this](const char* m, const std::string& path, const std::string& body, std::string& out) {
      return couch.handle(m, path, body, out);
    };
    net.info = [this] { return couch.info; };
    options.logError = [this](const std::string& line) { log.push_back(line); };
  }
  std::vector<std::string> log;
  // The first logged line holding `text`, or "".
  std::string logged(const std::string& text) const {
    for (const auto& line : log) {
      if (line.find(text) != std::string::npos) return line;
    }
    return "";
  }
  const std::string skipList = "/.vaultink/x/livesync/skipped.txt";
  std::string checkpoint() { return sd.files["/.vaultink/x/livesync/checkpoint"]; }
  bool sync(livesync::SyncStats& stats, std::string& error) {
    return livesync::syncOnce(net, sd.fs(), master, options, stats, error);
  }
};

}  // namespace

// Bytes with no pattern a chunk could repeat.
inline std::string noise(size_t n, uint32_t seed) {
  std::string s(n, '\0');
  for (auto& c : s) c = static_cast<char>((seed = seed * 1103515245u + 12345u) >> 16);
  return s;
}
