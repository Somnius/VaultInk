#pragma once

// Microsoft Compiled HTML Help (.chm) -> one XHTML page for the reader's text-book path (like lib/Html).
// Platform-free, host-tested (test/markup_books). Our own code, not taken from chmlib or 7-Zip; written from
// Matthew Russotto's "Microsoft's HTML Help (.chm) format" notes (the ITSF header, the ITSP directory header, the
// PMGL listing chunks and their variable-length numbers, the #SYSTEM file, the MSCompressed section with its
// ControlData and ResetTable), used from memory and then checked against a real .chm (see docs/book-formats.md).
// The decompression is lib/Chm/Lzx.
//
// The pages (the .htm / .html files inside) are written one after the other: first those the contents file
// (.hhc) lists, in its order, each under its contents title; then the rest in the order of the directory. Each
// page goes through the tag filter HTML and MOBI books use (mobi::HtmlFilter). Pictures, style sheets, scripts,
// the index (.hhk) and links between pages are left out. A page's encoding is the one it names; with none, the
// code page of the file's language when that is Greek or Cyrillic, else UTF-8 with Windows-1252 for the bytes
// that are not UTF-8.
//
// Refused: not an ITSF file (NotChm); a compression setting not known here, the E8 translation, a window above
// 2 MB (Unsupported); no page inside (NoPages).
//
// Memory, all with caps:
//  - the LZX window, the size the file asks for (64 KB as a rule, 2 MB at most), and MAX_FRAME_INPUT bytes of
//    compressed input: both from Memory::alloc (PSRAM on the device) the first time a compressed file is read;
//  - 16 bytes for each page, MAX_TOPICS at most: the pages past it are left out (Truncated);
//  - the contents titles, MAX_TOC_TITLE bytes each and MAX_TITLE_POOL in all: a title past it is not shown;
//  - MAX_TOC_FILE bytes of the contents file are read: pages listed past it come in directory order;
//  - one directory chunk (MAX_CHUNK_SIZE at most; larger: Unsupported), MAX_DIR_CHUNKS chunks (more: TooBig);
//  - MAX_FILE_SIZE (TooBig);
//  - the compressed section unpacks to MAX_PLAIN_SIZE and MAX_RATIO times its size at most (TooBig);
//  - frames unpacked for one book: 4 for each frame the section has, 2 for each page (what going back costs
//    with the usual reset every 2 frames), 256 more, and MAX_DECODED bytes in all (TooComplex): a file made so
//    that every page starts the unpacking again from far back;
//  - xhtml::MAX_PAGE_BYTES of page written (TooBig);
//  - more page bytes than the file can hold (entries that overlap): the pages stop there (Truncated).

#include <BlockSource.h>
#include <XhtmlOut.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace chm {

inline constexpr const char* CACHE_VERSION_TAG = "<!-- CHM_CACHE_VERSION: 1 -->";
inline constexpr uint32_t MAX_FILE_SIZE = 1024u * 1024 * 1024;
inline constexpr uint32_t MAX_CHUNK_SIZE = 16384;
inline constexpr uint32_t MAX_DIR_CHUNKS = 16384;
inline constexpr size_t MAX_TOPICS = 4096;
inline constexpr size_t MAX_TOC_TITLE = 160;
inline constexpr size_t MAX_TITLE_POOL = 64 * 1024;
inline constexpr uint32_t MAX_TOC_FILE = 4 * 1024 * 1024;
inline constexpr size_t MAX_TITLE = 200;
// The compressed section may unpack to this much, and to MAX_RATIO times its packed size, at most (TooBig).
inline constexpr uint32_t MAX_PLAIN_SIZE = 256u * 1024 * 1024;
inline constexpr uint32_t MAX_RATIO = 64;
// The most that is unpacked for one book, counting a frame again each time it is unpacked again (the unpacker
// only goes forward: a page that lies before the last one starts again from the reset before it).
inline constexpr uint32_t MAX_DECODED = 256u * 1024 * 1024;

enum class Status : uint8_t {
  Ok,
  Truncated,  // some pages are missing or cut because the file itself is damaged or past a cap: that is the book
  IoError,
  Oom,
  NotChm,
  Unsupported,
  TooBig,
  Corrupt,
  NoPages,
  WriteError,
  TooComplex  // reading it would unpack the same frames over and over (MAX_DECODED, the frame budget)
};
const char* statusText(Status status);

struct Info {
  std::string title;     // from #SYSTEM; empty when there is none
  std::string language;  // "el", "en", "ru"... from the file's language id; empty when not one of the few known here
};

using Source = blocksource::Source;

// Where the big blocks come from (the LZX window, the compressed frame, the page table, the contents titles).
// Left empty: malloc and free.
struct Memory {
  void* (*alloc)(size_t size) = nullptr;
  void (*release)(void* block) = nullptr;
  // Called every few frames unpacked and every 64 KB of pages read, so a long book lets the other tasks run
  // (the device gives vTaskDelay(1)). May be null.
  void (*pause)() = nullptr;
};

// The checks that refuse a file, and the title and language.
Status readInfo(const Source& source, Info& info, const Memory& memory = {});
// Every page as one XHTML page. `title` goes in <title>. Ok and Truncated both leave a complete page.
// Truncated is only for what the file itself lacks. A failed read (IoError), no memory (Oom), a page past
// xhtml::MAX_PAGE_BYTES (TooBig) and TooComplex mean the page must not be kept.
Status streamHtml(const Source& source, std::string_view title, xhtml::Emit emit, void* emitCtx,
                  const Memory& memory = {});

}  // namespace chm
