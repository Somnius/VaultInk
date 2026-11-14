#pragma once

#include <ContentProtection.h>
#include <Print.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "Epub/css/CssParser.h"

class ZipFile;
class ZipEntryReader;

class Epub {
 public:
  struct SyncMetadata {
    std::string isbn;
    std::string asin;
    std::string series;
    std::optional<float> seriesIndex;
  };

 private:
  // the ncx file (EPUB 2)
  std::string tocNcxItem;
  // the nav file (EPUB 3)
  std::string tocNavItem;
  // where is the EPUBfile?
  std::string filepath;
  // the base path for items in the EPUB file
  std::string contentBasePath;
  // Uniq cache key based on filepath
  std::string cachePath;
  // Spine and TOC cache
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  // CSS parser for styling
  std::unique_ptr<CssParser> cssParser;
  // CSS files
  std::vector<std::string> cssFiles;
  // Optional encrypted-entry accessor. Entries are decoded in memory and stay
  // encrypted at rest. Null when the accessor is not needed or unavailable.
  std::unique_ptr<freeink::content::ContentDecryptor> decryptor;
  // User-presentable reason the encrypted-entry accessor could not be opened.
  std::string protectionError;

  // Hidden flows (lib/HiddenFlows): one flag and one total per spine item, held only while something is hidden.
  mutable std::mutex hiddenLock;
  std::vector<uint32_t> hiddenCumulative;
  std::vector<uint8_t> hiddenFlags;
  bool hiddenActive = false;
  bool fillHiddenTotals();

  bool findContentOpfFile(std::string* contentOpfFile, ZipFile* sharedZip = nullptr) const;
  bool parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, bool writeSpineEntries = true,
                       bool metadataOnly = false, ZipFile* sharedZip = nullptr);
  bool generateThumbBmpForCover(int height, const std::string& coverImageHref) const;
  bool openProtection();
  bool parseTocNcxFile() const;
  bool parseTocNavFile() const;
  void discoverCssFilesFromZip();
  CssParser::ParseResult parseCssFiles(CssParser::CacheStatus existingCacheStatus) const;

 public:
  explicit Epub(std::string filepath, const std::string& cacheDir);
  ~Epub() = default;
  std::string& getBasePath() { return contentBasePath; }
  // skipProtection: leave out the protected-content check (a scan of the archive's directory for
  // META-INF/encryption.xml). Only for callers that read cached numbers (spine, sizes, progress) and
  // never the book's content.
  bool load(bool buildIfMissing = true, bool skipLoadingCss = false, bool skipProtection = false);
  bool loadMetadata(std::string& title, std::string& author);
  bool loadSyncMetadata(SyncMetadata& metadata);
  bool clearCache() const;
  void setupCacheDir() const;
  const std::string& getCachePath() const;
  const std::string& getPath() const;
  // Empty unless the encrypted-entry accessor failed to open.
  const std::string& getProtectionError() const { return protectionError; }
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;
  std::string getCoverBmpPath(bool cropped = false, bool originalThresholds = false) const;
  bool generateCoverBmp(bool cropped = false, bool originalThresholds = false) const;
  std::string getThumbBmpPath() const;
  std::string getThumbBmpPath(int height) const;
  bool generateThumbBmp(int height) const;
  // Locate the cover without building spine, TOC, or reading caches.
  bool generateThumbBmpFromSource(int height);
  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr,
                                   bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize,
                                bool allowEarlyStop = false) const;
  // A reader that hands out one plain (not encrypted, not a note) zip item a piece at a time, so the caller can
  // check its buttons in between; nullptr when the item is not like that (use readItemContentsToStream).
  std::unique_ptr<ZipEntryReader> openItemReader(const std::string& itemHref) const;
  // Extract an item to a file on SD. On failure the partial file is removed.
  bool extractItemToFile(const std::string& itemHref, const std::string& destPath) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  int getSpineItemsCount() const;
  int getTocItemsCount() const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;
  size_t getCumulativeSpineItemSize(int spineIndex) const;
  // First spine index whose cumulative size reaches `bytes`, or -1 past the end.
  int getSpineIndexForSize(size_t bytes) const;
  int getSpineIndexForTextReference() const;

  size_t getBookSize() const;
  float calculateProgress(int currentSpineIndex, float currentSpineRead) const;

  // Hidden flows: chapters the reader hid are left out of the size, percent and page count of the book.
  // The list is the book's hidden.bin (spine hrefs) in its cache folder; EPUB only. calculateProgress() and
  // getBookSize() stay raw (KOReader sync, bookmarks). Call the writers with the render task held off.
  bool supportsHiddenFlows() const;
  void loadHiddenFlows();
  bool saveHiddenFlows();
  // clearCache() that carries hidden.bin over (the reader's Delete Book Cache); the cache folder is left in place.
  bool clearCacheKeepHidden() const;
  bool setSpineRangeHidden(int firstSpineIndex, int lastSpineIndex, bool hidden);
  // The spine items contents entry `tocIndex` stands for; false when it has none.
  bool getTocSpineRange(int tocIndex, int& firstSpineIndex, int& lastSpineIndex) const;
  bool isSpineHidden(int spineIndex) const;
  bool hasAnyHiddenSpine() const;
  // True when the visible numbers differ from the raw ones (something is hidden and something is left).
  bool hasHiddenFlows() const;
  size_t visibleBookSize() const;
  float visibleProgress(int currentSpineIndex, float currentSpineRead) const;
  // The shown chapter that holds counted byte `visibleBytes` and how far into it; false when !hasHiddenFlows().
  bool locateVisibleSize(size_t visibleBytes, int& spineIndex, float& fraction) const;
  CssParser* getCssParser() const { return cssParser.get(); }
  int resolveHrefToSpineIndex(const std::string& href) const;
};
