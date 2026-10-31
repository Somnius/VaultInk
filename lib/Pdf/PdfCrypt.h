#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The few ciphers the PDF standard security handler needs (revisions 2 to 6): MD5 for the keys, RC4,
// AES-128 and AES-256 in CBC mode, and SHA-2 for the AES-256 handler. Small and self-contained so the PDF reader builds
// the same on the device and on the host; nothing here is for protecting anything, only for reading files that are
// encrypted with an empty user password (a permissions-only lock, as most shop-bought PDFs have).
namespace pdf::crypt {

void md5(const uint8_t* data, size_t len, uint8_t out[16]);
std::string md5(const std::string& data);  // 16 raw bytes

// RC4 in place.
void rc4(const uint8_t* key, size_t keyLen, uint8_t* data, size_t len);

// AES-128-CBC: `in` is a 16-byte IV followed by whole blocks; the result has the PKCS#5 padding removed.
// False when the length isn't right or the padding is wrong.
bool aes128CbcDecrypt(const uint8_t key[16], const uint8_t* in, size_t len, std::string& out);

// AES-256-CBC: `in` is a 16-byte IV followed by whole blocks; PKCS#5 padding removed. False on bad length/padding.
bool aes256CbcDecrypt(const uint8_t key[32], const uint8_t* in, size_t len, std::string& out);
// AES-256-CBC with an explicit IV, whole blocks, no padding handling (len must be a non-zero multiple of 16).
bool aes256CbcDecryptRaw(const uint8_t key[32], const uint8_t iv[16], const uint8_t* in, size_t len, std::string& out);
// AES-128-CBC encryption without padding (len is a multiple of 16; `out` has `len` bytes), for the R6 hash.
void aes128CbcEncryptRaw(const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, size_t len, uint8_t* out);

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void sha384(const uint8_t* data, size_t len, uint8_t out[48]);
void sha512(const uint8_t* data, size_t len, uint8_t out[64]);

}  // namespace pdf::crypt
