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
// .mobi, .prc, .azw and .azw3 (kos25k #26), and the other converted book formats: .fb2, .html / .htm / .xhtml and
// .rtf. They share this bit because the Library's mask is a uint8_t whose eighth bit is BROWSE_PICTURES in the
// Browse Files mask: a new bit would mean widening both stored masks and migrating them. Sharing keeps every
// stored mask valid as it is; the row is called "MOBI / AZW / FB2 / HTML / RTF".
inline constexpr uint8_t FMT_MOBI = 64;
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
  if (endsWithNoCase(name, ".fb2") || endsWithNoCase(name, ".html") || endsWithNoCase(name, ".htm") ||
      endsWithNoCase(name, ".xhtml") || endsWithNoCase(name, ".rtf")) {
    return FMT_MOBI;
  }
  return 0;
}

// Browse Files (0.72.0, kos25k #6): the seven book formats above plus three more switches. A wider mask than the
// Library's, which stays at seven bits.
inline constexpr uint16_t BROWSE_PICTURES = 128;
inline constexpr uint16_t BROWSE_DRAWINGS = 256;  // .canvas and .excalidraw / .excalidraw.md
inline constexpr uint16_t BROWSE_ZIP = 512;
inline constexpr uint16_t BROWSE_ALL = 1023;

// The Browse Files switch a file belongs to; 0 for anything without one (it always shows).
inline uint16_t browseBitOf(std::string_view name) {
  using detail::endsWithNoCase;
  if (endsWithNoCase(name, ".canvas") || endsWithNoCase(name, ".excalidraw") ||
      endsWithNoCase(name, ".excalidraw.md")) {
    return BROWSE_DRAWINGS;
  }
  if (endsWithNoCase(name, ".zip")) return BROWSE_ZIP;
  if (endsWithNoCase(name, ".jpg") || endsWithNoCase(name, ".jpeg") || endsWithNoCase(name, ".png") ||
      endsWithNoCase(name, ".bmp") || endsWithNoCase(name, ".gif")) {
    return BROWSE_PICTURES;
  }
  return formatBitOf(name);
}

inline bool browseAllowed(uint16_t mask, std::string_view name) {
  const uint16_t bit = browseBitOf(name);
  return bit == 0 || (mask & bit) != 0;
}

inline bool formatAllowed(uint8_t mask, std::string_view name) {
  const uint8_t bit = formatBitOf(name);
  return bit == 0 || (mask & bit) != 0;
}

// The Books tile on the home screen counts books only (kos25k #14): not .txt and .md notes, not canvases, and
// only the formats ticked in Book formats.
inline bool countsAsBook(uint8_t mask, std::string_view name) {
  const uint8_t bit = formatBitOf(name);
  return bit != 0 && bit != FMT_TXT && bit != FMT_MD && (mask & bit) != 0;
}

}  // namespace library
