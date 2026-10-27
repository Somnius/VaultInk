#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Decryption for Obsidian Self-hosted LiveSync, read side only (pinned from octagonal-wheels
// hkdf.ts and livesync-commonlib, see .work/livesync-spec.md). E2EE V2:
//   master = PBKDF2-SHA256(passphrase, salt, 310000 iterations, 32 bytes)
//   key    = HKDF-SHA256(master, hkdfSalt, info = "")          (per message)
//   "%=" + base64(iv[12] | hkdfSalt[32] | AES-256-GCM ciphertext | tag[16])
// "%$" strings (Setup URI) embed their PBKDF2 salt first: base64(pbkdf2Salt[32] | iv | hkdfSalt | ...).
namespace livesync {

constexpr int PBKDF2_ITERATIONS = 310000;
using Key = uint8_t[32];

enum class Result : uint8_t {
  Ok,
  Unsupported,  // a V1/V3 legacy format ("%~", "%", "[") VaultInk does not read
  BadFormat,    // not base64, too short
  WrongKey,     // the GCM tag did not match: wrong passphrase or damaged data
};

// The E2EE master key from the passphrase and the database's pbkdf2salt (32 bytes). `yield`
// (optional) is called every 1000 rounds with the rounds done: the 310,000 rounds take seconds on
// the device, so the caller can feed the watchdog and show progress.
bool deriveMaster(std::string_view passphrase, const uint8_t* salt, size_t saltLen, Key out,
                  void (*yield)(int roundsDone, void* ctx) = nullptr, void* ctx = nullptr);

// Called every 1000 rounds by every key derivation that has no `yield` of its own (Setup URI
// decoding derives one or two keys); null to stop.
void setDefaultYield(void (*yield)(int roundsDone, void* ctx), void* ctx);

// A "%=" string (chunk data, metadata) with the master key.
Result decrypt(const Key master, std::string_view encrypted, std::string& plain);

// A "%$" string with a passphrase (its PBKDF2 salt is inside).
Result decryptWithPassphrase(std::string_view passphrase, std::string_view encrypted, std::string& plain);

// An obfuscated metadata path field ("/\:" + "%=..."): the JSON with path, mtime, ctime, size, children.
Result decryptMetaPath(const Key master, std::string_view field, std::string& json);

// "obsidian://setuplivesync?settings=..." (or just the blob): the settings JSON. Tries the
// passphrase as typed and the time-bound (ephemeral) form for `weekWindow`
// (floor(unix_ms / 604800000)); pass weekWindow < 0 to skip the ephemeral form.
Result decodeSetupUri(std::string_view uri, std::string_view passphrase, int64_t weekWindow, std::string& json);

// Bytes to standard base64 with padding.
std::string base64Encode(const uint8_t* data, size_t len);

// Standard base64 (with padding) to bytes; false on bad input.
bool base64Decode(std::string_view in, std::string& out);

}  // namespace livesync
