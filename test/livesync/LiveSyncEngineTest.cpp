#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
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

  int handle(const char* method, const std::string& path, const std::string& body, std::string& out) {
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
  }
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
