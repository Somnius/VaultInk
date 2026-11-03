#pragma once

// Device glue for plain Mobipocket books (kos25k #26): the Epub class treats a .mobi/.prc like a
// .txt (one spine item, HTML streamed on demand), see Txt::isPlainBook.

#include <Print.h>

#include <memory>
#include <string>
#include <string_view>

#include "MobiFormat.h"

class BookMetadataCache;

class Mobi {
 public:
  static bool isMobi(std::string_view path);
  // Reads the headers. Drm / UnsupportedCompression / NotMobi name why a file is refused.
  static mobi::Status probe(const std::string& filepath, mobi::Info* info = nullptr);
  // Streams the book as XHTML. Images are extracted into cachePath (mobi_img_N.jpg|png|gif) the
  // first time and referenced by their SD path, like a note's embedded images.
  static bool streamHtml(const std::string& filepath, Print& out, const std::string& cachePath);
  static bool buildCache(const std::string& filepath, const std::string& cachePath,
                         std::unique_ptr<BookMetadataCache>& bookMetadataCache);
};
