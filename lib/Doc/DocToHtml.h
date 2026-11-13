#pragma once

// Word 97-2003 binary documents (.doc) -> XHTML for the reader's text-book path (like lib/Rtf). Platform-free,
// host-tested (test/markup_books). Our own code, written from the published formats: [MS-CFB] (the OLE2 compound
// file) and [MS-DOC] (the FIB, the Clx / PlcPcd piece table, FcCompressed, the special characters). The offsets
// were written from memory of those documents (no network here) and then checked against files LibreOffice wrote,
// see docs/book-formats.md for what was and was not tried.
//
// Text only: the main text, then the footnotes and the endnotes. Paragraph marks, line breaks, page breaks, tabs;
// a field shows its result and not its instruction; a table is text with " | " between the cells and a row per
// paragraph. No bold or italic, no headings, no pictures, no headers or footers, no comments, no text boxes.
// Text marked hidden or deleted (tracked changes) is shown, because the character formatting is not read.
//
// Refused: a file that is not an OLE2 compound file or has no WordDocument stream (NotDoc), Word 6 / 95 and
// older (Unsupported), a password-protected document, RC4 or XOR (Encrypted).
//
// Memory: the two streams are read through a list of extents (12 bytes per unbroken run of sectors), the FAT
// through a 512-byte window, the text 512 bytes at a time. Caps and what happens past them:
//  - MAX_FILE_SIZE: TooBig;
//  - MAX_FAT_SECTORS FAT sectors: TooBig;
//  - MAX_EXTENTS runs in one stream (a very fragmented file): TooBig;
//  - MAX_DIR_ENTRIES directory entries looked at: a stream past them is not found (NotDoc);
//  - MAX_PIECES entries in the piece table: TooBig;
//  - more text bytes than the WordDocument stream holds (pieces that overlap): the text stops there (Truncated);
//  - MAX_FIELD_DEPTH nested fields: a deeper field is hidden whole;
//  - MAX_INFO_TEXT bytes of title or author: cut;
//  - xhtml::MAX_PAGE_BYTES of page: TooBig.

#include <BlockSource.h>
#include <XhtmlOut.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace doc {

inline constexpr const char* CACHE_VERSION_TAG = "<!-- DOC_CACHE_VERSION: 1 -->";
inline constexpr uint32_t MAX_FILE_SIZE = 256u * 1024 * 1024;
inline constexpr uint32_t MAX_FAT_SECTORS = 4096;
inline constexpr size_t MAX_EXTENTS = 4096;
inline constexpr uint32_t MAX_DIR_ENTRIES = 4096;
inline constexpr uint32_t MAX_PIECES = 65536;
inline constexpr size_t MAX_FIELD_DEPTH = 16;
inline constexpr size_t MAX_INFO_TEXT = 200;

enum class Status : uint8_t {
  Ok,
  Truncated,  // the file itself is damaged or cut short: what was read is in the page, and that is the book
  IoError,
  Oom,
  NotDoc,
  Unsupported,
  Encrypted,
  TooBig,
  Corrupt,
  WriteError,
  NoText  // nothing to read in it (pictures only, or empty)
};
const char* statusText(Status status);

struct Info {
  std::string title;     // from the SummaryInformation stream; empty when there is none
  std::string author;    // the same
  std::string language;  // "el", "en", "ru"... from the FIB's language id; empty when not one of the few known here
};

using Source = blocksource::Source;

// The checks that refuse a file (NoText among them: the text is read up to its first letter), and the title,
// author and language.
Status readInfo(const Source& source, Info& info);
// The whole document as one XHTML page. `title` goes in <title>. Ok and Truncated both leave a complete page.
// Truncated is only for a file that is damaged or cut short itself. A read that fails inside the file (the
// card) is IoError and a page past xhtml::MAX_PAGE_BYTES is TooBig: both mean the page must not be kept.
Status streamHtml(const Source& source, std::string_view title, xhtml::Emit emit, void* emitCtx);

}  // namespace doc
