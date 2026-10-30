#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// Plain text out of a PDF (Phase 7 spike): the pages' text in reading order of the content stream,
// as UTF-8 for a text reader. No layout, no pictures, no scanned pages (image only), no encryption.
// The file is read through a callback (random access, never held whole), and the zlib streams are
// inflated by a callback so the library stays free of platform code.
namespace pdf {

struct Source {
  uint64_t size = 0;
  // Reads exactly `len` bytes at `offset`; false when that isn't possible.
  std::function<bool(uint64_t offset, uint8_t* out, size_t len)> read;
};

// Inflates a zlib-wrapped stream into `out` (cleared first); false on error or over `maxOut` bytes.
using Inflate = std::function<bool(const uint8_t* in, size_t len, std::string& out, size_t maxOut)>;

struct Limits {
  size_t maxPages = 5000;
  size_t maxStreamBytes = 4 * 1024 * 1024;  // one decoded content or object stream
  size_t maxPageText = 256 * 1024;
};

struct Result {
  size_t pages = 0;        // pages found
  size_t textPages = 0;    // pages that gave some text
  bool encrypted = false;  // the file is encrypted: nothing was read
  bool xrefFailed = false;
};

// Calls `page` with each page's text, in page order (and how many pages there are). Returns false when the file isn't a
// PDF this reader can open (see Result for why).
bool extractText(const Source& source, const Inflate& inflate,
                 const std::function<void(size_t pageIndex, size_t pageCount, const std::string& text)>& page,
                 Result* result = nullptr, const Limits& limits = Limits());

// UTF-8 of a code point (exposed for tests).
void appendUtf8(std::string& out, uint32_t cp);

}  // namespace pdf
