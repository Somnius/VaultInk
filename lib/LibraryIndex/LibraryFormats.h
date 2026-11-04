#pragma once

// Which book types the Library lists (kos25k #18). One bit per type; 127 = all.
// Platform-free so it is host-tested (test/library_index_file).

#include <cstdint>
#include <string_view>

namespace library {

inline constexpr uint8_t FMT_EPUB = 1;
inline constexpr uint8_t FMT_PDF = 2;
inline constexpr uint8_t FMT_TXT = 4;
inline constexpr uint8_t FMT_MD = 8;
inline constexpr uint8_t FMT_XTC = 16;  // .xtc and .xtch
inline constexpr uint8_t FMT_CBZ = 32;
inline constexpr uint8_t FMT_MOBI = 64;  // .mobi, .prc, .azw and .azw3 (kos25k #26)
inline constexpr uint8_t FMT_ALL = 127;

namespace detail {
inline bool endsWithNoCase(std::string_view name, std::string_view ext) {
  if (name.size() < ext.size()) return false;
  const std::string_view tail = name.substr(name.size() - ext.size());
  for (size_t i = 0; i < ext.size(); i++) {
    char c = tail[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    if (c != ext[i]) return false;
  }
  return true;
}
}  // namespace detail

// The bit of a file's type; 0 for anything the filter does not cover.
inline uint8_t formatBitOf(std::string_view name) {
  using detail::endsWithNoCase;
  if (endsWithNoCase(name, ".epub")) return FMT_EPUB;
  if (endsWithNoCase(name, ".pdf")) return FMT_PDF;
  if (endsWithNoCase(name, ".txt")) return FMT_TXT;
  if (endsWithNoCase(name, ".md")) return FMT_MD;
  if (endsWithNoCase(name, ".xtc") || endsWithNoCase(name, ".xtch")) return FMT_XTC;
  if (endsWithNoCase(name, ".cbz")) return FMT_CBZ;
  if (endsWithNoCase(name, ".mobi") || endsWithNoCase(name, ".prc") || endsWithNoCase(name, ".azw") ||
      endsWithNoCase(name, ".azw3")) {
    return FMT_MOBI;
  }
  return 0;
}

inline bool formatAllowed(uint8_t mask, std::string_view name) {
  const uint8_t bit = formatBitOf(name);
  return bit == 0 || (mask & bit) != 0;
}

}  // namespace library
