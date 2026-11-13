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

// --- integrity of the file ---
// The file ends with one line "# vaultink-mac <64 hex digits>": a keyed MAC (HMAC-SHA256, the key
// made from the device secret by the caller) over canonical(). Someone who only has the SD card can
// then neither take a pin out (to get a fresh "trust this key?" question) nor put one in (to have a
// key of his own accepted without a question) unnoticed.
inline constexpr char MAC_PREFIX[] = "# vaultink-mac ";
// The whole file with its MAC line; a longer file is not read.
inline constexpr size_t MAX_FILE_BYTES = MAX_TEXT_BYTES + 128;

// The lines that pin something, as "host keytype key\n" each, in the order of the file: what the
// MAC is taken over. Comments, blank lines, spacing and the MAC line itself are not part of it.
std::string canonical(std::string_view knownHosts);

// The caller's MAC: false when it cannot be computed (no device secret).
struct Mac {
  bool (*compute)(void* ctx, const std::string& canonicalText, uint8_t out[32]) = nullptr;
  void* ctx = nullptr;
};

enum class FileState : uint8_t {
  Missing,     // no file: nothing is pinned yet, a first connection may ask
  Good,        // read whole and the MAC is right
  Unreadable,  // there, but it could not be read, is too big, or is not text: hard stop
  Changed,     // there, but the MAC line is missing or wrong: changed outside the device, hard stop
};
// `present`: the file exists. `readOk`: all of it was read (and it is not over MAX_FILE_BYTES).
// Only Missing and Good may be used for a connection; nothing is ever written from the other two.
FileState verify(bool present, bool readOk, std::string_view text, const Mac& mac);
// The text with its MAC line renewed (always the last line). False, and the text untouched, when
// the MAC cannot be computed.
bool seal(std::string& text, const Mac& mac);

// The host key rule of one connection. Before the sign-in a key passes only when it is the pinned
// one. After it (a server may exchange keys again at any time: OpenSSH's RekeyLimit, or after about
// 1 GB) only the very key that passed at the start does; anything else ends the session.
class KeyGate {
 public:
  void begin(std::string knownHosts, std::string host, uint16_t port);
  // For every host key the server shows. True only for a key that may be used.
  bool accept(const uint8_t* keyBlob, size_t keyBlobLen);
  // The sign-in is over. False when no key passed before (then nothing passes later either).
  bool signedIn();
  Verdict lastVerdict() const { return last_; }
  void reset();

 private:
  std::string knownHosts_;
  std::string host_;
  uint16_t port_ = 22;
  std::vector<uint8_t> accepted_;
  bool signedIn_ = false;
  Verdict last_ = Verdict::Unknown;
};

// Standard base64. decode() is strict: false for a character outside the alphabet or bad padding.
std::string base64Encode(const uint8_t* data, size_t len, bool padding = true);
bool base64Decode(std::string_view text, std::vector<uint8_t>& out);

}  // namespace sshkh
