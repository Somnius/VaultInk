#pragma once

// Which SD paths the developer link may touch (CMD:LS, CMD:GET, CMD:PUT, CMD:FLASH, CMD:FETCH; docs/devlink.md).
// An allow-list: three folders and nothing else, so settings, saved passwords, vaults and books are
// out of reach whatever the path is spelled like. Header-only and platform-free (test/dev_link);
// only DevLinkDevice.cpp includes it, inside VAULTINK_DEVLINK.

#include <cstddef>
#include <string>

#include "network/ProtectedPaths.h"

namespace devlinkpaths {

enum class Access { List, Read, Write, Flash };

constexpr size_t MAX_PATH_BYTES = 200;

inline char lowerAscii(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline std::string lowered(const std::string& text) {
  std::string out(text);
  for (char& c : out) c = lowerAscii(c);
  return out;
}

// One spelling per file: absolute, no "..", no "." part, no empty part ("//"), no end slash, no
// backslash or control byte, no part that SdFat would open under another name (blanks in front,
// dots or blanks at the end, an 8.3 alias such as CROSSP~1).
inline bool isPlainPath(const std::string& path) {
  if (path.size() < 2 || path.size() > MAX_PATH_BYTES || path[0] != '/') return false;
  if (path.find("..") != std::string::npos) return false;
  size_t start = 1;
  while (true) {
    const size_t slash = path.find('/', start);
    const size_t end = slash == std::string::npos ? path.size() : slash;
    if (end == start) return false;  // "//" or a slash at the end
    const char* first = path.data() + start;
    const char* last = path.data() + end;
    if (*first == ' ' || last[-1] == ' ' || last[-1] == '.') return false;
    if (end - start == 1 && *first == '.') return false;
    if (protectedpaths::isShortAlias(first, last)) return false;
    for (const char* c = first; c < last; ++c) {
      const unsigned char byte = static_cast<unsigned char>(*c);
      if (byte < 0x20 || byte == 0x7f || byte == '\\' || byte == ':') return false;
    }
    if (slash == std::string::npos) return true;
    start = slash + 1;
  }
}

// True when `path` is `folder` itself (self) or something under it. Both lower case.
inline bool isUnder(const std::string& path, const char* folder) {
  const std::string prefix = std::string(folder) + "/";
  return path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
}

// The developer link's rule for one path.
//   /debug        list, read and write, with everything under it (the debug logs)
//   /screenshots  list and read, with everything under it
//   /.vaultink    list the folder itself; read the bench log, its old file, its setting and the
//                 battery log; write the bench setting. Nothing else in there: it also holds the
//                 lock PIN and the LiveSync profile and setup text.
//   Flash         a .bin anywhere on the card outside the device's own folders (it is only read
//                 as a firmware image and checked as one; nothing of it is sent back). CMD:FETCH
//                 writes under the same rule: only a file CMD:FLASH could then read.
// FAT names ignore case, so the comparison does too.
inline bool isAllowed(const std::string& rawPath, const Access access) {
  if (!isPlainPath(rawPath)) return false;
  if (protectedpaths::isSensitivePath(rawPath.c_str())) return false;
  const std::string path = lowered(rawPath);

  if (access == Access::Flash) {
    if (protectedpaths::isInDeviceFolder(path.c_str())) return false;
    return path.size() > 5 && path.compare(path.size() - 4, 4, ".bin") == 0;
  }

  if (protectedpaths::isInDeviceFolder(path.c_str())) {
    // .crosspoint, .vaultink or an alias of either (an alias never gets past isPlainPath).
    if (access == Access::List) return path == "/.vaultink";
    if (path == "/.vaultink/bench.cfg") return true;
    if (access == Access::Write) return false;
    return path == "/.vaultink/bench.csv" || path == "/.vaultink/bench.old.csv" || path == "/.vaultink/battery-log.tsv";
  }

  if (isUnder(path, "/debug")) return true;
  if (path == "/debug") return access == Access::List;
  if (access == Access::Write) return false;
  if (isUnder(path, "/screenshots")) return true;
  return path == "/screenshots" && access == Access::List;
}

// The folder a file is in ("/debug/a/b.txt" gives "/debug/a"); the path must be plain.
inline std::string parentOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
}

}  // namespace devlinkpaths
