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
