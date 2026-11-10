#pragma once

// Device glue for the books converted from markup when opened: FB2 (lib/Fb2), loose HTML pages (lib/Html) and
// RTF (lib/Rtf). The Epub class treats them like a .txt or a .mobi (one spine item, XHTML streamed on demand),
// see Txt::isPlainBook.

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
  // Reads the head of the file. False when it is not what its extension says or can't be read (an .fb2 that is
  // not FictionBook XML, an .rtf without "{\rtf", a UTF-16 .html); the reason is logged.
  static bool probe(const std::string& filepath, Info* info = nullptr);
  static bool streamHtml(const std::string& filepath, Print& out);
  static bool buildCache(const std::string& filepath, const std::string& cachePath,
                         std::unique_ptr<BookMetadataCache>& bookMetadataCache);
};
