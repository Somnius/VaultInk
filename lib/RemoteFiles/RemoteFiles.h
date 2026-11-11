#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// What the WebDAV and FTP clients share: one entry of a remote folder, the caps on a listing, and
// the rules for a name that came from a server before it is used on the SD card. No platform code.
namespace remotefiles {

// A folder listing keeps no more than this many entries...
inline constexpr size_t MAX_ENTRIES = 500;
// ...and no more than this many bytes of names, whichever comes first.
inline constexpr size_t MAX_NAME_BYTES_TOTAL = 16 * 1024;
// A single name longer than this is skipped (FAT/exFAT long names end at 255 UTF-16 units).
inline constexpr size_t MAX_NAME_BYTES = 255;

struct Entry {
  std::string name;   // one path segment, UTF-8, already decoded
  uint64_t size = 0;  // bytes; 0 for folders and when the server did not say
  int64_t mtime = 0;  // epoch seconds (UTC), 0 when unknown
  bool isDir = false;
};

// A listing being filled: add() applies the caps and the name rule.
struct Listing {
  std::vector<Entry> entries;
  size_t nameBytes = 0;
  size_t skipped = 0;      // names that are not safe to use
  bool truncated = false;  // a cap was hit: there are more entries on the server

  // False when the entry was not kept (unsafe name, or a cap was hit).
  bool add(Entry entry);
  bool full() const { return truncated; }
};

// A name a server gave us that may be used as one path segment on the SD card and in a request:
// not empty, not "." or "..", no '/', '\\', no control characters, no characters FAT refuses
// (" * : < > ? |), not ending in a space or a dot, at most MAX_NAME_BYTES.
bool safeName(std::string_view name);

// Folders first, then by name without regard to ASCII case.
void sortEntries(std::vector<Entry>& entries);

// "/a" + "b" -> "/a/b"; "/" + "b" -> "/b". `dir` is an absolute path.
std::string joinPath(std::string_view dir, std::string_view name);
// "/a/b" -> "/a"; "/a" and "/" -> "/".
std::string parentPath(std::string_view path);
// An absolute path without "." / ".." / empty segments; false when it would climb above "/".
bool normalizePath(std::string_view path, std::string& out);

// Epoch seconds of a UTC date and time; 0 when a field is out of range or the year is before 1970.
int64_t epochUtc(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second);

// "12.3 MB", "512 B".
std::string sizeText(uint64_t bytes);

}  // namespace remotefiles
