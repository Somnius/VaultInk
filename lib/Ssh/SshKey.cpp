#include "SshKey.h"

#include <cstring>

#include "SshKnownHosts.h"

namespace sshkey {

void layoutFile(uint8_t* file, uint8_t*& iv, uint8_t*& sealed, uint8_t*& tag) {
  memcpy(file, FILE_MAGIC, sizeof(FILE_MAGIC));
  file[4] = FILE_VERSION;
  iv = file + FILE_HEADER_BYTES;
  sealed = iv + FILE_IV_BYTES;
  tag = sealed + FILE_SEALED_BYTES;
}

bool parseFile(const uint8_t* file, const size_t len, FileParts& out) {
  if (!file || len != FILE_BYTES) return false;
  if (memcmp(file, FILE_MAGIC, sizeof(FILE_MAGIC)) != 0 || file[4] != FILE_VERSION) return false;
  out.header = file;
  out.iv = file + FILE_HEADER_BYTES;
  out.sealed = out.iv + FILE_IV_BYTES;
  out.tag = out.sealed + FILE_SEALED_BYTES;
  return true;
}

std::vector<uint8_t> publicBlob(const uint8_t publicKey[PUBLIC_BYTES]) {
  const size_t typeLen = sizeof(KEY_TYPE) - 1;
  std::vector<uint8_t> blob;
  blob.reserve(4 + typeLen + 4 + PUBLIC_BYTES);
  blob.insert(blob.end(), {0, 0, 0, static_cast<uint8_t>(typeLen)});
  blob.insert(blob.end(), KEY_TYPE, KEY_TYPE + typeLen);
  blob.insert(blob.end(), {0, 0, 0, static_cast<uint8_t>(PUBLIC_BYTES)});
  blob.insert(blob.end(), publicKey, publicKey + PUBLIC_BYTES);
  return blob;
}

std::string authorizedKeysLine(const uint8_t publicKey[PUBLIC_BYTES], const std::string_view comment) {
  const std::vector<uint8_t> blob = publicBlob(publicKey);
  std::string line = std::string(KEY_TYPE) + " " + sshkh::base64Encode(blob.data(), blob.size());
  std::string clean;
  for (const char c : comment) {
    if (c > ' ' && c <= '~' && c != '"' && c != '\'' && c != '\\') clean += c;
  }
  if (!clean.empty()) line += " " + clean;
  return line;
}

void privateDer(const uint8_t seed[SEED_BYTES], uint8_t out[PRIVATE_DER_BYTES]) {
  // SEQUENCE { INTEGER 0, SEQUENCE { OID 1.3.101.112 }, OCTET STRING { OCTET STRING seed } }
  static constexpr uint8_t PREFIX[16] = {0x30, 0x2E, 0x02, 0x01, 0x00, 0x30, 0x05, 0x06,
                                         0x03, 0x2B, 0x65, 0x70, 0x04, 0x22, 0x04, 0x20};
  memcpy(out, PREFIX, sizeof(PREFIX));
  memcpy(out + sizeof(PREFIX), seed, SEED_BYTES);
}

void wipe(void* data, const size_t len) {
  volatile uint8_t* p = static_cast<volatile uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) p[i] = 0;
}

void wipeConst(const std::string& secret) {
  if (!secret.empty()) wipe(const_cast<char*>(secret.data()), secret.size());
}

}  // namespace sshkey
