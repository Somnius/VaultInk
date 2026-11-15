#include <gtest/gtest.h>

#include <string>

#include "LiveSyncCrypto.h"
#include "python_uri_fixture.h"
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

// A Setup URI written by scripts/livesync_setup_uri.py (which re-encrypts a URI with a new
// passphrase) opens with the code the device runs.
TEST(LiveSyncCrypto, DecodesASetupUriMadeByThePythonScript) {
  std::string json;
  ASSERT_EQ(livesync::decodeSetupUri(pyfixture::SETUP_URI, pyfixture::PASSPHRASE, -1, json), livesync::Result::Ok);
  EXPECT_EQ(json, pyfixture::SETTINGS_JSON);
  // With the clock set the device tries the time-bound form first; the plain one still opens.
  ASSERT_EQ(livesync::decodeSetupUri(pyfixture::SETUP_URI, pyfixture::PASSPHRASE, 2900, json), livesync::Result::Ok);
  EXPECT_EQ(json, pyfixture::SETTINGS_JSON);
  EXPECT_EQ(livesync::decodeSetupUri(pyfixture::SETUP_URI, "dummy-old-passphrase", -1, json),
            livesync::Result::WrongKey);
}

// A chunk too big to copy is decrypted and decoded inside its own buffer: the same bytes as the
// copying way, and a damaged one is refused.
TEST(LiveSyncCrypto, DecryptsAndDecodesInPlace) {
  livesync::Key master;
  for (size_t i = 0; i < sizeof(master); ++i) master[i] = static_cast<uint8_t>(i * 7 + 1);
  uint32_t seed = 3;
  const livesync::RandomFill random = [&seed](uint8_t* out, size_t len) {
    for (size_t i = 0; i < len; ++i) out[i] = static_cast<uint8_t>((seed = seed * 1664525u + 1013904223u) >> 24);
  };
  for (const size_t size : {0u, 1u, 2u, 3u, 15u, 16u, 17u, 4096u, 300001u}) {
    std::string bytes(size, '\0');
    random(reinterpret_cast<uint8_t*>(bytes.data()), bytes.size());
    const std::string text = livesync::base64Encode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    std::string stored;
    ASSERT_TRUE(livesync::encrypt(master, text, random, stored));
    std::string copied;
    ASSERT_EQ(livesync::decrypt(master, stored, copied), livesync::Result::Ok);
    std::string buffer = stored;
    size_t len = 0;
    ASSERT_EQ(livesync::decryptInPlace(master, buffer.data(), buffer.size(), len), livesync::Result::Ok) << size;
    EXPECT_EQ(std::string(buffer.data(), len), copied) << size;
    size_t decoded = 0;
    ASSERT_TRUE(livesync::base64DecodeInPlace(buffer.data(), len, decoded));
    EXPECT_EQ(std::string(buffer.data(), decoded), bytes) << size;

    buffer = stored;
    buffer[buffer.size() / 2] = buffer[buffer.size() / 2] == 'A' ? 'B' : 'A';
    EXPECT_EQ(livesync::decryptInPlace(master, buffer.data(), buffer.size(), len), livesync::Result::WrongKey);
  }
  std::string legacy = "%~abc", none = "abc", bad = "%=a!bc";
  size_t len = 0;
  EXPECT_EQ(livesync::decryptInPlace(master, legacy.data(), legacy.size(), len), livesync::Result::Unsupported);
  EXPECT_EQ(livesync::decryptInPlace(master, none.data(), none.size(), len), livesync::Result::BadFormat);
  EXPECT_EQ(livesync::decryptInPlace(master, bad.data(), bad.size(), len), livesync::Result::BadFormat);
}

TEST(LiveSyncCrypto, Base64RoundTrip) {
  for (const std::string s : {"", "f", "fo", "foo", "foob", "fooba", "foobar", "\xce\xb1\x00\xff"}) {
    const std::string enc = livesync::base64Encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    std::string dec;
    ASSERT_TRUE(livesync::base64Decode(enc, dec));
    EXPECT_EQ(dec, s);
  }
  EXPECT_EQ(livesync::base64Encode(reinterpret_cast<const uint8_t*>("foobar"), 6), "Zm9vYmFy");
  EXPECT_EQ(livesync::base64Encode(reinterpret_cast<const uint8_t*>("fo"), 2), "Zm8=");
}
