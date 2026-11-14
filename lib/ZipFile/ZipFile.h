#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

class ZipFile {
 public:
  struct FileStatSlim {
    uint16_t method;             // Compression method
    uint32_t compressedSize;     // Compressed size
    uint32_t uncompressedSize;   // Uncompressed size
    uint32_t localHeaderOffset;  // Offset of local file header
  };

  struct ZipDetails {
    uint32_t centralDirOffset;
    uint16_t totalEntries;
    bool isSet;
  };

  // Target for batch uncompressed size lookup (sorted by hash, then len)
  struct SizeTarget {
    uint64_t hash;   // FNV-1a 64-bit hash of normalized path
    uint16_t len;    // Length of path for collision reduction
    uint16_t index;  // Caller's index (e.g. spine index)
  };

  // FNV-1a 64-bit hash computed from char buffer (no std::string allocation)
  static uint64_t fnvHash64(const char* s, size_t len) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; i++) {
      hash ^= static_cast<uint8_t>(s[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }

 private:
  const std::string& filePath;
  HalFile file;
  ZipDetails zipDetails = {0, 0, false};
  std::unordered_map<std::string, FileStatSlim> fileStatSlimCache;

  // Cursor for sequential central-dir scanning optimization
  uint32_t lastCentralDirPos = 0;
  bool lastCentralDirPosValid = false;
  // Reuse the lookup when extraction follows a size query for the same entry
  // while the archive stays open
  std::string lastLookupName;
  FileStatSlim lastLookup = {};
  bool lastLookupValid = false;

  bool loadFileStatSlim(const char* filename, FileStatSlim* fileStat);
  long getDataOffset(const FileStatSlim& fileStat);
  bool loadZipDetails();

 public:
  explicit ZipFile(const std::string& filePath) : filePath(filePath) {}
  // True for the first bytes of a ZIP archive: "PK" followed by a local file header (03 04), the end
  // record of an empty archive (05 06) or a spanned-archive marker (07 08).
  static bool isZipSignature(const uint8_t* bytes, size_t len);
  // Reads the first four bytes of the file and checks them with isZipSignature(). Logs nothing, so a
  // caller can ask before treating a MOBI, FB2 or text book as an archive (the end-record scan logs
  // an error for those). False when the file cannot be opened.
  static bool hasZipSignature(const std::string& filePath);
  ~ZipFile() = default;
  // Zip file can be opened and closed by hand in order to allow for quick calculation of inflated file size
  // It is NOT recommended to pre-open it for any kind of inflation due to memory constraints
  bool isOpen() const { return !!file; }
  bool open();
  bool close();
  bool loadAllFileStatSlims();
  bool getInflatedFileSize(const char* filename, size_t* size);
  // Batch lookup: scan ZIP central dir once and fill sizes for matching targets.
  // targets must be sorted by (hash, len). sizes[target.index] receives uncompressedSize.
  // Returns number of targets matched.
  int fillUncompressedSizes(std::deque<SizeTarget>& targets, std::deque<uint32_t>& sizes);
  // Due to the memory required to run each of these, it is recommended to not preopen the zip file for multiple
  // These functions will open and close the zip as needed
  uint8_t* readFileToMemory(const char* filename, size_t* size = nullptr, bool trailingNullByte = false);
  // allowEarlyStop: a short write from `out` is treated as the sink asking to
  // stop (returns true) instead of a write failure — used by header probes
  // that only need the first bytes of an entry.
  bool readFileToStream(const char* filename, Print& out, size_t chunkSize, bool allowEarlyStop = false);
  // For ZipEntryReader: with the archive open, finds the entry, positions the file at its data and
  // fills the entry's method and sizes.
  bool seekToEntry(const char* filename, FileStatSlim* stat);
  HalFile& rawFile() { return file; }

  template <typename F>
  bool enumerateFilePaths(F&& callback) {
    if (!fileStatSlimCache.empty()) {
      for (const auto& entry : fileStatSlimCache) {
        callback(std::string_view{entry.first});
      }
      return true;
    }

    return enumerateFileEntries([&callback](std::string_view path, uint32_t, uint32_t) { callback(path); });
  }

  // Callback receives (path, crc32, compressedSize) for each central-directory
  // entry. Always scans the central directory: the slim-stat cache does not
  // hold CRCs.
  template <typename F>
  bool enumerateFileEntries(F&& callback) {
    const bool wasOpen = isOpen();
    if (!wasOpen && !open()) {
      return false;
    }

    if (!loadZipDetails()) {
      if (!wasOpen) {
        close();
      }
      return false;
    }

    file.seek(zipDetails.centralDirOffset);

    uint32_t sig;
    char itemName[256];

    while (file.available()) {
      file.read(&sig, 4);
      if (sig != 0x02014b50) {
        break;
      }

      file.seekCur(12);
      uint32_t crc32, compressedSize;
      file.read(&crc32, 4);
      file.read(&compressedSize, 4);
      file.seekCur(4);
      uint16_t nameLen, m, k;
      file.read(&nameLen, 2);
      file.read(&m, 2);
      file.read(&k, 2);
      file.seekCur(12);

      if (nameLen < sizeof(itemName)) {
        file.read(itemName, nameLen);
        itemName[nameLen] = '\0';
        callback(std::string_view{itemName, nameLen}, crc32, compressedSize);
      } else {
        file.seekCur(nameLen);
      }

      file.seekCur(m + k);
    }

    if (!wasOpen) {
      close();
    }
    return true;
  }
};

// Reads one zip entry a piece at a time, so a caller can look at its buttons between pieces (a
// deflated entry cannot be paused inside ZipFile::readFileToStream). Keeps the archive open for its
// lifetime; one reader at a time on a device.
class ZipEntryReader {
 public:
  explicit ZipEntryReader(const std::string& zipPath);
  ~ZipEntryReader();
  ZipEntryReader(const ZipEntryReader&) = delete;
  ZipEntryReader& operator=(const ZipEntryReader&) = delete;

  // Finds the entry and gets ready to inflate it. False when it is missing or not stored/deflated.
  bool open(const char* entryName, size_t chunkSize = 4096);
  // Up to `max` bytes of the entry into `out`. Returns the byte count; 0 at the end of the entry;
  // -1 when the data is broken (or the size does not match the zip's record).
  int read(uint8_t* out, size_t max);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
