#pragma once

// Plain Mobipocket (MOBI6 / PalmDOC, .mobi/.prc, no DRM) reading, kos25k #26.
// Platform-free: the caller supplies random-access reads, so test/mobi builds it on the host.
// Not AZW/AZW3/KF8: a hybrid file's MOBI6 part is what record 0 describes, so it reads as MOBI6.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mobi {

// Random-access reader: bytes read at `offset`, or <0 on error.
struct Reader {
  void* ctx = nullptr;
  int (*readAt)(void* ctx, uint32_t offset, uint8_t* buf, size_t size) = nullptr;
  uint32_t fileSize = 0;
};

enum class Status : uint8_t {
  Ok,
  NotMobi,                 // not a PDB file of type BOOKMOBI / TEXtREAd
  BadHeader,               // record table or headers inconsistent
  Drm,                     // encryption type != 0: refused
  UnsupportedCompression,  // HUFF/CDIC (17480) or unknown
  IoError,
  Oom,
};

const char* statusText(Status s);

inline constexpr uint16_t COMPRESSION_NONE = 1;
inline constexpr uint16_t COMPRESSION_PALMDOC = 2;
inline constexpr uint16_t COMPRESSION_HUFF = 17480;
inline constexpr uint16_t ENCODING_CP1252 = 1252;
inline constexpr uint16_t ENCODING_UTF8 = 65001;
inline constexpr size_t MAX_RECORD_SIZE = 4096;  // PalmDOC text records are at most 4096 bytes uncompressed

struct Info {
  uint16_t numRecords = 0;
  uint16_t compression = 0;
  uint32_t textLength = 0;
  uint16_t textRecordCount = 0;  // records 1..textRecordCount hold the text
  uint16_t recordSize = 4096;
  uint16_t encryption = 0;
  bool hasMobiHeader = false;  // false for a bare PalmDOC text (.prc "TEXtREAd"): plain text, not HTML
  uint32_t mobiHeaderLength = 0;
  uint32_t mobiType = 0;
  uint32_t fileVersion = 0;
  uint32_t textEncoding = ENCODING_CP1252;
  uint32_t firstImageIndex = 0;  // 0 or 0xFFFFFFFF when none
  uint16_t extraFlags = 0;       // trailing-entry flags per text record
  bool hasKf8Boundary = false;   // hybrid file: EXTH 121 present
  std::string title;
  std::string author;
  std::string language;
};

// PDB record `index`: file offset and byte length. False when out of range.
bool recordSpan(const Reader& reader, uint16_t index, uint32_t& offset, uint32_t& length);

// Reads the PDB header, record 0 (PalmDOC + MOBI + EXTH headers). `Drm` for encrypted books.
Status open(const Reader& reader, Info& info);

// PalmDOC LZ77 decompression. Returns bytes written, or 0 when the input is malformed or `out` is
// too small (the format caps a record at 4096 bytes).
size_t palmdocDecompress(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap);

// Bytes of trailing entries (multibyte overlap + TBS/index data) at the end of a raw text record.
size_t trailingSize(const uint8_t* record, size_t length, uint16_t extraFlags);

// Transcodes one cp1252 byte to UTF-8 (1-3 bytes) into `out`, returning the byte count.
size_t cp1252ToUtf8(uint8_t c, char out[3]);

// Sink for the converted text: false stops the stream (reported as IoError).
struct Sink {
  void* ctx = nullptr;
  bool (*write)(void* ctx, const char* data, size_t size) = nullptr;
};

// Optional image resolver for <img recindex="N">: returns the path to emit as src, or empty to drop.
struct ImageResolver {
  void* ctx = nullptr;
  bool (*resolve)(void* ctx, uint32_t recindex, std::string& path) = nullptr;
};

// Streams the book as well-formed XHTML for the reader's expat layout parser, one text record
// (<= 4096 bytes decompressed) at a time. Needs about 2x MAX_RECORD_SIZE + 1 KB of heap.
Status streamHtml(const Reader& reader, const Info& info, std::string_view title, const Sink& out,
                  const ImageResolver* images = nullptr);

// Cache tag at the top of the streamed document; bump when the output changes.
inline constexpr const char* CACHE_VERSION_TAG = "<!-- MOBI_CACHE_VERSION: 1 -->";

}  // namespace mobi
