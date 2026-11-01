#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Decoder for the lz-string JavaScript library's compressToBase64 output (lz-string 1.4/1.5), as the
// Obsidian Excalidraw plugin stores a drawing in a ```compressed-json block. Platform free, no
// exceptions, bounded memory: the dictionary is kept as compact arrays of (parent, char) pairs.
namespace lzstring {

constexpr size_t MAX_INPUT_CHARS = 512 * 1024;  // base64 characters (whitespace not counted)
constexpr size_t MAX_OUTPUT = 2 * 1024 * 1024;  // UTF-8 bytes
constexpr size_t MAX_DICT = 65536;              // dictionary entries

// Decodes `base64` (whitespace and newlines are ignored, as the plugin splits the text into lines)
// into `out` as UTF-8; the decoded UTF-16 is converted with surrogate pairs joined and a lone
// surrogate replaced by U+FFFD. Returns false, with `out` cleared, for an input over MAX_INPUT_CHARS, a
// result over MAX_OUTPUT, a dictionary over MAX_DICT, a character outside the base64 alphabet, a
// damaged stream or a failed allocation.
bool decompressFromBase64(std::string_view base64, std::string& out);

}  // namespace lzstring
