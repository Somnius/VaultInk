#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The device's own Ed25519 key for SSH sign-in: the shapes of the key in its different places.
// The secret half never leaves the device; on the SD card it is sealed with a key made from the
// device secret (AES-256-GCM, done by the caller: this file has no crypto and no platform code).
namespace sshkey {

inline constexpr size_t SEED_BYTES = 32;    // the private key
inline constexpr size_t PUBLIC_BYTES = 32;  // the public key
inline constexpr char KEY_TYPE[] = "ssh-ed25519";

// --- the key file: /.vaultink/ssh/id_ed25519.key ---
//   magic "VISK" (4) | version (1) | iv (12) | sealed seed and public key (64) | GCM tag (16)
// The first 5 bytes are the additional data of the seal, so a file with a changed header fails.
inline constexpr uint8_t FILE_MAGIC[4] = {'V', 'I', 'S', 'K'};
inline constexpr uint8_t FILE_VERSION = 1;
inline constexpr size_t FILE_HEADER_BYTES = 5;
inline constexpr size_t FILE_IV_BYTES = 12;
inline constexpr size_t FILE_SEALED_BYTES = SEED_BYTES + PUBLIC_BYTES;
inline constexpr size_t FILE_TAG_BYTES = 16;
inline constexpr size_t FILE_BYTES = FILE_HEADER_BYTES + FILE_IV_BYTES + FILE_SEALED_BYTES + FILE_TAG_BYTES;

struct FileParts {
  const uint8_t* header = nullptr;  // FILE_HEADER_BYTES
  const uint8_t* iv = nullptr;      // FILE_IV_BYTES
  const uint8_t* sealed = nullptr;  // FILE_SEALED_BYTES
  const uint8_t* tag = nullptr;     // FILE_TAG_BYTES
};

// Writes the header into `file` (FILE_BYTES long) and says where the iv, the sealed bytes and the
// tag go, for the caller to fill.
void layoutFile(uint8_t* file, uint8_t*& iv, uint8_t*& sealed, uint8_t*& tag);
// False unless the length, the magic and the version are exactly right.
bool parseFile(const uint8_t* file, size_t len, FileParts& out);

// --- the public key ---
// The SSH wire form: string "ssh-ed25519", string key (51 bytes). What a server sees and what the
// fingerprint is taken over.
std::vector<uint8_t> publicBlob(const uint8_t publicKey[PUBLIC_BYTES]);
// "ssh-ed25519 AAAA... comment": the line for ~/.ssh/authorized_keys on the server. Characters of
// the comment that are not plain (space to ~, no quotes or backslash) are left out.
std::string authorizedKeysLine(const uint8_t publicKey[PUBLIC_BYTES], std::string_view comment);

// --- the private key for the SSH library ---
// PKCS#8 DER of an Ed25519 private key (RFC 8410, 48 bytes), the form wolfSSH takes. Wipe after use.
inline constexpr size_t PRIVATE_DER_BYTES = 48;
void privateDer(const uint8_t seed[SEED_BYTES], uint8_t out[PRIVATE_DER_BYTES]);

// Overwrites a buffer with zeros in a way the compiler does not remove.
void wipe(void* data, size_t len);

}  // namespace sshkey
