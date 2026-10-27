#include <gtest/gtest.h>

#include <string>

#include "LiveSyncCrypto.h"
#include "vectors.h"

namespace {

std::string toHex(const uint8_t* p, size_t n) {
  static constexpr char d[] = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; ++i) {
    s += d[p[i] >> 4];
    s += d[p[i] & 15];
  }
  return s;
}

struct Master {
  livesync::Key key{};
  Master() {
    std::string salt;
    EXPECT_TRUE(livesync::base64Decode(vectors::SALT_B64, salt));
    EXPECT_TRUE(
        livesync::deriveMaster(vectors::PASSPHRASE, reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), key));
  }
};

}  // namespace

TEST(LiveSyncCrypto, MasterKeyMatchesTheLibrary) {
  Master m;
  EXPECT_EQ(toHex(m.key, 32), vectors::MASTER_HEX);
}

TEST(LiveSyncCrypto, DecryptsChunkAndObfuscatedPath) {
  Master m;
  std::string plain;
  ASSERT_EQ(livesync::decrypt(m.key, vectors::CHUNK_DATA, plain), livesync::Result::Ok);
  EXPECT_EQ(plain, vectors::CHUNK_PLAIN);
  std::string json;
  ASSERT_EQ(livesync::decryptMetaPath(m.key, vectors::META_FIELD, json), livesync::Result::Ok);
  EXPECT_EQ(json, vectors::META_JSON);
}

TEST(LiveSyncCrypto, RefusesTamperingAndLegacyFormats) {
  Master m;
  std::string plain;
  std::string bad = vectors::CHUNK_DATA;
  bad[bad.size() - 6] = bad[bad.size() - 6] == 'A' ? 'B' : 'A';  // flips tag/ciphertext bits
  EXPECT_EQ(livesync::decrypt(m.key, bad, plain), livesync::Result::WrongKey);
  EXPECT_TRUE(plain.empty());
  EXPECT_EQ(livesync::decrypt(m.key, "%~abc", plain), livesync::Result::Unsupported);
  EXPECT_EQ(livesync::decrypt(m.key, "%abc", plain), livesync::Result::Unsupported);
  EXPECT_EQ(livesync::decrypt(m.key, "[1,2]", plain), livesync::Result::Unsupported);
  EXPECT_EQ(livesync::decrypt(m.key, "%=AAAA", plain), livesync::Result::BadFormat);
  EXPECT_EQ(livesync::decrypt(m.key, "plain text", plain), livesync::Result::BadFormat);
  EXPECT_EQ(livesync::decryptMetaPath(m.key, vectors::CHUNK_DATA, plain), livesync::Result::BadFormat);
}

TEST(LiveSyncCrypto, DecodesSetupUri) {
  std::string json;
  // The full URI (URL-encoded, with the generator's trailing space) and the bare blob.
  ASSERT_EQ(livesync::decodeSetupUri(std::string(vectors::SETUP_URI) + " ", vectors::URI_PASSPHRASE, -1, json),
            livesync::Result::Ok);
  EXPECT_EQ(json, vectors::SETTINGS_JSON);
  ASSERT_EQ(livesync::decodeSetupUri(vectors::SETUP_BLOB, vectors::URI_PASSPHRASE, 2900, json), livesync::Result::Ok);
  EXPECT_EQ(json, vectors::SETTINGS_JSON);
  EXPECT_EQ(livesync::decodeSetupUri(vectors::SETUP_URI, "wrong", -1, json), livesync::Result::WrongKey);
}
