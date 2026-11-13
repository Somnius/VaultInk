#include "SshDevice.h"

#if defined(VAULTINK_SSH) && VAULTINK_SSH && !defined(SIMULATOR)

#include <DeviceSecret.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SshKey.h>
#include <SshKnownHosts.h>
#include <SshSession.h>
#include <esp_heap_caps.h>

#include <cstring>

namespace sshdevice {

namespace {
bool writeWhole(const char* path, const uint8_t* data, const size_t len) {
  if (!Storage.ensureDirectoryExists(DIR)) return false;
  // Staged and swapped: a torn write must not replace a working file with half of one.
  const std::string tmp = std::string(path) + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite("SSH", tmp, file)) return false;
    const bool ok = file.write(data, len) == len;
    file.close();
    if (!ok) {
      Storage.remove(tmp.c_str());
      return false;
    }
  }
  return Storage.replaceFile(tmp.c_str(), path);
}
}  // namespace

bool heapAllowsConnection() {
  const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  LOG_INF("SSH", "Heap before connecting: %u free, largest block %u", static_cast<unsigned>(freeBytes),
          static_cast<unsigned>(largest));
  return freeBytes >= MIN_FREE_HEAP && largest >= MIN_LARGEST_BLOCK;
}

bool hasKey() { return Storage.exists(KEY_PATH); }

bool loadKey(uint8_t seed[32], uint8_t publicKeyOut[32]) {
  uint8_t file[sshkey::FILE_BYTES + 1];
  HalFile in;
  if (!Storage.openFileForRead("SSH", KEY_PATH, in)) return false;
  // One byte more than a key file has: a longer file is not a key file.
  const int got = in.read(file, sizeof(file));
  in.close();
  uint8_t secret[32];
  bool ok = got == static_cast<int>(sshkey::FILE_BYTES) && deviceSecret(secret);
  if (ok) ok = sshclient::openKeyFile(secret, file, sshkey::FILE_BYTES, seed, publicKeyOut);
  sshkey::wipe(secret, sizeof(secret));
  if (!ok) LOG_ERR("SSH", "The key file cannot be opened on this device");
  return ok;
}

bool publicKey(uint8_t out[32]) {
  uint8_t seed[32];
  const bool ok = loadKey(seed, out);
  sshkey::wipe(seed, sizeof(seed));
  return ok;
}

bool createKey() {
  uint8_t seed[32];
  uint8_t pub[32];
  uint8_t secret[32];
  uint8_t file[sshkey::FILE_BYTES];
  bool ok = deviceSecret(secret) && sshclient::generateKey(seed, pub) &&
            sshclient::sealKeyFile(secret, seed, pub, file) && writeWhole(KEY_PATH, file, sizeof(file));
  sshkey::wipe(seed, sizeof(seed));
  sshkey::wipe(secret, sizeof(secret));
  if (ok) {
    // Read it back: a key that cannot be opened again must not be handed out to servers.
    uint8_t check[32];
    ok = publicKey(check) && memcmp(check, pub, sizeof(pub)) == 0;
  }
  if (!ok) LOG_ERR("SSH", "Could not make the key");
  return ok;
}

bool publicKeyText(std::string& authorizedKeysLine, std::string& fingerprint) {
  uint8_t pub[32];
  if (!publicKey(pub)) return false;
  authorizedKeysLine = sshkey::authorizedKeysLine(pub, "vaultink");
  const std::vector<uint8_t> blob = sshkey::publicBlob(pub);
  fingerprint = sshclient::fingerprintOf(blob.data(), blob.size());
  return !fingerprint.empty();
}

namespace {
bool macCompute(void*, const std::string& canonicalText, uint8_t out[32]) {
  uint8_t secret[32];
  const bool ok = deviceSecret(secret) && sshclient::knownHostsMac(secret, canonicalText, out);
  sshkey::wipe(secret, sizeof(secret));
  return ok;
}
constexpr sshkh::Mac MAC{macCompute, nullptr};

// The file as it is on the card and what may be done with it. A file that is there but cannot be
// read whole is never "no file".
sshkh::FileState readKnownHosts(std::string& text) {
  text.clear();
  if (!Storage.exists(KNOWN_HOSTS_PATH)) return sshkh::FileState::Missing;
  bool readOk = false;
  HalFile file;
  if (Storage.openFileForRead("SSH", KNOWN_HOSTS_PATH, file)) {
    const uint64_t size = file.fileSize64();
    if (!file.isDirectory() && size <= sshkh::MAX_FILE_BYTES) {
      text.resize(static_cast<size_t>(size));
      size_t got = 0;
      while (got < text.size()) {
        const int n = file.read(&text[got], text.size() - got);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
      }
      // Whole, and nothing after what the size said.
      readOk = got == text.size() && file.read() < 0;
    }
    file.close();
  }
  const sshkh::FileState state = sshkh::verify(true, readOk, text, MAC);
  if (state != sshkh::FileState::Good) {
    LOG_ERR("SSH", "known_hosts is %s",
            state == sshkh::FileState::Changed ? "changed outside the device" : "unreadable");
    text.clear();
  }
  return state;
}

bool writeKnownHosts(std::string& text) {
  if (!sshkh::seal(text, MAC)) return false;
  return writeWhole(KNOWN_HOSTS_PATH, reinterpret_cast<const uint8_t*>(text.data()), text.size());
}
}  // namespace

HostKeys knownHosts(std::string& text) {
  switch (readKnownHosts(text)) {
    case sshkh::FileState::Missing:
    case sshkh::FileState::Good:
      return HostKeys::Ok;
    case sshkh::FileState::Changed:
      return HostKeys::Changed;
    case sshkh::FileState::Unreadable:
    default:
      return HostKeys::Unreadable;
  }
}

bool trustHost(const std::string& host, const uint16_t port, const std::vector<uint8_t>& keyBlob) {
  std::string text;
  // Only on top of a file that was read whole and is the device's own (or no file at all): from
  // any other state nothing is written, so the pins that are there are never replaced by one line.
  if (knownHosts(text) != HostKeys::Ok) return false;
  // Only a host with nothing pinned gets a line: a changed key is never accepted by adding one.
  if (sshkh::check(text, host, port, keyBlob.data(), keyBlob.size()) != sshkh::Verdict::Unknown) return false;
  if (!sshkh::add(text, host, port, keyBlob.data(), keyBlob.size())) return false;
  return writeKnownHosts(text);
}

size_t forgetHost(const std::string& host, const uint16_t port) {
  std::string text;
  if (knownHosts(text) != HostKeys::Ok) return 0;
  const size_t removed = sshkh::remove(text, host, port);
  if (removed == 0) return 0;
  return writeKnownHosts(text) ? removed : 0;
}

bool hasKnownHosts() { return Storage.exists(KNOWN_HOSTS_PATH); }

bool forgetAllHosts() {
  // The reset for a file that is unreadable or was changed outside the device: it is not read.
  Storage.remove((std::string(KNOWN_HOSTS_PATH) + ".tmp").c_str());
  if (!Storage.exists(KNOWN_HOSTS_PATH)) return true;
  return Storage.remove(KNOWN_HOSTS_PATH) && !Storage.exists(KNOWN_HOSTS_PATH);
}

}  // namespace sshdevice

#else  // no SSH in this build

namespace sshdevice {
bool heapAllowsConnection() { return false; }
bool hasKey() { return false; }
bool publicKey(uint8_t*) { return false; }
bool loadKey(uint8_t*, uint8_t*) { return false; }
bool createKey() { return false; }
bool publicKeyText(std::string&, std::string&) { return false; }
HostKeys knownHosts(std::string& text) {
  text.clear();
  return HostKeys::Unreadable;
}
bool hasKnownHosts() { return false; }
bool forgetAllHosts() { return false; }
bool trustHost(const std::string&, uint16_t, const std::vector<uint8_t>&) { return false; }
size_t forgetHost(const std::string&, uint16_t) { return 0; }
}  // namespace sshdevice

#endif
