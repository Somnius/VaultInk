#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "VaultLink.h"

// The vault's search index: one text line per note, kept outside the vault on the SD card. Built
// incrementally (a note whose size didn't change keeps its line) and searched by streaming the
// file line by line, so memory stays bounded. A small state file next to it holds a fingerprint
// of the vault's folder listing: while that matches, build() only walks the folders and touches
// neither the index nor the notes. File access goes through callbacks for host tests.
namespace vaultindex {

constexpr const char* INDEX_VERSION = "vaultink-index 5";  // 3: outgoing links, 4: headings, 5: block ids
// Per note: words kept for search and bytes read from the note.
constexpr size_t MAX_TERMS_PER_NOTE = 1500;
constexpr size_t MAX_NOTE_BYTES = 256 * 1024;
constexpr size_t MAX_LINKS_PER_NOTE = 200;
constexpr size_t MAX_HEADINGS_PER_NOTE = 64;
constexpr size_t MAX_BLOCK_IDS_PER_NOTE = 64;
// Longest index line written; the reader's line buffer must be at least this big.
constexpr size_t MAX_LINE_BYTES = 60 * 1024;

struct Note {
  std::string path;    // full SD path
  uint64_t stamp = 0;  // file size and modification time when indexed (IndexFs::stamp)
  std::string title;   // file name without .md
  std::vector<std::string> aliases;
  std::vector<std::string> tags;      // without '#', as written
  std::vector<std::string> links;     // link targets as written, without |alias, #heading or .md
  std::vector<std::string> headings;  // heading texts as written, without the '#'s; block ids as "^id"
  std::vector<std::string> terms;     // folded words, unique
};

struct IndexFs : vaultlink::Fs {
  // Changes whenever the file does (size and modification time); 0 when it can't be read.
  std::function<uint64_t(const std::string& path)> stamp;
  // Optional: `list` with each file's stamp taken from the folder entry, so build() doesn't open
  // every note by path. The stamps must equal what `stamp` returns. Without it, `stamp` is used.
  std::function<void(const std::string& dir,
                     const std::function<void(const std::string& name, bool isDir, uint64_t stamp)>& entry)>
      listStamped;
  // Reads up to `cap` bytes of a file into `out`.
  std::function<bool(const std::string& path, size_t cap, std::string& out)> read;
  // Streams the lines of a file (without '\n') to `line`; stops when it returns false.
  std::function<bool(const std::string& path, const std::function<bool(std::string_view line)>& line)> lines;
  // Writes a whole new file line by line: `begin` starts it, `append` adds bytes, `commit`
  // replaces the old file (false = discard).
  std::function<bool(const std::string& path)> begin;
  std::function<bool(std::string_view text)> append;
  std::function<bool(bool keep)> commit;
};

// Where a vault's index lives: /.vaultink/<id>/index.tsv, outside the vault. Next to it,
// files.txt lists every file in the vault (one path per line) for vaultlink's `known` lookup.
std::string indexPathFor(const std::string& root);
std::string fileListPathFor(const std::string& root);
// state.txt next to them: one line with the fingerprint of the folder listing the index and the
// file list were last brought up to date with.
std::string statePathFor(const std::string& root);
constexpr const char* STATE_VERSION = "vaultink-state 1";
constexpr size_t MAX_INDEXED_NOTES = 5000;
constexpr size_t MAX_LISTED_FILES = 20000;

// Parses a note's text (frontmatter aliases/tags, inline #tags, words).
Note scanNote(const std::string& path, std::string_view text);

// One index line for a note, and back.
std::string toLine(const Note& note);
bool fromLine(std::string_view line, Note& note);

// Every .md under `root` (dot folders skipped), shortest paths first.
std::vector<std::string> listNotes(const std::string& root, const IndexFs& fs, size_t maxNotes = 5000);

struct BuildStats {
  size_t notes = 0;
  size_t rescanned = 0;
  bool unchanged = false;  // the folder listing matched the state file: nothing was read or written
  bool rewritten = false;  // index.tsv was written again
};
// Brings the index at `indexPath` up to date with the vault at `root`, and rewrites the file list
// next to it (fileListPathFor) and the state file (statePathFor).
// - Nothing changed since the last build (same files, sizes and times): one walk of the folders.
// - Otherwise the old index is checked against the listing; it is written again only when a note
//   was added, changed or removed, and only those notes are read.
// `progress(done, total)` may be null. It is called with total 0 every few files while checking,
// so a device can yield, and with total > 0 once the index is being written.
bool build(const std::string& root, const std::string& indexPath, const IndexFs& fs, BuildStats* stats = nullptr,
           const std::function<void(size_t done, size_t total)>& progress = nullptr);

struct Hit {
  std::string path;
  std::string title;
  int score = 0;
  // The first heading (or block id, "^id") that matches every word of the query, or "": search
  // opens the note there.
  std::string heading;
};
// Notes that link to the note at `notePath` in the vault at `root`: by name, or by a path from the
// vault root or the linking note's folder. Case and accents are ignored. Sorted by title.
std::vector<Hit> backlinks(const std::string& indexPath, const std::string& root, const std::string& notePath,
                           const IndexFs& fs, size_t maxResults = 100);

// Notes matching every word of `query` (folded, prefix match), best first.
std::vector<Hit> search(const std::string& indexPath, std::string_view query, const IndexFs& fs,
                        size_t maxResults = 50);

}  // namespace vaultindex
