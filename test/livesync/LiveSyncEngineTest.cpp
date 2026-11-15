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

TEST(LiveSyncEngine, FirstSyncWritesTheWholeVault) {
  Fixture f;
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.written, 13);
  EXPECT_GT(stats.pages, 1);
  const std::string demo = DEMO_VAULT_DIR;
  int compared = 0;
  for (const auto& [path, content] : f.sd.files) {
    if (path.rfind("/Vaults/Demo/", 0) != 0) continue;
    EXPECT_TRUE(content == readFile(demo + "/" + path.substr(13))) << path;
    ++compared;
  }
  EXPECT_EQ(compared, 13);
  EXPECT_EQ(f.sd.files["/.vaultink/x/livesync/checkpoint"], std::to_string(f.couch.order.size()));

  // Nothing new: nothing written.
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.written, 0);
}

TEST(LiveSyncEngine, RemoteDeleteGoesToTheTrash) {
  Fixture f;
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  // The file a map line points at, deleted on the server.
  const std::string map = f.sd.files["/.vaultink/x/livesync/files.tsv"];
  const std::string id = map.substr(0, map.find('\t'));
  const std::string path = map.substr(map.find('\t') + 1, map.find('\n') - map.find('\t') - 1);
  f.couch.touch(id, true);
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.trashed, 1);
  EXPECT_FALSE(f.sd.files.count("/Vaults/Demo/" + path));
  EXPECT_TRUE(f.sd.files.count("/.vaultink/x/livesync/trash/" + path));
}

TEST(LiveSyncEngine, StopsOnChangedSaltOrNewerRemote) {
  Fixture f;
  livesync::SyncStats stats;
  std::string error;
  f.options.saltB64 = "AAAA";
  EXPECT_FALSE(f.sync(stats, error));
  EXPECT_NE(error.find("passphrase changed"), std::string::npos);
  Fixture g;
  g.couch.version = R"({"_id":"obsydian_livesync_version","version":14})";
  EXPECT_FALSE(g.sync(stats, error));
  EXPECT_NE(error.find("newer LiveSync"), std::string::npos);
  EXPECT_TRUE(g.sd.files.empty());
}

TEST(LiveSyncEngine, FailureKeepsTheCheckpoint) {
  Fixture f;
  f.couch.failChangesAfter = 2;  // the third page fails
  livesync::SyncStats stats;
  std::string error;
  EXPECT_FALSE(f.sync(stats, error));
  const std::string saved = f.sd.files["/.vaultink/x/livesync/checkpoint"];
  EXPECT_EQ(saved, "14");  // two full pages of 7 were applied
  f.couch.failChangesAfter = -1;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(f.sd.files["/.vaultink/x/livesync/checkpoint"], std::to_string(f.couch.order.size()));
}

TEST(LiveSyncEngine, SafePaths) {
  EXPECT_TRUE(livesync::safeVaultPath("Notes/Πρόσωπα/Άννα.md"));
  EXPECT_TRUE(livesync::safeVaultPath("Dr. Smith.md"));
  EXPECT_FALSE(livesync::safeVaultPath("../escape.md"));
  EXPECT_FALSE(livesync::safeVaultPath("/abs.md"));
  EXPECT_FALSE(livesync::safeVaultPath("a//b.md"));
  EXPECT_FALSE(livesync::safeVaultPath("a/../b.md"));
  EXPECT_FALSE(livesync::safeVaultPath("what?.md"));
}

TEST(LiveSyncEngine, LocalChangesAreNeverOverwritten) {
  Fixture f;
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  const std::string map = f.sd.files["/.vaultink/x/livesync/files.tsv"];
  // Two notes from the map: one edited remotely, one deleted remotely, both changed on the device.
  const size_t second = map.find('\n') + 1;
  const std::string idA = map.substr(0, map.find('\t'));
  const std::string pathA = map.substr(map.find('\t') + 1, map.find('\n') - map.find('\t') - 1);
  const std::string idB = map.substr(second, map.find('\t', second) - second);
  const std::string pathB = map.substr(map.find('\t', second) + 1, map.find('\n', second) - map.find('\t', second) - 1);
  f.sd.files["/Vaults/Demo/" + pathA] = "edited on the device A";
  f.sd.files["/Vaults/Demo/" + pathB] = "edited on the device B";
  f.options.locallyChanged = [&](const std::string& p) { return p == pathA || p == pathB; };
  f.options.conflictDate = "2026-10-27";
  f.couch.touch(idA, false);
  f.couch.touch(idB, true);
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.conflicts, 2);
  EXPECT_EQ(stats.trashed, 0);
  EXPECT_EQ(f.sd.files["/Vaults/Demo/" + pathA], "edited on the device A");
  EXPECT_EQ(f.sd.files["/Vaults/Demo/" + pathB], "edited on the device B");
  EXPECT_TRUE(f.sd.files.count("/Vaults/Demo/" + livesync::conflictPath(pathA, "2026-10-27")));
}

TEST(LiveSyncEngine, ConflictPath) {
  EXPECT_EQ(livesync::conflictPath("Folder/Note.md", "2026-10-27"), "Folder/Note (conflict 2026-10-27).md");
  EXPECT_EQ(livesync::conflictPath("Note", "2026-10-27"), "Note (conflict 2026-10-27)");
  EXPECT_EQ(livesync::conflictPath("a.b/.hidden", "D"), "a.b/.hidden (conflict D)");
  EXPECT_EQ(livesync::conflictPath("a.b/c.tar.gz", "D"), "a.b/c.tar (conflict D).gz");
}

// Bytes with no pattern a chunk could repeat.
static std::string noise(size_t n, uint32_t seed) {
  std::string s(n, '\0');
  for (auto& c : s) c = static_cast<char>((seed = seed * 1103515245u + 12345u) >> 16);
  return s;
}

static bool mentions(const std::string& body, const std::vector<std::string>& ids) {
  return std::any_of(ids.begin(), ids.end(), [&](const std::string& id) { return body.find(id) != std::string::npos; });
}

TEST(LiveSyncEngine, ChunksPerRequestFollowsTheFileSize) {
  livesync::SyncOptions o;
  livesync::FileEntry f;
  f.size = 3 * 1024 * 1024;
  f.children.assign(60, "h:x");  // 51 KB each: about 182 KB allowed per chunk, 8 fit
  EXPECT_EQ(livesync::chunksPerRequest(f, o), 8u);
  f.children.assign(12, "h:x");  // 256 KB each: about 911 KB allowed per chunk
  EXPECT_EQ(livesync::chunksPerRequest(f, o), 1u);
  f.children.assign(3, "h:x");  // 1 MB each: more than the target, still one at a time
  EXPECT_EQ(livesync::chunksPerRequest(f, o), 1u);
  f.size = 0;  // a record without a size: the plain cap
  EXPECT_EQ(livesync::chunksPerRequest(f, o), 8u);
  f.children.clear();
  EXPECT_EQ(livesync::chunksPerRequest(f, o), 8u);
}

TEST(LiveSyncEngine, ThreeMegabyteImageComesInBoundedBatches) {
  Fixture f;
  const std::string image = noise(3 * 1024 * 1024, 7);
  f.couch.addFile(f.master, "Board/Images/Image 18.png", image, 50 * 1024);  // 62 chunks
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.written, 14);
  EXPECT_EQ(stats.skipped, 0);
  EXPECT_TRUE(f.sd.files["/Vaults/Demo/Board/Images/Image 18.png"] == image);
  EXPECT_LE(f.couch.mostKeys, 10u);                 // the docs batch; chunks go 8 at a time
  EXPECT_LT(f.couch.biggestAnswer, 1536u * 1024u);  // no answer near the transport's cap
  EXPECT_GT(f.couch.biggestAnswer, 500u * 1024u);   // and the batches really held several chunks
  EXPECT_TRUE(f.log.empty());
}

// The X4 Pro case: one answer of about 230 kB whose rows hold `data` strings past 65,535
// characters, the most ArduinoJson keeps with 2-byte string lengths (its 32-bit default).
TEST(LiveSyncEngine, AnswerWithDataStringsOver65535Characters) {
  static_assert(ARDUINOJSON_STRING_LENGTH_SIZE >= 4, "the device builds with 4 too (platformio.ini)");
  Fixture f;
  const std::string image = noise(4 * 36 * 1024, 11);
  f.couch.addFile(f.master, "Board/Images/Image 18.png", image, 36 * 1024);
  EXPECT_GT(f.couch.longestData, 65535u);
  EXPECT_LT(f.couch.longestData, 70000u);
  size_t chunkAnswer = 0;
  f.couch.intercept = [&](const std::string& body, std::string& out) {
    if (body.find("h:made") == std::string::npos) return 0;
    const int status = f.couch.answer("POST", "_all_docs?include_docs=true", body, out);
    chunkAnswer = out.size();
    return status;
  };
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_GT(chunkAnswer, 220u * 1024u);  // all four chunks in one answer
  EXPECT_LT(chunkAnswer, 280u * 1024u);
  EXPECT_EQ(stats.skipped, 0);
  EXPECT_TRUE(f.sd.files["/Vaults/Demo/Board/Images/Image 18.png"] == image);
}

TEST(LiveSyncEngine, BatchThatFailsOnceIsAskedAgain) {
  Fixture f;
  const std::string image = noise(300 * 1024, 3);
  const auto chunks = f.couch.addFile(f.master, "Board/a.png", image, 20 * 1024);
  int failures = 0;
  f.couch.intercept = [&](const std::string& body, std::string&) {
    if (mentions(body, chunks) && failures == 0) return ++failures, -1;
    return 0;
  };
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(failures, 1);
  EXPECT_EQ(stats.skipped, 0);
  EXPECT_TRUE(f.sd.files["/Vaults/Demo/Board/a.png"] == image);
  EXPECT_FALSE(f.sd.files.count(f.skipList));
  EXPECT_EQ(f.logged("chunks:"),
            "chunks: no HTTP answer (-1), type application/json, length chunked, got 0 bytes, body ended early, "
            "file \"Board/a.png\"");
}

TEST(LiveSyncEngine, FileThatAlwaysFailsIsSkippedAndTriedNextTime) {
  Fixture f;
  const std::string bad = noise(200 * 1024, 5), good = noise(90 * 1024, 6);
  const auto badChunks = f.couch.addFile(f.master, "Board/bad.png", bad, 20 * 1024);
  f.couch.addFile(f.master, "Board/good.png", good, 20 * 1024);
  bool broken = true;
  int tries = 0;
  f.couch.intercept = [&](const std::string& body, std::string& out) {
    if (!broken || !mentions(body, badChunks)) return 0;
    ++tries;
    out = R"({"error":"timeout","reason":"The request could not be processed in a reasonable amount of time."})";
    return 500;
  };
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(tries, 2);  // once, and once more
  EXPECT_EQ(stats.written, 14);
  EXPECT_EQ(stats.skipped, 1);
  EXPECT_FALSE(f.sd.files.count("/Vaults/Demo/Board/bad.png"));
  EXPECT_TRUE(f.sd.files["/Vaults/Demo/Board/good.png"] == good);  // the file after it was still written
  EXPECT_EQ(f.checkpoint(), std::to_string(f.couch.order.size()));
  const std::string list = f.sd.files[f.skipList];
  EXPECT_EQ(list.rfind("Board/bad.png\tthe server answered 500\tf:made", 0), 0u) << list;
  EXPECT_NE(f.logged("chunks: HTTP 500").find("starts \"{\\\"error\\\":\\\"timeout\\\""), std::string::npos)
      << f.logged("chunks: HTTP 500");
  EXPECT_EQ(f.logged("skipped"), "skipped \"Board/bad.png\": the server answered 500");

  // Still broken: nothing new in the feed, the file is asked for again and stays on the list.
  tries = 0;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(tries, 2);
  EXPECT_EQ(stats.written, 0);
  EXPECT_EQ(stats.skipped, 1);
  EXPECT_EQ(f.sd.files[f.skipList], list);

  // The server recovered: the file arrives and leaves the list.
  broken = false;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.written, 1);
  EXPECT_EQ(stats.skipped, 0);
  EXPECT_TRUE(f.sd.files["/Vaults/Demo/Board/bad.png"] == bad);
  EXPECT_EQ(f.sd.files[f.skipList], "");
}

TEST(LiveSyncEngine, AnswerThatIsNotJsonIsLoggedByShape) {
  Fixture f;
  const auto chunks = f.couch.addFile(f.master, "Board/a.png", noise(50 * 1024, 9), 20 * 1024);
  f.couch.intercept = [&](const std::string& body, std::string& out) {
    if (!mentions(body, chunks)) return 0;
    out = "<html>\r\n<head><title>502 Bad Gateway</title></head>\r\n<body>\x01\xFF</body></html>";
    f.couch.info.contentType = "text/html";
    f.couch.info.chunked = false;
    f.couch.info.contentLength = static_cast<int64_t>(out.size());
    return 200;
  };
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.skipped, 1);
  EXPECT_EQ(f.logged("chunks:"),
            "chunks: HTTP 200, type text/html, length 75, got 75 bytes, body complete, JSON InvalidInput, "
            "starts \"<html>\\r\\n<head><title>502 Bad Gateway</title></head>\\r\\n<body>\\x01\\xFF</body></html>\", "
            "file \"Board/a.png\"");
  EXPECT_NE(f.sd.files[f.skipList].find("the server's answer was not readable JSON (InvalidInput)"), std::string::npos);
}

TEST(LiveSyncEngine, TruncatedBodies) {
  // Cut once, with the transport saying so: logged, asked again, written.
  {
    Fixture f;
    const std::string image = noise(100 * 1024, 13);
    const auto chunks = f.couch.addFile(f.master, "Board/a.png", image, 20 * 1024);
    int cuts = 0;
    bool early = false;
    f.couch.intercept = [&](const std::string& body, std::string& out) {
      early = false;
      if (!mentions(body, chunks) || cuts > 0) return 0;
      ++cuts;
      f.couch.answer("POST", "_all_docs?include_docs=true", body, out);
      out.resize(out.size() / 2);
      early = true;
      return 200;
    };
    f.net.info = [&] {
      livesync::ResponseInfo i = f.couch.info;
      i.complete = !early;
      return i;
    };
    livesync::SyncStats stats;
    std::string error;
    ASSERT_TRUE(f.sync(stats, error)) << error;
    EXPECT_EQ(stats.skipped, 0);
    EXPECT_TRUE(f.sd.files["/Vaults/Demo/Board/a.png"] == image);
    const std::string line = f.logged("chunks: HTTP 200");
    EXPECT_NE(line.find("body ended early"), std::string::npos) << line;
    EXPECT_EQ(line.find("JSON"), std::string::npos) << line;  // not parsed at all
  }
  // Always cut, and the transport does not notice: the parser reports it, the file is skipped.
  {
    Fixture f;
    const auto chunks = f.couch.addFile(f.master, "Board/a.png", noise(100 * 1024, 13), 20 * 1024);
    f.couch.intercept = [&](const std::string& body, std::string& out) {
      if (!mentions(body, chunks)) return 0;
      f.couch.answer("POST", "_all_docs?include_docs=true", body, out);
      out.resize(out.size() / 2);
      return 200;
    };
    livesync::SyncStats stats;
    std::string error;
    ASSERT_TRUE(f.sync(stats, error)) << error;
    EXPECT_EQ(stats.written, 13);
    EXPECT_EQ(stats.skipped, 1);
    EXPECT_FALSE(f.sd.files.count("/Vaults/Demo/Board/a.png"));
    EXPECT_NE(f.logged("chunks: HTTP 200").find("body complete, JSON IncompleteInput"), std::string::npos);
  }
}

TEST(LiveSyncEngine, MissingOrDamagedChunkSkipsOnlyThatFile) {
  Fixture f;
  const auto gone = f.couch.addFile(f.master, "Board/gone.png", noise(60 * 1024, 1), 20 * 1024);
  const auto damaged = f.couch.addFile(f.master, "Board/damaged.png", noise(60 * 1024, 2), 20 * 1024);
  f.couch.docs.erase(gone[1]);
  std::string& doc = f.couch.docs[damaged[2]];
  const size_t at = doc.find("%=") + 40;
  doc[at] = doc[at] == 'A' ? 'B' : 'A';
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.written, 13);
  EXPECT_EQ(stats.skipped, 2);
  const std::string list = f.sd.files[f.skipList];
  EXPECT_NE(list.find("Board/gone.png\ta piece of it is missing on the server\t"), std::string::npos) << list;
  EXPECT_NE(list.find("Board/damaged.png\ta piece of it could not be decrypted\t"), std::string::npos) << list;
  EXPECT_FALSE(f.sd.files.count("/Vaults/Demo/Board/gone.png"));
  EXPECT_FALSE(f.sd.files.count("/Vaults/Demo/Board/damaged.png"));
}

TEST(LiveSyncEngine, ServerGoneOrLoginRefusedStillStopsTheSync) {
  {
    Fixture f;
    const auto chunks = f.couch.addFile(f.master, "Board/a.png", noise(50 * 1024, 4), 20 * 1024);
    f.couch.intercept = [&](const std::string& body, std::string&) {
      if (mentions(body, chunks)) f.couch.down = true;  // from here on nothing answers
      return f.couch.down ? -1 : 0;
    };
    livesync::SyncStats stats;
    std::string error;
    EXPECT_FALSE(f.sync(stats, error));
    EXPECT_EQ(error, "Server answered -1 to a document request.");
    EXPECT_FALSE(f.sd.files.count(f.skipList));
    EXPECT_NE(f.checkpoint(), std::to_string(f.couch.order.size()));
  }
  {
    Fixture f;
    const auto chunks = f.couch.addFile(f.master, "Board/a.png", noise(50 * 1024, 4), 20 * 1024);
    int asked = 0;
    f.couch.intercept = [&](const std::string& body, std::string&) {
      return mentions(body, chunks) ? ++asked, 401 : 0;
    };
    livesync::SyncStats stats;
    std::string error;
    EXPECT_FALSE(f.sync(stats, error));
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(error, "The server refused the user name or password.");
    EXPECT_FALSE(f.sd.files.count(f.skipList));
  }
}

TEST(LiveSyncEngine, TooBigAndBadNamesAreListedNotFatal) {
  Fixture f;
  f.couch.addFile(f.master, "Board/what?.png", noise(1000, 1), 500);
  f.couch.addFile(f.master, "Board/huge.bin", noise(1100 * 1024, 2), 100 * 1024);
  const auto wide = f.couch.addFile(f.master, "Board/wide.png", noise(3000, 3), 1000);
  f.options.maxFileBytes = 1024 * 1024;
  f.couch.intercept = [&](const std::string& body, std::string&) {
    return mentions(body, wide) ? livesync::STATUS_TOO_BIG : 0;  // the transport's cap, asked once only
  };
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.written, 13);
  EXPECT_EQ(stats.skipped, 3);
  const std::string list = f.sd.files[f.skipList];
  EXPECT_NE(list.find("Board/what?.png\tits name cannot be stored on the SD card\t"), std::string::npos) << list;
  EXPECT_NE(list.find("Board/huge.bin\tbigger than 1 MB\t"), std::string::npos) << list;
  EXPECT_NE(list.find("Board/wide.png\tthe server's answer was too big\t"), std::string::npos) << list;
  EXPECT_EQ(f.logged("chunks:").rfind("chunks: answer over the size cap", 0), 0u);
  int n = 0;
  for (const auto& line : f.log) n += line.rfind("chunks:", 0) == 0;
  EXPECT_EQ(n, 1);
}

TEST(LiveSyncEngine, SkipListIsCapped) {
  Fixture f;
  f.options.maxSkipList = 2;
  f.options.maxFileBytes = 10;
  for (int i = 0; i < 4; ++i) f.couch.addFile(f.master, "big" + std::to_string(i) + ".bin", noise(100, 1), 50);
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  // The demo vault's own files are over 10 bytes too: all counted, two listed.
  EXPECT_GT(stats.skipped, 4);
  const std::string list = f.sd.files[f.skipList];
  EXPECT_EQ(std::count(list.begin(), list.end(), '\n'), 2);
}

TEST(LiveSyncEngine, DescribeResponse) {
  livesync::ResponseInfo info;
  info.contentType = "application/json";
  info.contentLength = 227636;
  info.received = 227636;
  EXPECT_EQ(livesync::describeResponse("chunks", 200, info, "{\"rows\":[", "NoMemory", false),
            "chunks: HTTP 200, type application/json, length 227636, got 227636 bytes, body complete, JSON NoMemory");
  info = livesync::ResponseInfo{};
  info.complete = false;
  EXPECT_EQ(livesync::describeResponse("docs", -1, info, "", nullptr, true),
            "docs: no HTTP answer (-1), type none, length none, got 0 bytes, body ended early");
  const std::string longBody(200, 'x');
  const std::string line = livesync::describeResponse("changes", 502, info, longBody, nullptr, true);
  EXPECT_NE(line.find("starts \"" + std::string(80, 'x') + "\""), std::string::npos);
}
