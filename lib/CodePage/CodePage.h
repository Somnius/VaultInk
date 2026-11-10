#pragma once

// Single-byte code pages to UTF-8 for the converted book formats (FB2, HTML, RTF). Platform-free, host-tested
// (test/markup_books). Windows-1251 (Cyrillic), 1252 (Western), 1253 (Greek) and ISO-8859-1 / -7; nothing else.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace codepage {

enum class Id : uint8_t { Utf8, Cp1251, Cp1252, Cp1253, Iso8859_1, Iso8859_7 };

// "windows-1253", "cp1253", "ISO-8859-7", "greek", "utf-8"... (case-insensitive). False for a name not known here.
bool fromName(std::string_view name, Id& id);
// A Windows code page number (RTF \ansicpgN, \cpgN): 1251, 1252, 1253, 28591, 28597, 65001. False for any other.
bool fromWindowsNumber(int number, Id& id);
// An RTF \fcharsetN: 161 Greek, 204 Russian. False for the rest (0 ANSI and 1 default mean "the document's").
bool fromRtfCharset(int charset, Id& id);

// One byte of a single-byte code page as a code point; 0 for a byte the page leaves undefined (and for Utf8).
uint16_t toUnicode(Id id, uint8_t byte);

// 1 to 4 bytes; 0 for a surrogate or a value past U+10FFFF.
size_t encodeUtf8(uint32_t codePoint, char out[4]);

// True for a code point XML 1.0 allows in a document (so expat will not refuse the page).
inline bool xmlChar(const uint32_t cp) {
  return cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) || (cp >= 0xE000 && cp <= 0xFFFD) ||
         (cp >= 0x10000 && cp <= 0x10FFFF);
}

// Bytes in any chunking -> clean UTF-8: only characters XML allows, never a broken sequence. For Utf8 a byte that is
// not valid UTF-8 is read as Windows-1252 (the usual cause is a page that never said it was Windows-1252).
class Decoder {
 public:
  explicit Decoder(Id id = Id::Utf8) : id_(id) {}
  void setEncoding(Id id) { id_ = id; }
  Id encoding() const { return id_; }
  // `out` needs room for MAX_OUT_PER_BYTE * size + MAX_PENDING_OUT bytes. Returns the bytes written.
  size_t feed(const uint8_t* in, size_t size, char* out);
  // The end of the input: an unfinished UTF-8 sequence is written as Windows-1252. `out` needs MAX_PENDING_OUT.
  size_t finish(char* out);

  static constexpr size_t MAX_OUT_PER_BYTE = 3;
  static constexpr size_t MAX_PENDING_OUT = 9;

 private:
  size_t flushPending(char* out);
  static size_t single(Id id, uint8_t byte, char* out);

  Id id_;
  uint8_t pending_[4] = {};
  uint8_t pendingLen_ = 0;
  uint8_t need_ = 0;
};

}  // namespace codepage
