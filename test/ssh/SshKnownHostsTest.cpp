#include <gtest/gtest.h>

#include <cstring>
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

// ---- the MAC line, and what the file's state allows ----

namespace {
// A stand-in for the device's HMAC: keyed, and different for every text. `fail` models a device
// secret that cannot be read.
struct FakeMac {
  uint8_t key = 0x5A;
  bool fail = false;
  int calls = 0;
};
bool fakeCompute(void* ctx, const std::string& canonicalText, uint8_t out[32]) {
  auto* mac = static_cast<FakeMac*>(ctx);
  ++mac->calls;
  if (mac->fail) return false;
  uint32_t h = 2166136261u ^ mac->key;
  for (int i = 0; i < 32; ++i) {
    for (const char c : canonicalText) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    h = (h ^ static_cast<uint32_t>(i)) * 16777619u;
    out[i] = static_cast<uint8_t>(h >> 24);
  }
  return true;
}
sshkh::Mac macOf(FakeMac& mac) { return sshkh::Mac{fakeCompute, &mac}; }

std::string sealedFile(FakeMac& mac, const std::vector<std::string>& hosts) {
  std::string text;
  for (size_t i = 0; i < hosts.size(); ++i) {
    const auto key = keyBlob(static_cast<uint8_t>(10 + i));
    EXPECT_TRUE(sshkh::add(text, hosts[i], 22, key.data(), key.size()));
  }
  EXPECT_TRUE(sshkh::seal(text, macOf(mac)));
  return text;
}
}  // namespace

TEST(SshKnownHostsMac, AMissingFileIsAFirstUse) {
  FakeMac mac;
  EXPECT_EQ(sshkh::verify(false, false, "", macOf(mac)), sshkh::FileState::Missing);
  EXPECT_EQ(mac.calls, 0);
}

TEST(SshKnownHostsMac, ASealedFileIsGood) {
  FakeMac mac;
  const std::string text = sealedFile(mac, {"a.local", "b.local"});
  EXPECT_EQ(sshkh::verify(true, true, text, macOf(mac)), sshkh::FileState::Good);
  // The last line is the MAC line, and it pins nothing.
  const size_t at = text.rfind(sshkh::MAC_PREFIX);
  ASSERT_NE(at, std::string::npos);
  EXPECT_EQ(text.size() - at, strlen(sshkh::MAC_PREFIX) + 64 + 1);
  EXPECT_EQ(check(text, "a.local", 22, keyBlob(10)), Verdict::Match);
  EXPECT_EQ(check(text, "#", 22, keyBlob(10)), Verdict::Unknown);
}

TEST(SshKnownHostsMac, AFileThatCouldNotBeReadIsNeverAFirstUse) {
  FakeMac mac;
  const std::string text = sealedFile(mac, {"a.local"});
  // Present but the read failed: not Missing, whatever was read.
  EXPECT_EQ(sshkh::verify(true, false, "", macOf(mac)), sshkh::FileState::Unreadable);
  EXPECT_EQ(sshkh::verify(true, false, text, macOf(mac)), sshkh::FileState::Unreadable);
  // Too big.
  EXPECT_EQ(sshkh::verify(true, true, std::string(sshkh::MAX_FILE_BYTES + 1, '#'), macOf(mac)),
            sshkh::FileState::Unreadable);
  // Not text: a NUL or another control byte.
  std::string binary = text;
  binary[3] = '\0';
  EXPECT_EQ(sshkh::verify(true, true, binary, macOf(mac)), sshkh::FileState::Unreadable);
  binary = text;
  binary.insert(0, "\x01");
  EXPECT_EQ(sshkh::verify(true, true, binary, macOf(mac)), sshkh::FileState::Unreadable);
  // The device secret cannot be read: the MAC cannot be checked, so the file is not trusted.
  FakeMac broken;
  broken.fail = true;
  EXPECT_EQ(sshkh::verify(true, true, text, macOf(broken)), sshkh::FileState::Unreadable);
}

TEST(SshKnownHostsMac, ACardOnlyEditIsDetected) {
  FakeMac mac;
  const std::string text = sealedFile(mac, {"a.local", "b.local"});
  const std::string macLine = text.substr(text.rfind(sshkh::MAC_PREFIX));
  const std::string noMac = text.substr(0, text.rfind(sshkh::MAC_PREFIX));

  // A pin taken out (to get a fresh "trust this key?" question).
  std::string removed = text;
  ASSERT_EQ(sshkh::remove(removed, "a.local", 22), 1u);
  EXPECT_EQ(sshkh::verify(true, true, removed, macOf(mac)), sshkh::FileState::Changed);

  // A pin put in (an attacker's key accepted without a question).
  std::string added = noMac;
  const auto evil = keyBlob(99);
  ASSERT_TRUE(sshkh::add(added, "c.local", 22, evil.data(), evil.size()));
  EXPECT_EQ(sshkh::verify(true, true, added + macLine, macOf(mac)), sshkh::FileState::Changed);

  // A key swapped for another one.
  std::string swapped;
  const auto other = keyBlob(77);
  ASSERT_TRUE(sshkh::add(swapped, "a.local", 22, other.data(), other.size()));
  const auto second = keyBlob(11);
  ASSERT_TRUE(sshkh::add(swapped, "b.local", 22, second.data(), second.size()));
  EXPECT_EQ(sshkh::verify(true, true, swapped + macLine, macOf(mac)), sshkh::FileState::Changed);

  // The two pins in the other order.
  std::string reordered;
  ASSERT_TRUE(sshkh::add(reordered, "b.local", 22, second.data(), second.size()));
  const auto first = keyBlob(10);
  ASSERT_TRUE(sshkh::add(reordered, "a.local", 22, first.data(), first.size()));
  EXPECT_EQ(sshkh::verify(true, true, reordered + macLine, macOf(mac)), sshkh::FileState::Changed);

  // The MAC line gone, the file emptied, the MAC cut short, doubled, or not the last line.
  EXPECT_EQ(sshkh::verify(true, true, noMac, macOf(mac)), sshkh::FileState::Changed);
  EXPECT_EQ(sshkh::verify(true, true, "", macOf(mac)), sshkh::FileState::Changed);
  EXPECT_EQ(sshkh::verify(true, true, noMac + macLine.substr(0, macLine.size() - 3) + "\n", macOf(mac)),
            sshkh::FileState::Changed);
  EXPECT_EQ(sshkh::verify(true, true, text + macLine, macOf(mac)), sshkh::FileState::Changed);
  EXPECT_EQ(sshkh::verify(true, true, macLine + noMac, macOf(mac)), sshkh::FileState::Changed);

  // A file sealed on another device (another key).
  FakeMac otherDevice;
  otherDevice.key = 0x11;
  EXPECT_EQ(sshkh::verify(true, true, text, macOf(otherDevice)), sshkh::FileState::Changed);
}

TEST(SshKnownHostsMac, AFileWrittenBeforeTheMacExistedIsChanged) {
  FakeMac mac;
  std::string old;
  const auto key = keyBlob(1);
  ASSERT_TRUE(sshkh::add(old, "nas.local", 22, key.data(), key.size()));
  EXPECT_EQ(sshkh::verify(true, true, old, macOf(mac)), sshkh::FileState::Changed);
}

TEST(SshKnownHostsMac, CommentsAndSpacingAreNotPartOfTheMac) {
  FakeMac mac;
  const std::string text = sealedFile(mac, {"a.local"});
  const std::string body = text.substr(0, text.rfind(sshkh::MAC_PREFIX));
  const std::string macLine = text.substr(text.rfind(sshkh::MAC_PREFIX));
  EXPECT_EQ(sshkh::verify(true, true, "# a note\r\n\r\n" + body + "\n" + macLine + "\n\n", macOf(mac)),
            sshkh::FileState::Good);
  EXPECT_EQ(sshkh::canonical("# x\n  A.Local\tssh-ed25519   QUJD  note\r\n@revoked b ssh-ed25519 QUJD\n"),
            "a.local ssh-ed25519 QUJD\n");
}

TEST(SshKnownHostsMac, SealRenewsTheOneMacLineAndKeepsEveryPin) {
  FakeMac mac;
  std::string text = sealedFile(mac, {"a.local", "b.local", "c.local"});
  const auto key = keyBlob(50);
  ASSERT_TRUE(sshkh::add(text, "d.local", 2222, key.data(), key.size()));
  EXPECT_EQ(sshkh::verify(true, true, text, macOf(mac)), sshkh::FileState::Changed);  // not sealed yet
  ASSERT_TRUE(sshkh::seal(text, macOf(mac)));
  EXPECT_EQ(sshkh::verify(true, true, text, macOf(mac)), sshkh::FileState::Good);
  size_t macLines = 0;
  for (size_t at = text.find(sshkh::MAC_PREFIX); at != std::string::npos; at = text.find(sshkh::MAC_PREFIX, at + 1)) {
    ++macLines;
  }
  EXPECT_EQ(macLines, 1u);
  // The older pins are all still there.
  EXPECT_EQ(check(text, "a.local", 22, keyBlob(10)), Verdict::Match);
  EXPECT_EQ(check(text, "b.local", 22, keyBlob(11)), Verdict::Match);
  EXPECT_EQ(check(text, "c.local", 22, keyBlob(12)), Verdict::Match);
  EXPECT_EQ(check(text, "d.local", 2222, key), Verdict::Match);
  // Without a MAC nothing is written.
  FakeMac broken;
  broken.fail = true;
  const std::string before = text;
  EXPECT_FALSE(sshkh::seal(text, macOf(broken)));
  EXPECT_EQ(text, before);
  EXPECT_FALSE(sshkh::seal(text, sshkh::Mac{}));
  EXPECT_EQ(text, before);
}

// ---- the host key rule of one session, as the library calls it ----

TEST(SshKeyGate, BeforeSignInOnlyThePinnedKeyPasses) {
  std::string text;
  const auto pinned = keyBlob(1);
  const auto other = keyBlob(2);
  ASSERT_TRUE(sshkh::add(text, "nas.local", 22, pinned.data(), pinned.size()));

  sshkh::KeyGate gate;
  gate.begin(text, "nas.local", 22);
  EXPECT_FALSE(gate.accept(other.data(), other.size()));
  EXPECT_EQ(gate.lastVerdict(), Verdict::Mismatch);
  EXPECT_FALSE(gate.signedIn());  // nothing passed: no session

  gate.begin(text, "new.local", 22);
  EXPECT_FALSE(gate.accept(pinned.data(), pinned.size()));
  EXPECT_EQ(gate.lastVerdict(), Verdict::Unknown);
  EXPECT_FALSE(gate.signedIn());

  gate.begin(text, "nas.local", 22);
  EXPECT_TRUE(gate.accept(pinned.data(), pinned.size()));
  EXPECT_EQ(gate.lastVerdict(), Verdict::Match);
  EXPECT_TRUE(gate.signedIn());
}

TEST(SshKeyGate, ARekeyWithTheSameKeyPassesEveryTime) {
  std::string text;
  const auto pinned = keyBlob(1);
  ASSERT_TRUE(sshkh::add(text, "nas.local", 22, pinned.data(), pinned.size()));
  sshkh::KeyGate gate;
  gate.begin(text, "nas.local", 22);
  ASSERT_TRUE(gate.accept(pinned.data(), pinned.size()));
  ASSERT_TRUE(gate.signedIn());
  // The sequence a long session sees: the callback again at every key exchange.
  for (int rekey = 0; rekey < 5; ++rekey) {
    EXPECT_TRUE(gate.accept(pinned.data(), pinned.size())) << rekey;
    EXPECT_EQ(gate.lastVerdict(), Verdict::Match);
  }
}

TEST(SshKeyGate, ARekeyWithAnotherKeyFailsClosed) {
  std::string text;
  const auto pinned = keyBlob(1);
  const auto other = keyBlob(2);
  ASSERT_TRUE(sshkh::add(text, "nas.local", 22, pinned.data(), pinned.size()));
  // A second key for the same host is pinned too (a file edited by hand): after the sign-in it is
  // still not accepted, only the key the session began with is.
  ASSERT_TRUE(sshkh::add(text, "nas.local", 22, other.data(), other.size()));
  sshkh::KeyGate gate;
  gate.begin(text, "nas.local", 22);
  ASSERT_TRUE(gate.accept(pinned.data(), pinned.size()));
  ASSERT_TRUE(gate.signedIn());
  EXPECT_TRUE(gate.accept(pinned.data(), pinned.size()));
  EXPECT_FALSE(gate.accept(other.data(), other.size()));
  EXPECT_EQ(gate.lastVerdict(), Verdict::Mismatch);
  // Shorter, longer, empty and absent keys.
  EXPECT_FALSE(gate.accept(pinned.data(), pinned.size() - 1));
  std::vector<uint8_t> longer = pinned;
  longer.push_back(0);
  EXPECT_FALSE(gate.accept(longer.data(), longer.size()));
  EXPECT_FALSE(gate.accept(pinned.data(), 0));
  EXPECT_FALSE(gate.accept(nullptr, 51));
  // And the right key is still the right key afterwards.
  EXPECT_TRUE(gate.accept(pinned.data(), pinned.size()));
}

TEST(SshKeyGate, AfterResetNothingPasses) {
  std::string text;
  const auto pinned = keyBlob(1);
  ASSERT_TRUE(sshkh::add(text, "nas.local", 22, pinned.data(), pinned.size()));
  sshkh::KeyGate gate;
  gate.begin(text, "nas.local", 22);
  ASSERT_TRUE(gate.accept(pinned.data(), pinned.size()));
  ASSERT_TRUE(gate.signedIn());
  gate.reset();
  EXPECT_FALSE(gate.accept(pinned.data(), pinned.size()));
  EXPECT_FALSE(gate.signedIn());
  EXPECT_FALSE(gate.accept(pinned.data(), pinned.size()));  // signed in with no key: nothing passes
}
