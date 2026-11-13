#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The device side of SSH (docs/ssh.md): where the key and the pinned host keys live on the card,
// the memory gate, and the Wi-Fi socket wolfSSH talks through. Everything here is empty in a build
// without VAULTINK_SSH=1 (available() is false and the screens do not offer SSH).
namespace sshdevice {

// Is SSH in this build?
constexpr bool available() {
#if defined(VAULTINK_SSH) && VAULTINK_SSH && !defined(SIMULATOR)
  return true;
#else
  return false;
#endif
}

// /.vaultink/ssh is closed to the web server, plugins, file servers and the developer link
// (src/network/ProtectedPaths.h).
inline constexpr char DIR[] = "/.vaultink/ssh";
inline constexpr char KEY_PATH[] = "/.vaultink/ssh/id_ed25519.key";
inline constexpr char KNOWN_HOSTS_PATH[] = "/.vaultink/ssh/known_hosts";

// Free internal heap a connection is started with: the key exchange and the sign-in allocate
// small blocks (elliptic-curve temporaries, the session's structures) that do not go to PSRAM.
// Below this the screens refuse with "not enough memory" before anything is sent.
// NOT MEASURED on the device yet (ROADMAP: SSH bench): chosen above the about 50 KB a reading
// session leaves, so SSH starts from Settings with no book loaded.
inline constexpr size_t MIN_FREE_HEAP = 60 * 1024;
inline constexpr size_t MIN_LARGEST_BLOCK = 24 * 1024;
bool heapAllowsConnection();

// --- the device's own key ---
bool hasKey();
// The public half; false when there is no key or it cannot be opened on this device.
bool publicKey(uint8_t out[32]);
// Both halves, for a sign-in. Wipe `seed` after use.
bool loadKey(uint8_t seed[32], uint8_t publicKeyOut[32]);
// Makes a new key and writes it, replacing any old one (the old public key stops working on
// every server it was added to).
bool createKey();
// "ssh-ed25519 AAAA... vaultink" for the server's authorized_keys, and its fingerprint.
bool publicKeyText(std::string& authorizedKeysLine, std::string& fingerprint);

// --- pinned host keys ---
// /.vaultink/ssh/known_hosts ends with a MAC line keyed from the device secret (sshkh::verify).
enum class HostKeys : uint8_t {
  Ok,          // `text` holds the pins, or is empty because there is no file yet (a first use)
  Unreadable,  // the file is there but could not be read whole, is too big or is not text
  Changed,     // the file is there but its MAC line is missing or wrong: changed outside the device
};
// Anything but Ok is a hard stop for a connection: no "trust this key?" question, nothing written.
// The way out is forgetAllHosts(), on the user's word.
HostKeys knownHosts(std::string& text);
// Adds one pin, keeping the others. False unless the file is Ok and the host has nothing pinned.
bool trustHost(const std::string& host, uint16_t port, const std::vector<uint8_t>& keyBlob);
// How many lines were taken out (0 also when the file is not Ok).
size_t forgetHost(const std::string& host, uint16_t port);
// Is there a file at all (for the "Forget all host keys" row)?
bool hasKnownHosts();
// Deletes the file without reading it: every server asks again at its next connection.
bool forgetAllHosts();

}  // namespace sshdevice
