#pragma once

#include <Print.h>

#include <memory>
#include <string>
#include <string_view>

class BookMetadataCache;

class Txt {
 public:
  static bool isTxtOrMd(std::string_view path);
  // TXT, MD, MOBI, FB2, HTML or RTF: the Epub class reads these as one streamed HTML section instead of a zip.
  static bool isPlainBook(std::string_view path);
  static bool validateCache(const std::string& filepath, const std::string& cachePath, size_t cachedSize);
  static void invalidateCache(const std::string& cachePath);
  // A PDF's converted text (kos25k #29) is made with or without its style marks. The cache folder remembers which in
  // pdfstyle.txt; when the Style setting is not what it was built with, the html and the pages are thrown away.
  // Other text books are left alone.
  static void syncPdfStyle(const std::string& filepath, const std::string& cachePath, bool styled);
  // With a cachePath, a note's embedded notes are listed in cachePath/embeds.txt ("size\tpath" per
  // line) after a complete conversion, so validateCache() can see an embedded note change.
  static bool streamTxtToHtml(const std::string& filepath, Print& out, const std::string& cachePath = "");
  static std::string findCompanionCoverImage(const std::string& filepath);
  static bool convertCoverImageToBmp(const std::string& imagePath, const std::string& destBmpPath, int thumbHeight = 0,
                                     bool cropped = false, bool originalThresholds = false);
  static bool buildTxtCache(const std::string& filepath, const std::string& cachePath,
                            std::unique_ptr<BookMetadataCache>& bookMetadataCache);
};
