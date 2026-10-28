#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <map>
#include <random>
#include <string>

#include "LiveSyncCrypto.h"
#include "LiveSyncIds.h"
#include "LiveSyncPull.h"
#include "LiveSyncPush.h"

namespace {

// A CouchDB with GET/PUT of single docs, _bulk_docs and revision checks.
struct FakeCouch {
  std::map<std::string, std::string> docs;  // id -> JSON with _rev
  int revs = 0;
  std::string decode(const std::string& p) {
    std::string out;
    for (size_t i = 0; i < p.size(); ++i) {
      if (p[i] == '%' && i + 2 < p.size()) {
        out += static_cast<char>(std::stoi(p.substr(i + 1, 2), nullptr, 16));
        i += 2;
      } else {
        out += p[i];
      }
    }
    return out;
  }
  std::string store(JsonDocument& d) {
    const std::string rev = std::to_string(++revs) + "-x";
    d["_rev"] = rev;
    std::string text;
    serializeJson(d, text);
    docs[d["_id"] | ""] = text;
    return rev;
  }
  int handle(const char* method, const std::string& path, const std::string& body, std::string& out) {
    const std::string m = method;
    if (m == "POST" && path == "_bulk_docs") {
      JsonDocument req, res;
      deserializeJson(req, body);
      JsonArray rows = res.to<JsonArray>();
      for (JsonObject d : req["docs"].as<JsonArray>()) {
        JsonObject r = rows.add<JsonObject>();
        r["id"] = d["_id"];
        if (docs.count(d["_id"] | "")) {
          r["error"] = "conflict";
          continue;
        }
        JsonDocument copy;
        copy.set(d);
        r["rev"] = store(copy);
      }
      serializeJson(res, out);
      return 201;
    }
    const std::string id = decode(path);
    if (m == "GET") {
      if (!docs.count(id)) return 404;
      out = docs[id];
      return 200;
    }
    if (m == "PUT") {
      JsonDocument d;
      deserializeJson(d, body);
      if (docs.count(id)) {
        JsonDocument cur;
        deserializeJson(cur, docs[id]);
        if (std::string(d["_rev"] | "") != std::string(cur["_rev"] | "")) return 409;
      } else if (!(d["_rev"] | std::string()).empty()) {
        return 409;
      }
      out = "{\"ok\":true,\"rev\":\"" + store(d) + "\"}";
      return 201;
    }
    return 400;
  }
};

struct Fixture {
  FakeCouch couch;
  livesync::Transport net;
  livesync::Key master{};
  livesync::PushIdentity identity{livesync::hashedPassphrase("pass phrase"), livesync::passphraseDigest("pass phrase")};
  std::mt19937 gen{42};
  livesync::RandomFill random = [this](uint8_t* out, size_t n) {
    for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>(gen());
  };
  Fixture() {
    for (int i = 0; i < 32; ++i) master[i] = static_cast<uint8_t>(i * 7);
    net.request = [this](const char* m, const std::string& p, const std::string& b, std::string& o) {
      return couch.handle(m, p, b, o);
    };
  }
  // The note as a puller sees it.
  std::string read(const std::string& id, livesync::FileEntry& entry) {
    JsonDocument doc;
    deserializeJson(doc, couch.docs[id]);
    livesync::DocKind kind;
    EXPECT_EQ(livesync::readFileDoc(doc.as<JsonObjectConst>(), master, entry, kind), livesync::Result::Ok);
    std::string content;
    livesync::assemble(
        entry, master,
        [&](const std::string& c, std::string& data, bool& enc) {
          JsonDocument d;
          deserializeJson(d, couch.docs[c]);
          data = d["data"] | "";
          enc = d["e_"] | false;
          return true;
        },
        [&](const char* p, size_t n) {
          content.append(p, n);
          return true;
        });
    return content;
  }
};

}  // namespace

TEST(LiveSyncPush, NewNoteReadsBackExactly) {
  Fixture f;
  livesync::PushItem item;
  item.path = "Folder/Νέα σημείωση.md";
  for (int i = 0; i < 300; ++i) item.content += "Γραμμή " + std::to_string(i) + " με ελληνικά και English.\n";
  std::string id, rev, error;
  ASSERT_EQ(livesync::pushNote(f.net, f.master, f.identity, f.random, item, 1793100000000, id, rev, error),
            livesync::PushResult::Pushed)
      << error;
  EXPECT_EQ(id, livesync::noteId(f.identity.passDigest, item.path));
  livesync::FileEntry entry;
  EXPECT_EQ(f.read(id, entry), item.content);
  EXPECT_EQ(entry.path, item.path);
  EXPECT_EQ(entry.size, item.content.size());
  EXPECT_GT(entry.children.size(), 10u);  // split into pieces under 1000 bytes
}

TEST(LiveSyncPush, UpdatesOnlyOnTopOfTheLastPulledRevision) {
  Fixture f;
  livesync::PushItem item;
  item.path = "Note.md";
  item.content = "one\n";
  std::string id, rev, error;
  ASSERT_EQ(livesync::pushNote(f.net, f.master, f.identity, f.random, item, 1000, id, rev, error),
            livesync::PushResult::Pushed);
  // An edit on top of that revision.
  item.knownId = id;
  item.knownRev = rev;
  item.content = "two\n";
  std::string rev2;
  ASSERT_EQ(livesync::pushNote(f.net, f.master, f.identity, f.random, item, 2000, id, rev2, error),
            livesync::PushResult::Pushed);
  livesync::FileEntry entry;
  EXPECT_EQ(f.read(id, entry), "two\n");
  // Built on the old revision again: the server moved on, nothing is written.
  item.content = "three\n";
  std::string rev3;
  EXPECT_EQ(livesync::pushNote(f.net, f.master, f.identity, f.random, item, 3000, id, rev3, error),
            livesync::PushResult::Conflict);
  EXPECT_EQ(f.read(id, entry), "two\n");
  // A note made on the device under a name the server already has: also a conflict.
  livesync::PushItem fresh;
  fresh.path = "note.md";  // ids ignore case
  fresh.content = "x";
  EXPECT_EQ(livesync::pushNote(f.net, f.master, f.identity, f.random, fresh, 4000, id, rev3, error),
            livesync::PushResult::Conflict);
}

TEST(LiveSyncPush, DeleteMarksTheNoteDeleted) {
  Fixture f;
  livesync::PushItem item;
  item.path = "Gone.md";
  item.content = "bye";
  std::string id, rev, error;
  ASSERT_EQ(livesync::pushNote(f.net, f.master, f.identity, f.random, item, 1000, id, rev, error),
            livesync::PushResult::Pushed);
  item.deleted = true;
  item.knownId = id;
  item.knownRev = rev;
  std::string rev2;
  ASSERT_EQ(livesync::pushNote(f.net, f.master, f.identity, f.random, item, 2000, id, rev2, error),
            livesync::PushResult::Pushed);
  livesync::FileEntry entry;
  f.read(id, entry);
  EXPECT_TRUE(entry.deleted);
  // Deleting it again: nothing to do.
  item.knownRev = rev2;
  EXPECT_EQ(livesync::pushNote(f.net, f.master, f.identity, f.random, item, 3000, id, rev, error),
            livesync::PushResult::Skipped);
}

TEST(LiveSyncPush, EncryptDecryptRoundTrip) {
  Fixture f;
  std::string box, plain;
  ASSERT_TRUE(livesync::encrypt(f.master, "hello κόσμε", f.random, box));
  EXPECT_EQ(box.substr(0, 2), "%=");
  ASSERT_EQ(livesync::decrypt(f.master, box, plain), livesync::Result::Ok);
  EXPECT_EQ(plain, "hello κόσμε");
}
