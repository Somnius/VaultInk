#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "SshKey.h"
#include "SshKnownHosts.h"

using sshkh::Verdict;

namespace {
std::vector<uint8_t> keyBlob(const uint8_t fill) {
  uint8_t pub[32];
  for (size_t i = 0; i < sizeof(pub); ++i) pub[i] = static_cast<uint8_t>(fill + i);
  return sshkey::publicBlob(pub);
}

// An ECDSA-shaped blob: another key type for the same host.
std::vector<uint8_t> otherTypeBlob() {
  const std::string type = "ecdsa-sha2-nistp256";
  std::vector<uint8_t> blob = {0, 0, 0, static_cast<uint8_t>(type.size())};
  blob.insert(blob.end(), type.begin(), type.end());
  blob.insert(blob.end(), {0, 0, 0, 4, 1, 2, 3, 4});
  return blob;
}

Verdict check(const std::string& text, const char* host, const uint16_t port, const std::vector<uint8_t>& blob) {
  return sshkh::check(text, host, port, blob.data(), blob.size());
}
}  // namespace

TEST(SshKnownHosts, Base64RoundTripAndKnownVectors) {
  const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
  const char* encoded[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
  for (size_t i = 0; i < 7; ++i) {
    const std::string in = plain[i];
    EXPECT_EQ(sshkh::base64Encode(reinterpret_cast<const uint8_t*>(in.data()), in.size()), encoded[i]);
    std::vector<uint8_t> out;
    ASSERT_TRUE(sshkh::base64Decode(encoded[i], out)) << encoded[i];
    EXPECT_EQ(std::string(out.begin(), out.end()), in);
  }
  const uint8_t two[] = {'f', 'o'};
  EXPECT_EQ(sshkh::base64Encode(two, 2, false), "Zm8");
}

TEST(SshKnownHosts, Base64DecodeIsStrict) {
  std::vector<uint8_t> out;
  EXPECT_FALSE(sshkh::base64Decode("Zm9v!", out));
  EXPECT_FALSE(sshkh::base64Decode("Zm9", out));  // padding missing
  EXPECT_FALSE(sshkh::base64Decode("Z", out));
  EXPECT_FALSE(sshkh::base64Decode("Zm9v Zm9v", out));  // no spaces
  EXPECT_FALSE(sshkh::base64Decode("Zh==", out));       // left-over bits are not zero
  EXPECT_FALSE(sshkh::base64Decode("Zg===", out));
}

TEST(SshKnownHosts, HostFieldFollowsOpenSsh) {
  EXPECT_EQ(sshkh::hostField("Example.ORG", 22), "example.org");
  EXPECT_EQ(sshkh::hostField("example.org", 2222), "[example.org]:2222");
  EXPECT_EQ(sshkh::hostField("192.168.1.10", 22), "192.168.1.10");
}

TEST(SshKnownHosts, FirstUseThenMatchThenMismatch) {
  std::string text;
  const auto key = keyBlob(1);
  const auto changed = keyBlob(2);
  EXPECT_EQ(check(text, "nas.local", 22, key), Verdict::Unknown);
  ASSERT_TRUE(sshkh::add(text, "nas.local", 22, key.data(), key.size()));
  EXPECT_EQ(check(text, "nas.local", 22, key), Verdict::Match);
  EXPECT_EQ(check(text, "NAS.Local", 22, key), Verdict::Match);
  // The hard stop: the same host, another key.
  EXPECT_EQ(check(text, "nas.local", 22, changed), Verdict::Mismatch);
  // Another host or another port is its own server.
  EXPECT_EQ(check(text, "other.local", 22, key), Verdict::Unknown);
  EXPECT_EQ(check(text, "nas.local", 2222, key), Verdict::Unknown);
}

TEST(SshKnownHosts, AnotherKeyTypeForAPinnedHostIsAMismatch) {
  std::string text;
  const auto key = keyBlob(1);
  ASSERT_TRUE(sshkh::add(text, "nas.local", 22, key.data(), key.size()));
  EXPECT_EQ(check(text, "nas.local", 22, otherTypeBlob()), Verdict::Mismatch);
}

TEST(SshKnownHosts, ABlobThatIsNotAKeyNeverMatches) {
  std::string text;
  const auto key = keyBlob(1);
  ASSERT_TRUE(sshkh::add(text, "nas.local", 22, key.data(), key.size()));
  const std::vector<uint8_t> junk = {1, 2, 3};
  EXPECT_EQ(check(text, "nas.local", 22, junk), Verdict::Mismatch);
  EXPECT_EQ(check(text, "new.local", 22, junk), Verdict::Unknown);
  EXPECT_FALSE(sshkh::add(text, "new.local", 22, junk.data(), junk.size()));
  // A length that runs past the end, and a key type with nothing after it.
  const std::vector<uint8_t> longLen = {0, 0, 0, 200, 's', 's', 'h'};
  EXPECT_EQ(sshkh::keyType(longLen.data(), longLen.size()), "");
  const std::vector<uint8_t> typeOnly = {0, 0, 0, 3, 's', 's', 'h'};
  EXPECT_EQ(sshkh::keyType(typeOnly.data(), typeOnly.size()), "");
  EXPECT_EQ(sshkh::keyType(key.data(), key.size()), "ssh-ed25519");
}

TEST(SshKnownHosts, FileLinesAreOpenSshFormat) {
  std::string text;
  const auto key = keyBlob(1);
  ASSERT_TRUE(sshkh::add(text, "nas.local", 2222, key.data(), key.size()));
  EXPECT_EQ(text, "[nas.local]:2222 ssh-ed25519 " + sshkh::base64Encode(key.data(), key.size()) + "\n");
}

TEST(SshKnownHosts, ParsesAFileWrittenByHand) {
  const auto key = keyBlob(7);
  const std::string b64 = sshkh::base64Encode(key.data(), key.size());
  const std::string text = "# my servers\r\n\r\n   \n@revoked nas.local ssh-ed25519 " + b64 +
                           "\n|1|hashed|entry ssh-ed25519 " + b64 + "\nbroken line\n  NAS.local\tssh-ed25519   " + b64 +
                           "  a comment\r\nnoeol.local ssh-ed25519 " + b64;
  EXPECT_EQ(check(text, "nas.local", 22, key), Verdict::Match);
  EXPECT_EQ(check(text, "noeol.local", 22, key), Verdict::Match);
  // Comment, marker, hashed and broken lines pin nothing.
  EXPECT_EQ(check(text, "broken", 22, key), Verdict::Unknown);
  EXPECT_EQ(check(text, "hashed", 22, key), Verdict::Unknown);
}

TEST(SshKnownHosts, AddAppendsAfterAFileWithoutFinalNewline) {
  const auto key = keyBlob(1);
  const auto second = keyBlob(9);
  std::string text = "a.local ssh-ed25519 " + sshkh::base64Encode(key.data(), key.size());
  ASSERT_TRUE(sshkh::add(text, "b.local", 22, second.data(), second.size()));
  EXPECT_EQ(check(text, "a.local", 22, key), Verdict::Match);
  EXPECT_EQ(check(text, "b.local", 22, second), Verdict::Match);
}

TEST(SshKnownHosts, RefusesHostsThatCouldForgeALine) {
  std::string text;
  const auto key = keyBlob(1);
  EXPECT_FALSE(sshkh::add(text, "evil.local ssh-ed25519 AAAA\nnas.local", 22, key.data(), key.size()));
  EXPECT_FALSE(sshkh::add(text, "host name", 22, key.data(), key.size()));
  EXPECT_FALSE(sshkh::add(text, "", 22, key.data(), key.size()));
  EXPECT_TRUE(text.empty());
}

TEST(SshKnownHosts, FileIsBounded) {
  std::string text;
  const auto key = keyBlob(1);
  for (size_t i = 0; i < sshkh::MAX_ENTRIES; ++i) {
    ASSERT_TRUE(sshkh::add(text, "host" + std::to_string(i) + ".local", 22, key.data(), key.size()));
  }
  EXPECT_FALSE(sshkh::add(text, "onemore.local", 22, key.data(), key.size()));
  EXPECT_LT(text.size(), sshkh::MAX_TEXT_BYTES);
}

TEST(SshKnownHosts, RemoveForgetsOnlyThatHostAndPort) {
  std::string text = "# kept\n";
  const auto key = keyBlob(1);
  ASSERT_TRUE(sshkh::add(text, "nas.local", 22, key.data(), key.size()));
  ASSERT_TRUE(sshkh::add(text, "nas.local", 2222, key.data(), key.size()));
  ASSERT_TRUE(sshkh::add(text, "other.local", 22, key.data(), key.size()));
  EXPECT_EQ(sshkh::remove(text, "NAS.local", 22), 1u);
  EXPECT_EQ(check(text, "nas.local", 22, keyBlob(2)), Verdict::Unknown);  // may be pinned again
  EXPECT_EQ(check(text, "nas.local", 2222, key), Verdict::Match);
  EXPECT_EQ(check(text, "other.local", 22, key), Verdict::Match);
  EXPECT_EQ(text.rfind("# kept\n", 0), 0u);
  EXPECT_EQ(sshkh::remove(text, "nas.local", 22), 0u);
}

TEST(SshKnownHosts, FingerprintTextMatchesSshKeygen) {
  // SHA-256 of the empty input; `printf '' | sha256sum | xxd -r -p | base64` gives the text.
  const uint8_t digest[32] = {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4,
                              0xc8, 0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b,
                              0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
  EXPECT_EQ(sshkh::fingerprint(digest), "SHA256:47DEQpj8HBSa+/TImW+5JCeuQeRkm5NMpJWZG3hSuFU");
}
