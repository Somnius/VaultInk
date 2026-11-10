#pragma once

#include <string>
#include <string_view>

namespace FsHelpers {
inline std::string decodeUriEscapes(const std::string& path) { return path; }
inline std::string normalisePath(const std::string& path) { return path; }
inline bool hasTxtExtension(std::string_view name) { return name.ends_with(".txt"); }
inline bool hasMarkdownExtension(std::string_view name) { return name.ends_with(".md"); }
inline bool hasMobiExtension(std::string_view name) {
  return name.ends_with(".mobi") || name.ends_with(".prc") || name.ends_with(".azw") || name.ends_with(".azw3");
}
inline bool hasMarkupBookExtension(std::string_view name) {
  return name.ends_with(".fb2") || name.ends_with(".html") || name.ends_with(".htm") || name.ends_with(".xhtml") ||
         name.ends_with(".rtf");
}
}  // namespace FsHelpers
