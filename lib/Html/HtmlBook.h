#pragma once

// A loose .html / .htm / .xhtml file -> the XHTML page the reader's text-book path lays out (like lib/Mobi).
// Platform-free, host-tested (test/markup_books).
//
// The tags go through the tolerant scanner MOBI books already use (mobi::HtmlFilter): headings, p, div, br, hr,
// em / strong / i / b, lists, blockquote, pre, tables; script, style and head are dropped with their text, unknown
// tags are dropped and their text kept, entities are decoded, unclosed tags are closed. Pictures and links are not
// followed (a loose page has no book around it to hold them).
//
// Encoding: a UTF-8 BOM, <meta charset>, the http-equiv content type or the XML declaration name it. Known:
// UTF-8, Windows-1251 / 1252 / 1253, ISO-8859-1 / -7. With no name, or one not known here, the page is read as
// UTF-8 and every byte that is not valid UTF-8 as Windows-1252. UTF-16 is refused (Status::Unsupported).
//
// Memory is fixed: the file is read CHUNK bytes at a time; SNIFF_SIZE bytes are looked at for the encoding, the
// language and the title (a <title> past that, or longer than MAX_TITLE, is not used or is cut).

#include <CodePage.h>
#include <XhtmlOut.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace htmlbook {

inline constexpr const char* CACHE_VERSION_TAG = "<!-- HTML_CACHE_VERSION: 1 -->";
inline constexpr size_t SNIFF_SIZE = 4096;
inline constexpr size_t MAX_TITLE = 200;

enum class Status : uint8_t { Ok, IoError, Oom, Unsupported, WriteError };
const char* statusText(Status status);

struct Info {
  std::string title;     // from <title>, entities decoded, white space folded; empty when there is none
  std::string language;  // from <html lang>, two letters; empty when there is none
  codepage::Id encoding = codepage::Id::Utf8;
  bool utf16 = false;
  bool named = false;  // a BOM or the page names the encoding (lib/Chm falls back to the file's code page)
  size_t bomSize = 0;
};

using Read = int (*)(void* ctx, uint8_t* buf, size_t size);  // bytes read, 0 at the end, < 0 on an error

// Looks at the head of the file (up to SNIFF_SIZE bytes of it).
void sniff(std::string_view head, Info& info);
Status readInfo(void* ctx, Read read, Info& info);
// The whole page as one XHTML page. `title` goes in <title>; empty uses the page's own.
Status streamHtml(void* ctx, Read read, std::string_view title, xhtml::Emit emit, void* emitCtx);

}  // namespace htmlbook
