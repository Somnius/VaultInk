#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Document ids for writing to Self-hosted LiveSync (Phase 4c), ported from the plugin so the
// plugin finds what the device writes (see .work/livesync-write-spec.md for the source pins).
//   chunk id = "h:+" + base36(xxh64("<piece>-<hashedPassphrase>-<piece length in UTF-16 units>"))
//   note id  = "f:" + hex(SHA-256(hex(SHA-256(passphrase)) + ":" + lowercase(path)))   (legacy ids)
// Both depend on the passphrase, which the device never stores: it keeps hashedPassphrase() and
// the hex SHA-256 of the passphrase instead (made once at setup).
namespace livesync {

// fallbackMixedHashEach(SALT_OF_ID + the first 3/4 of the passphrase): the per-vault part of
// every chunk id.
std::string hashedPassphrase(std::string_view passphrase);

// Lowercase hex SHA-256 of the passphrase, the per-vault part of every note id.
std::string passphraseDigest(std::string_view passphrase);

std::string chunkId(std::string_view piece, std::string_view hashedPass);

// The note id for a vault-relative path ("Folder/Note.md"); "" when the path has letters the
// device can't lowercase like JavaScript does (then a new note can't be pushed).
std::string noteId(std::string_view passphraseDigestHex, std::string_view path);

// Id scheme 1 (idDerivationVersion 1 in the plugin): HMAC-SHA256 with a 64-hex key kept in the
// vault's settings (the plugin's idDerivation.ts). The device never derives the key (PBKDF2 of a
// typed source), it takes it from the Setup URI.
//   chunk id = "h:+" + hex(HMAC(HMAC(key, "self-hosted-livesync:id-v1:chunk-key:xxhash64"),
//                              "self-hosted-livesync:id-v1:chunk:xxhash64\0" + hex16(xxh64(piece))))
//   note id  = "f:" + hex(HMAC(key, "self-hosted-livesync:id-v1:document\0" + lowercase(path)))
bool isIdKeyHex(std::string_view keyHex);  // exactly 64 lowercase hex digits
std::string chunkIdV1(std::string_view piece, std::string_view keyHex);
// "" when the key is bad or the path has letters the device can't lowercase like JavaScript.
// `nfc` = pathNormalizationVersion 1: the path is composed to NFC before the id is made.
std::string noteIdV1(std::string_view keyHex, std::string_view path, bool nfc);

// Building blocks, public for tests.
uint64_t xxh64(const void* data, size_t len, uint64_t seed = 0);
std::string base36(uint64_t value);
size_t utf16Length(std::string_view utf8);

}  // namespace livesync
