#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "LiveSyncCrypto.h"
#include "LiveSyncIds.h"
#include "LiveSyncPull.h"

namespace {
constexpr const char* PASSPHRASE = "vaultink-demo-pass";
std::string fixture(const char* name) {
  std::ifstream f(std::string(LIVESYNC_FIXTURE_DIR) + "/" + name, std::ios::binary);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}
}  // namespace

TEST(LiveSyncIds, Xxh64KnownValues) {
  // Reference values of the xxHash spec (seed 0).
  EXPECT_EQ(livesync::xxh64("", 0), 0xEF46DB3751D8E999ULL);
  EXPECT_EQ(livesync::xxh64("a", 1), 0xD24EC4F1A98C6E5BULL);
  const std::string longer = "Nobody inspects the spammish repetition";
  EXPECT_EQ(livesync::xxh64(longer.data(), longer.size()), 0xFBCEA83C8A378BF1ULL);
}

TEST(LiveSyncIds, MatchEveryIdInTheRecordedDatabase) {
  JsonDocument params;
  ASSERT_EQ(deserializeJson(params, fixture("sync_parameters.json")), DeserializationError::Ok);
  std::string salt;
  ASSERT_TRUE(livesync::base64Decode(params["pbkdf2salt"] | "", salt));
  livesync::Key master;
  ASSERT_TRUE(livesync::deriveMaster(PASSPHRASE, reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), master));
  const std::string hashed = livesync::hashedPassphrase(PASSPHRASE);
  const std::string digest = livesync::passphraseDigest(PASSPHRASE);

  JsonDocument docs;
  ASSERT_EQ(deserializeJson(docs, fixture("docs.json")), DeserializationError::Ok);
  int chunks = 0, notes = 0;
  for (JsonObjectConst d : docs.as<JsonArrayConst>()) {
    const std::string id = d["_id"] | "";
    if (id.rfind("h:+", 0) == 0) {
      std::string piece;
      ASSERT_EQ(livesync::decrypt(master, d["data"] | "", piece), livesync::Result::Ok) << id;
      EXPECT_EQ(livesync::chunkId(piece, hashed), id);
      ++chunks;
    } else {
      livesync::FileEntry entry;
      livesync::DocKind kind;
      if (livesync::readFileDoc(d, master, entry, kind) != livesync::Result::Ok || kind != livesync::DocKind::File) {
        continue;
      }
      EXPECT_EQ(livesync::noteId(digest, entry.path), id) << entry.path;
      ++notes;
    }
  }
  EXPECT_GT(chunks, 10);
  EXPECT_EQ(notes, 13);
}

// Vectors from the plugin's idDerivation.unit.spec.ts (key and expected ids).
namespace {
constexpr const char* V1_KEY = "f3205cc41d24116d8c2484993c9d9a2e667373af338ba02f2ee71199adb82f2e";
}

TEST(LiveSyncIdsV1, ChunkIdsMatchThePluginVectors) {
  EXPECT_EQ(livesync::chunkIdV1("sample chunk", V1_KEY),
            "h:+5868bc5fff9c1537ad5e6dfdf016b46daad9cf3827cb607150aa9e3feaaf36ce");
  EXPECT_EQ(livesync::chunkIdV1("試験用の文字列📄", V1_KEY),
            "h:+e779967dd509ab655bea444877337860a1d4eeba718995df2546050d75f06d2f");
  // Contents are not normalised: NFD and NFC text give different ids.
  EXPECT_NE(livesync::chunkIdV1("cafe\xCC\x81", V1_KEY), livesync::chunkIdV1("caf\xC3\xA9", V1_KEY));
  // Another key gives another id.
  EXPECT_NE(livesync::chunkIdV1("sample chunk", std::string(64, 'a')), livesync::chunkIdV1("sample chunk", V1_KEY));
}

TEST(LiveSyncIdsV1, NoteIdMatchesThePluginVectorAndLowercases) {
  // "document" purpose over the value "sample chunk" (already lowercase).
  EXPECT_EQ(livesync::noteIdV1(V1_KEY, "sample chunk", false),
            "f:a2a525f9d994e83f29588ad43513be468e137135a88689cd0790e48834d9d3f3");
  EXPECT_EQ(livesync::noteIdV1(V1_KEY, "Sample Chunk", false), livesync::noteIdV1(V1_KEY, "sample chunk", false));
}

TEST(LiveSyncIdsV1, PathNormalizationNfcComposesBeforeTheId) {
  const std::string nfd = "Caf\x65\xCC\x81.md", nfc = "Caf\xC3\xA9.md";
  EXPECT_EQ(livesync::noteIdV1(V1_KEY, nfd, true), livesync::noteIdV1(V1_KEY, nfc, true));
  EXPECT_NE(livesync::noteIdV1(V1_KEY, nfd, false), livesync::noteIdV1(V1_KEY, nfc, false));
}

TEST(LiveSyncIdsV1, BadKeysAreRefused) {
  EXPECT_TRUE(livesync::isIdKeyHex(V1_KEY));
  EXPECT_FALSE(livesync::isIdKeyHex(""));
  EXPECT_FALSE(livesync::isIdKeyHex("F3205cc41d24116d8c2484993c9d9a2e667373af338ba02f2ee71199adb82f2e"));
  EXPECT_FALSE(livesync::isIdKeyHex(std::string(63, 'a')));
  EXPECT_EQ(livesync::noteIdV1("wrong", "a.md", false), "");
  EXPECT_EQ(livesync::chunkIdV1("x", "wrong"), "");
}
