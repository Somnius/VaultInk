#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The pinned host keys of the SSH servers: trust on first use, and a changed key is refused. The
// text is OpenSSH's known_hosts format, one "host keytype base64key" per line ("[host]:port" when
// the port is not 22), so a line can be compared with the server's own. No platform code.
namespace sshkh {

inline constexpr size_t MAX_ENTRIES = 64;
inline constexpr size_t MAX_TEXT_BYTES = 32 * 1024;
inline constexpr size_t MAX_KEY_BLOB_BYTES = 2048;

enum class Verdict : uint8_t {
  Unknown,   // no line for this host and port: ask the user, showing the fingerprint
  Match,     // the pinned key
  Mismatch,  // a line for this host and port exists and the key is another one: hard stop
};

// "example.org" for port 22, "[example.org]:2222" otherwise; the host in lower case.
std::string hostField(std::string_view host, uint16_t port);

// `keyBlob` is the server's public key as it comes in the key exchange (SSH wire format: a string
// with the key type, then the key). Any line for the host with another key, of any type, is a
// mismatch: this client always asks for the same key types in the same order, so another type
// means the server's keys changed.
Verdict check(std::string_view knownHosts, std::string_view host, uint16_t port, const uint8_t* keyBlob,
              size_t keyBlobLen);

// The text with one more line. False (and `knownHosts` untouched) when the blob is not a key, the
// host has a character that cannot be in the file, or the file is full.
bool add(std::string& knownHosts, std::string_view host, uint16_t port, const uint8_t* keyBlob, size_t keyBlobLen);

// Takes out every line for the host and port ("Forget host key"). Returns how many.
size_t remove(std::string& knownHosts, std::string_view host, uint16_t port);

// "ssh-ed25519" from a key blob; empty when the blob is not well formed.
std::string keyType(const uint8_t* keyBlob, size_t keyBlobLen);

// "SHA256:" and the unpadded base64 of the digest, as `ssh-keygen -l` prints it. The digest is
// SHA-256 over the key blob (computed by the caller, which has the hash).
std::string fingerprint(const uint8_t sha256[32]);

// Standard base64. decode() is strict: false for a character outside the alphabet or bad padding.
std::string base64Encode(const uint8_t* data, size_t len, bool padding = true);
bool base64Decode(std::string_view text, std::vector<uint8_t>& out);

}  // namespace sshkh
