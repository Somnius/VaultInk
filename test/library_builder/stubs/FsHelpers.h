#pragma once

#include <string>
#include <string_view>

namespace FsHelpers {
inline bool checkFileExtension(std::string_view path, const char* extension) { return path.ends_with(extension); }
inline bool hasEpubExtension(const std::string& path) { return checkFileExtension(path, ".epub"); }
inline bool hasXtcExtension(std::string_view path) {
  return checkFileExtension(path, ".xtc") || checkFileExtension(path, ".xtch");
}
inline bool hasCbzExtension(std::string_view path) { return checkFileExtension(path, ".cbz"); }
inline bool hasMobiExtension(std::string_view path) {
  return checkFileExtension(path, ".mobi") || checkFileExtension(path, ".prc") || checkFileExtension(path, ".azw") ||
         checkFileExtension(path, ".azw3");
}
}  // namespace FsHelpers
