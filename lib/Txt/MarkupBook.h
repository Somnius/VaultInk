#pragma once

// Device glue for the books converted when opened: FB2 (lib/Fb2), loose HTML pages (lib/Html), RTF (lib/Rtf),
// Word 97-2003 documents (lib/Doc) and compiled HTML Help (lib/Chm). The Epub class treats them like a .txt or a
// .mobi (one spine item, XHTML streamed on demand), see Txt::isPlainBook.

#include <Print.h>

#include <memory>
#include <string>
#include <string_view>

class BookMetadataCache;

class MarkupBook {
 public:
  struct Info {
    std::string title;
    std::string author;
    std::string language;
  };

  static bool isMarkupBook(std::string_view path);
  // The tag at the top of the book's html/0.html; a converter change bumps it and the book is laid out again.
  static const char* cacheVersionTag(std::string_view path);
  // Reads the head of the file (for a .doc or a .chm: its directory and headers). False when it is not what its
  // extension says or can't be read (an .fb2 that is not FictionBook XML, an .rtf without "{\rtf", a UTF-16
  // .html, a .doc older than Word 97 or with a password, a .chm with no pages); the reason is logged.
  // `encrypted` is set for a password-protected .doc, which has its own message.
  static bool probe(const std::string& filepath, Info* info = nullptr, bool* encrypted = nullptr);
  static bool streamHtml(const std::string& filepath, Print& out);
  static bool buildCache(const std::string& filepath, const std::string& cachePath,
                         std::unique_ptr<BookMetadataCache>& bookMetadataCache);
};
