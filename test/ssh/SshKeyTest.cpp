#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "SshKey.h"
#include "SshKnownHosts.h"
#include "network/ProtectedPaths.h"
#include "util/DevLinkPaths.h"

namespace {
// The public key of RFC 8032 test 1 (its seed is 9d61b19d...7f60).
const uint8_t kPublic[32] = {0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
                             0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
                             0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
const uint8_t kSeed[32] = {0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a,
                           0xf4, 0x92, 0xec, 0x2c, 0xc4, 0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32,
                           0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60};
}  // namespace

TEST(SshKey, FileLayoutRoundTrip) {
  uint8_t file[sshkey::FILE_BYTES] = {};
  uint8_t* iv = nullptr;
  uint8_t* sealed = nullptr;
  uint8_t* tag = nullptr;
  sshkey::layoutFile(file, iv, sealed, tag);
  EXPECT_EQ(sshkey::FILE_BYTES, 97u);
  EXPECT_EQ(memcmp(file, "VISK\x01", 5), 0);
  memset(iv, 0x11, sshkey::FILE_IV_BYTES);
  memset(sealed, 0x22, sshkey::FILE_SEALED_BYTES);
  memset(tag, 0x33, sshkey::FILE_TAG_BYTES);

  sshkey::FileParts parts;
  ASSERT_TRUE(sshkey::parseFile(file, sizeof(file), parts));
  EXPECT_EQ(parts.header, file);
  EXPECT_EQ(parts.iv, file + 5);
  EXPECT_EQ(parts.sealed, file + 17);
  EXPECT_EQ(parts.tag, file + 81);
  EXPECT_EQ(parts.iv[0], 0x11);
  EXPECT_EQ(parts.sealed[63], 0x22);
  EXPECT_EQ(parts.tag[15], 0x33);
  EXPECT_EQ(file[96], 0x33);  // the tag ends the file
}

TEST(SshKey, ParseRefusesAnythingElse) {
  uint8_t file[sshkey::FILE_BYTES + 1] = {};
  uint8_t* iv = nullptr;
  uint8_t* sealed = nullptr;
  uint8_t* tag = nullptr;
  sshkey::layoutFile(file, iv, sealed, tag);
  sshkey::FileParts parts;
  EXPECT_FALSE(sshkey::parseFile(nullptr, sshkey::FILE_BYTES, parts));
  EXPECT_FALSE(sshkey::parseFile(file, sshkey::FILE_BYTES - 1, parts));  // cut short
  EXPECT_FALSE(sshkey::parseFile(file, sshkey::FILE_BYTES + 1, parts));  // something appended
  EXPECT_FALSE(sshkey::parseFile(file, 0, parts));
  file[0] = 'X';
  EXPECT_FALSE(sshkey::parseFile(file, sshkey::FILE_BYTES, parts));  // another magic
  file[0] = 'V';
  file[4] = 2;
  EXPECT_FALSE(sshkey::parseFile(file, sshkey::FILE_BYTES, parts));  // a version this build does not know
}

TEST(SshKey, PublicBlobIsTheWireForm) {
  const std::vector<uint8_t> blob = sshkey::publicBlob(kPublic);
  ASSERT_EQ(blob.size(), 51u);
  EXPECT_EQ(memcmp(blob.data(), "\0\0\0\x0bssh-ed25519\0\0\0\x20", 19), 0);
  EXPECT_EQ(memcmp(blob.data() + 19, kPublic, 32), 0);
  EXPECT_EQ(sshkh::keyType(blob.data(), blob.size()), "ssh-ed25519");
}

TEST(SshKey, AuthorizedKeysLine) {
  // The base64 was checked with OpenSSH: `ssh-keygen -l -f` reads this line as an ED25519 key.
  const std::string line = sshkey::authorizedKeysLine(kPublic, "vaultink");
  EXPECT_EQ(line, "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAINdamAGCsQq31Uv+08lkBzoO4XLz2qYjJa8CGmj3B1Ea vaultink");
  EXPECT_EQ(sshkey::authorizedKeysLine(kPublic, ""),
            "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAINdamAGCsQq31Uv+08lkBzoO4XLz2qYjJa8CGmj3B1Ea");
  // A comment cannot add a second key or an option to the line.
  const std::string odd = sshkey::authorizedKeysLine(kPublic, "my reader\n\"x\" \\ \xCE\xB1");
  EXPECT_EQ(odd, "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAINdamAGCsQq31Uv+08lkBzoO4XLz2qYjJa8CGmj3B1Ea myreaderx");
}

TEST(SshKey, PrivateDerIsRfc8410) {
  uint8_t der[sshkey::PRIVATE_DER_BYTES];
  sshkey::privateDer(kSeed, der);
  const uint8_t prefix[16] = {0x30, 0x2e, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06,
                              0x03, 0x2b, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20};
  EXPECT_EQ(memcmp(der, prefix, 16), 0);
  EXPECT_EQ(memcmp(der + 16, kSeed, 32), 0);
  EXPECT_EQ(der[1], sizeof(der) - 2);  // the outer length covers the rest
}

TEST(SshKey, WipeClears) {
  uint8_t secret[32];
  memset(secret, 0xAA, sizeof(secret));
  sshkey::wipe(secret, sizeof(secret));
  for (const uint8_t b : secret) EXPECT_EQ(b, 0);
}

// The key file and the pinned host keys stay on the device: no web request, no plugin, no file
// from a server and no developer link command reaches /.vaultink/ssh/.
TEST(SshKey, KeyFolderIsProtected) {
  const char* paths[] = {
      "/.vaultink/ssh/id_ed25519.key",    "/.vaultink/ssh/known_hosts",       "/.vaultink/ssh",
      "/.VaultInk/SSH/id_ed25519.key",    "/.vaultink//ssh/./id_ed25519.key", "/books/../.vaultink/ssh/id_ed25519.key",
      "/.vaultink/ssh/id_ed25519.key.tmp"};
  for (const char* path : paths) {
    EXPECT_TRUE(protectedpaths::isSensitivePath(path)) << path;
    // isInDeviceFolder() looks at the first folder as written; the one path that starts elsewhere is
    // caught by isSensitivePath() above.
    if (strncmp(path, "/books", 6) != 0) {
      EXPECT_TRUE(protectedpaths::isInDeviceFolder(path)) << path;
    }
    EXPECT_FALSE(protectedpaths::isPluginPath(path)) << path;
  }
  using devlinkpaths::Access;
  for (const Access access : {Access::List, Access::Read, Access::Write, Access::Flash}) {
    EXPECT_FALSE(devlinkpaths::isAllowed("/.vaultink/ssh", access));
    EXPECT_FALSE(devlinkpaths::isAllowed("/.vaultink/ssh/id_ed25519.key", access));
    EXPECT_FALSE(devlinkpaths::isAllowed("/.vaultink/ssh/known_hosts", access));
    EXPECT_FALSE(devlinkpaths::isAllowed("/.vaultink/ssh/firmware.bin", access));
  }
  EXPECT_FALSE(protectedpaths::isSensitivePath("/.vaultink/bench.csv"));
  EXPECT_FALSE(protectedpaths::isSensitivePath("/books/ssh/id_ed25519.key"));
}
