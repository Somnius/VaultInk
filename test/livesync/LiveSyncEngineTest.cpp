#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "FakeCouch.h"
#include "LiveSyncEngine.h"

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
    if (mentions(body, chunks) && failures == 0) return ++failures, 503;
    return 0;
  };
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(failures, 1);
  EXPECT_EQ(stats.skipped, 0);
  EXPECT_TRUE(f.sd.files["/Vaults/Demo/Board/a.png"] == image);
  EXPECT_FALSE(f.sd.files.count(f.skipList));
  EXPECT_EQ(f.logged("chunks:").rfind("chunks: HTTP 503, ", 0), 0u) << f.logged("chunks:");
}

// A request with no answer at all is the transport's to send again (it does, once, on a new
// connection): the engine does not repeat it, or a dead server would be waited on twice as long.
// The server still answers the small request after it, so only this file is left for the next sync.
TEST(LiveSyncEngine, RequestWithNoAnswerIsNotRepeatedByTheEngine) {
  Fixture f;
  const std::string image = noise(300 * 1024, 3);
  const auto chunks = f.couch.addFile(f.master, "Board/a.png", image, 20 * 1024);
  int asked = 0;
  bool broken = true;
  f.couch.intercept = [&](const std::string& body, std::string&) {
    if (!mentions(body, chunks)) return 0;
    ++asked;
    return broken ? -1 : 0;
  };
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(asked, 1);
  EXPECT_EQ(stats.skipped, 1);
  EXPECT_EQ(f.logged("chunks:"),
            "chunks: no HTTP answer (-1), type application/json, length chunked, got 0 bytes, body ended early, "
            "file \"Board/a.png\"");
  broken = false;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.skipped, 0);
  EXPECT_TRUE(f.sd.files["/Vaults/Demo/Board/a.png"] == image);
}

// The steps of a sync are told (the device logs its free memory with each).
TEST(LiveSyncEngine, StagesAreTold) {
  Fixture f;
  std::vector<std::string> stages;
  f.options.stage = [&](const std::string& s) { stages.push_back(s); };
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  ASSERT_GE(stages.size(), 4u);
  EXPECT_EQ(stages.front(), "state loaded, 0 files known, 0 revisions");
  EXPECT_EQ(stages[1], "change page 1 read, 7 rows");
  EXPECT_EQ(
      stages.back().rfind("change page " + std::to_string(stats.pages) + " applied, 13 files known, 0 skipped", 0), 0u)
      << stages.back();
  int batches = 0;
  for (const auto& s : stages) batches += s.rfind("file records ", 0) == 0;
  EXPECT_GT(batches, 0);
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
  // Cut once, with the transport saying so: logged, and written by the next sync.
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
    // The transport sends such a request again itself; the engine does not repeat it. Here the
    // transport is a plain function that did not, so the file waits for the next sync.
    EXPECT_EQ(cuts, 1);
    EXPECT_EQ(stats.skipped, 1);
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
    EXPECT_EQ(error, "No answer to a document request.");
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
  // Three chunks in one request, then one at a time, each asked once: a single chunk is the least.
  int n = 0;
  for (const auto& line : f.log) n += line.rfind("chunks: answer over", 0) == 0;
  EXPECT_EQ(n, 2);
}

TEST(LiveSyncEngine, FullSkipListStopsTheSyncBeforeTheCheckpointMoves) {
  Fixture f;
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;  // the demo vault is in; its checkpoint is saved
  const std::string before = f.checkpoint();
  f.options.maxSkipList = 2;
  f.options.maxFileBytes = 10;
  for (int i = 0; i < 4; ++i) f.couch.addFile(f.master, "big" + std::to_string(i) + ".bin", noise(100, 1), 50);
  // Four files that cannot be listed in two lines: the sync says so and the page is asked again
  // next time (a file counted but not listed used to be passed by the checkpoint for good).
  EXPECT_FALSE(f.sync(stats, error));
  EXPECT_NE(error.find("More than 2 files could not be fetched"), std::string::npos) << error;
  EXPECT_EQ(f.checkpoint(), before);
  EXPECT_EQ(stats.skipped, 2);

  // With room for them the same four are found again: none was lost.
  f.options.maxSkipList = 2000;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.skipped, 4);
  const std::string list = f.sd.files[f.skipList];
  EXPECT_EQ(std::count(list.begin(), list.end(), '\n'), 4);
  EXPECT_EQ(f.checkpoint(), std::to_string(f.couch.order.size()));
}

// A proxy that refuses every POST while the small GET still works: the sync stops, it does not
// skip the whole vault and report success.
TEST(LiveSyncEngine, RequestsFailingInARowStopTheSync) {
  Fixture f;
  f.options.maxFailuresInARow = 5;
  int refused = 0;
  f.couch.intercept = [&](const std::string&, std::string& out) {
    ++refused;
    out = "<html>403 Forbidden</html>";
    return 403 + 2;  // 405: 401 and 403 stop the sync on their own
  };
  livesync::SyncStats stats;
  std::string error;
  EXPECT_FALSE(f.sync(stats, error));
  EXPECT_EQ(error,
            "6 document requests in a row failed (the server answered 405). The sync stopped; nothing was "
            "left out.");
  EXPECT_EQ(refused, 12);  // each asked twice
  EXPECT_EQ(f.checkpoint(), "");
  EXPECT_FALSE(f.sd.files.count(f.skipList));

  // Under the limit, with an answered request in between, files are skipped as before.
  Fixture g;
  const auto bad = g.couch.addFile(g.master, "Board/bad.png", noise(40 * 1024, 5), 20 * 1024);
  g.options.maxFailuresInARow = 1;
  g.couch.intercept = [&](const std::string& body, std::string&) { return mentions(body, bad) ? 500 : 0; };
  ASSERT_TRUE(g.sync(stats, error)) << error;
  EXPECT_EQ(stats.skipped, 1);
}

// Answers too big for the device are about one file each: they never add up to a stopped sync.
TEST(LiveSyncEngine, TooBigAnswersDoNotCountAsAServerThatRefuses) {
  Fixture f;
  f.options.maxFailuresInARow = 2;
  std::vector<std::string> wide;
  for (int i = 0; i < 6; ++i) {
    const auto ids = f.couch.addFile(f.master, "wide" + std::to_string(i) + ".png", noise(600, i), 300);
    wide.insert(wide.end(), ids.begin(), ids.end());
  }
  f.couch.intercept = [&](const std::string& body, std::string&) {
    return mentions(body, wide) ? livesync::STATUS_TOO_BIG : 0;
  };
  livesync::SyncStats stats;
  std::string error;
  ASSERT_TRUE(f.sync(stats, error)) << error;
  EXPECT_EQ(stats.skipped, 6);
}

// A batch whose answer is too big (or leaves no memory to read it) is asked again in halves, down
// to one chunk a request, and the file is written; only a single chunk that is too big skips it.
TEST(LiveSyncEngine, TooBigBatchIsAskedAgainInSmallerBatches) {
  for (const int status : {livesync::STATUS_TOO_BIG, livesync::STATUS_NO_MEMORY}) {
    Fixture f;
    const std::string image = noise(16 * 20 * 1024, 4);
    const auto chunks = f.couch.addFile(f.master, "Board/a.png", image, 20 * 1024);  // 16 chunks, 8 a request
    std::vector<size_t> asked;
    f.couch.intercept = [&](const std::string& body, std::string&) {
      if (!mentions(body, chunks)) return 0;
      JsonDocument req;
      deserializeJson(req, body);
      asked.push_back(req["keys"].as<JsonArrayConst>().size());
      return asked.back() > 2 ? status : 0;  // the device can hold two chunks, no more
    };
    livesync::SyncStats stats;
    std::string error;
    ASSERT_TRUE(f.sync(stats, error)) << error;
    EXPECT_EQ(stats.skipped, 0);
    EXPECT_TRUE(f.sd.files["/Vaults/Demo/Board/a.png"] == image);
    ASSERT_GE(asked.size(), 3u);
    EXPECT_EQ(asked[0], 8u);
    EXPECT_EQ(asked[1], 4u);
    EXPECT_EQ(asked[2], 2u);
    EXPECT_EQ(asked.size(), 2u + 8u);  // then two at a time for all sixteen
    EXPECT_NE(f.logged("asking again"), "");
    EXPECT_FALSE(f.sd.files.count(f.skipList));
  }
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
