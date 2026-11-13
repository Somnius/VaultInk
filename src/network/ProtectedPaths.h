#pragma once

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <string>

// Credential stores no web request (file manager, plugin endpoints) or plugin
// manifest may read or write: their passwords are obfuscated with a device
// key, so the files must stay on the device. Mirrors the getFilePath() of
// WifiCredentialStore, OpdsServerStore, KOReaderCredentialStore and
// RemoteServerStore (the WebDAV, FTP and SSH servers), and the SSH folder
// (/.vaultink/ssh: the device's sealed private key and the pinned host keys,
// docs/ssh.md). Prefix match, so the stores' temp/backup siblings and
// everything in the SSH folder are covered too.
namespace protectedpaths {

inline constexpr const char* SENSITIVE_FILES[] = {"/.crosspoint/wifi.json", "/.crosspoint/opds.json",
                                                  "/.crosspoint/koreader.json", "/.crosspoint/remotes.json",
                                                  "/.vaultink/ssh"};

// SdFat also opens a name by its generated 8.3 alias (".crosspoint" is
// CROSSP~1), which no spelling check can map back to the long name.
inline bool isShortAlias(const char* first, const char* last) {
  const char* tilde = static_cast<const char*>(memchr(first, '~', static_cast<size_t>(last - first)));
  if (!tilde || tilde + 1 >= last || tilde[1] < '0' || tilde[1] > '9') return false;
  const char* dot = static_cast<const char*>(memchr(first, '.', static_cast<size_t>(last - first)));
  const size_t base = static_cast<size_t>((dot ? dot : last) - first);
  const size_t ext = dot ? static_cast<size_t>(last - dot - 1) : 0;
  return base <= 8 && ext <= 3;
}

inline bool startsWith8dot3Alias(const std::string& rest) {
  const size_t slash = rest.find('/');
  const size_t len = slash == std::string::npos ? rest.size() : slash;
  return isShortAlias(rest.data(), rest.data() + len);
}

inline bool isSensitivePath(const char* path) {
  // Canonicalise first ("//", "/./" and ".." would otherwise dodge the prefix
  // match while still opening the same file).
  std::string canon;
  canon.reserve(strlen(path) + 1);
  int depth = 0;
  for (const char* seg = path; *seg;) {
    while (*seg == '/') seg++;
    const char* end = strchr(seg, '/');
    const size_t len = end ? static_cast<size_t>(end - seg) : strlen(seg);
    if (len == 2 && seg[0] == '.' && seg[1] == '.') {
      const size_t slash = canon.rfind('/');
      canon.erase(slash == std::string::npos ? 0 : slash);
      if (depth > 0) depth--;
    } else {
      // SdFat skips leading spaces and drops trailing dots and spaces when it
      // opens a name, so compare the name it would actually open.
      const char* first = seg;
      const char* last = seg + len;
      while (first < last && *first == ' ') first++;
      while (last > first && (last[-1] == '.' || last[-1] == ' ')) last--;
      // The stores sit two levels deep; an alias further down cannot reach one.
      if (depth < 2 && isShortAlias(first, last)) return true;
      if (first < last) {
        canon += '/';
        canon.append(first, static_cast<size_t>(last - first));
        depth++;
      }
    }
    seg += len;
  }
  for (const char* file : SENSITIVE_FILES) {
    // FAT names are case-insensitive.
    if (strncasecmp(canon.c_str(), file, strlen(file)) == 0) return true;
  }
  return false;
}

// The device's own folders (settings, state, caches, the battery log): a file from a server never goes in there,
// whatever it is called. Case-insensitive like FAT, and the 8.3 alias counts as the folder.
inline bool isInDeviceFolder(const char* path) {
  std::string p(path);
  std::transform(p.begin(), p.end(), p.begin(),
                 [](const char c) { return static_cast<char>(tolower(static_cast<unsigned char>(c))); });
  size_t i = 0;
  while (i < p.size() && (p[i] == '/' || p[i] == ' ')) i++;
  const std::string rest = p.substr(i);
  const auto startsWithFolder = [&rest](const char* folder) {
    const size_t n = strlen(folder);
    return rest.compare(0, n, folder) == 0 && (rest.size() == n || rest[n] == '/');
  };
  return startsWithFolder(".crosspoint") || startsWithFolder(".vaultink") || startsWith8dot3Alias(rest);
}

// A plugin-supplied path (web request or device.json): absolute, no parent
// refs, not a credential store.
inline bool isPluginPath(const std::string& p) {
  return p.size() > 1 && p[0] == '/' && p.find("..") == std::string::npos && !isSensitivePath(p.c_str());
}

}  // namespace protectedpaths
