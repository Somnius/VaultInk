#pragma once

// FictionBook 2 (.fb2, plain XML, not the zipped .fb2.zip / .fbz) -> the XHTML page the reader's text-book path
// lays out (like lib/Mobi). Platform-free apart from expat, host-tested (test/markup_books).
//
// Read with expat as a stream: sections, titles (h1 for the book's, h2-h4 by section depth), subtitles,
// paragraphs, emphasis, strong, strikethrough, sub, sup, code, epigraphs / cites / annotations (block quotes),
// poems (a stanza is one paragraph, a line break after each verse), text-author, tables, empty lines. Every
// <body> is read, so a notes body follows the text; a note link shows raised, and is not a link. Pictures are
// skipped: <image> is ignored and the <binary> blocks are parsed past without being kept.
//
// Encoding: UTF-8, UTF-16, ISO-8859-1 (expat's own) and Windows-1251 / 1252 / 1253, ISO-8859-7. Any other
// encoding is refused (Status::Malformed).
//
// Caps and what happens past them:
//  - MAX_DEPTH nested elements: Status::TooDeep;
//  - MAX_PARSER_MEMORY for all of expat's allocations (a single huge tag or comment would grow its buffer):
//    Status::Oom;
//  - MAX_INFO_TEXT bytes of title, author or language: cut at a character;
//  - INFO_SCAN_LIMIT bytes read by readInfo(): the title is then not found and the caller uses the file name.
// A file that stops being XML part-way (cut short) gives Status::Truncated with what was read closed properly.

#include <XhtmlOut.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fb2 {

inline constexpr const char* CACHE_VERSION_TAG = "<!-- FB2_CACHE_VERSION: 1 -->";
inline constexpr size_t INFO_SCAN_LIMIT = 64 * 1024;
inline constexpr size_t MAX_INFO_TEXT = 200;
inline constexpr size_t MAX_DEPTH = 64;
inline constexpr size_t MAX_PARSER_MEMORY = 64 * 1024;

enum class Status : uint8_t { Ok, Truncated, IoError, Oom, NotFb2, Malformed, TooDeep, WriteError, TooBig };
const char* statusText(Status status);

struct Info {
  std::string title;     // <book-title>
  std::string author;    // the first <author>: first, middle and last name, or the nickname
  std::string language;  // <lang>
};

using Read = int (*)(void* ctx, uint8_t* buf, size_t size);  // bytes read, 0 at the end, < 0 on an error

// Title, author and language from <description><title-info>. NotFb2 when the root element is not FictionBook.
Status readInfo(void* ctx, Read read, Info& info);
// The whole book as one XHTML page. `title` goes in <title>. Ok or Truncated mean a page was written.
Status streamHtml(void* ctx, Read read, std::string_view title, xhtml::Emit emit, void* emitCtx);

}  // namespace fb2
