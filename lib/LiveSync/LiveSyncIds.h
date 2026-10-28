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

// Building blocks, public for tests.
uint64_t xxh64(const void* data, size_t len, uint64_t seed = 0);
std::string base36(uint64_t value);
size_t utf16Length(std::string_view utf8);

}  // namespace livesync
