#pragma once

#include <cstddef>
#include <string>
#include <vector>

// A name search over a folder tree (kos25k #13): the folded query (lower case, no accents, see
// TextFold) is a substring of the folded file or folder name. The walk is stepped, one folder per
// step(), so the caller can look at the buttons between steps and stop at any time. Platform-free:
// the folders are read through DirectorySource, so the walk is unit tested with a fake card.
namespace filesearch {

struct Entry {
  std::string name;
  bool isDirectory = false;
};

// One folder listing at a time. `path` has no trailing slash except the root "/".
class DirectorySource {
 public:
  virtual ~DirectorySource() = default;
  // Fills `out` with the folder's entries ("." and ".." excluded); false when the folder can't be read.
  virtual bool list(const std::string& path, std::vector<Entry>& out) = 0;
};

struct Hit {
  std::string name;    // the file or folder name, without a slash
  std::string folder;  // the folder it is in: "/" or "/Books/Classics"
  bool isDirectory = false;
};

// Skip rules shared with Browse Files: dot names (unless hidden files are shown) and the Windows
// "System Volume Information" folder.
bool skipped(const std::string& name, bool showHidden);

class Walker {
 public:
  static constexpr int MAX_DEPTH = 12;         // folders below the root that are entered
  static constexpr size_t MAX_HITS = 200;      // the list stops growing here
  static constexpr size_t MAX_PENDING = 4096;  // folders waiting to be read

  Walker(DirectorySource& source, const std::string& root, const std::string& query, bool showHidden);

  // Reads one folder: its matches join hits(), its sub folders join the queue. False when there is
  // nothing left to read (or the hit cap is reached); the matches so far stay.
  bool step();
  bool done() const { return pending.empty() || hits.size() >= MAX_HITS; }
  const std::vector<Hit>& results() const { return hits; }
  size_t foldersRead() const { return folders; }
  // True when the list is not the whole answer: the hit cap was reached with more to read, a match was left out, or a
  // folder was dropped because the waiting list was full.
  bool truncated() const { return cut || (hits.size() >= MAX_HITS && !pending.empty()); }
  // Drops the hit at `index` (the file was moved away).
  void erase(size_t index);

 private:
  struct Pending {
    std::string path;
    int depth;
  };
  DirectorySource& source;
  std::string folded;  // the query, folded; empty = nothing matches
  bool showHidden;
  std::vector<Pending> pending;  // a stack: depth first, so one folder's files come together
  std::vector<Hit> hits;
  std::vector<Entry> entries;  // reused per step
  size_t folders = 0;
  bool cut = false;
};

// Case and accent folding of a name for matching: composed first (a card written by macOS keeps
// names decomposed), then TextFold.
std::string foldName(const std::string& name);

}  // namespace filesearch
