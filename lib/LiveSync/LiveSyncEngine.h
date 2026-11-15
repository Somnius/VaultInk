#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "LiveSyncCrypto.h"
#include "LiveSyncPull.h"

// One pull of a LiveSync database into a vault folder. The network and the SD card come in through
// two small interfaces, so the whole sync runs in host tests against a recorded database.
//
// Flow (see SPECS.md 7): version doc and sync parameters checked first; then `_changes` in pages
// of ids only (deletes are never filtered away); the page's file docs by `_all_docs?keys=`; each
// file's chunks in batches sized so one answer stays near maxResponseBytes, each batch written out
// before the next is fetched, through a temp file; the id->path map and the checkpoint are saved
// only after the whole page is applied, so a failure retries the same page.
//
// A file that cannot be fetched or decoded does not stop the sync: it goes to stateDir/skipped.txt
// ("path<TAB>reason<TAB>id" per line) and is tried again at the start of the next sync. A batch of
// chunks whose answer is too big for the device is asked for again in halves, down to one chunk a
// request, before its file is left out. The sync stops only when the server itself cannot be used
// (no answer, a refused login, more than maxFailuresInARow requests failing one after the other),
// when the skip list is full, or when the SD card cannot be written.
namespace livesync {

// Transport results below zero that are about one answer, not about the connection.
constexpr int STATUS_TOO_BIG = -2;    // the answer passed the transport's size cap
constexpr int STATUS_NO_MEMORY = -3;  // no room for the answer

// What the transport saw of the last answer (for the error log; never a URL or a header sent).
struct ResponseInfo {
  std::string contentType;
  int64_t contentLength = -1;  // -1: no Content-Length header
  bool chunked = false;        // Transfer-Encoding: chunked
  size_t received = 0;         // body bytes read
  bool complete = true;        // false: the body ended early (socket closed, a read timed out)
};

struct Transport {
  // `path` is relative to the database ("_changes?since=..."); returns the HTTP status (<0 on a
  // transport error) and fills `response` with the body.
  std::function<int(const char* method, const std::string& path, const std::string& body, std::string& response)>
      request;
  // The last answer's details; may be empty (tests, other adapters).
  std::function<ResponseInfo()> info;
};

struct FileStore {
  std::function<bool(const std::string& path)> exists;
  std::function<bool(const std::string& path, std::string& out)> readAll;
  // Replaces `path` with `text` (through a temp file).
  std::function<bool(const std::string& path, const std::string& text)> writeAll;
  // Streams a new file: begin, append..., commit(true) renames it over `path`; commit(false) drops it.
  std::function<bool(const std::string& path)> begin;
  std::function<bool(const char* bytes, size_t len)> append;
  std::function<bool(bool keep)> commit;
  // Moves a file (making folders); used for the trash.
  std::function<bool(const std::string& from, const std::string& to)> move;
};

struct SyncOptions {
  std::string vaultDir;                         // "/Vaults/Name": files land here
  std::string stateDir;                         // "/.vaultink/<id>/livesync": checkpoint, id->path map, trash
  std::string saltB64;                          // the pbkdf2salt the master key was derived with
  int changesPage = 100;                        // `_changes` rows per request
  int docsBatch = 10;                           // file docs per `_all_docs`
  int chunkBatch = 8;                           // chunks per `_all_docs`, at most
  uint64_t maxFileBytes = 32ull * 1024 * 1024;  // bigger files are skipped (listed, not written)
  // A chunk batch is sized so its answer should stay under this: the file's average chunk
  // (size / chunk count) with the base64 and encryption overhead, doubled for uneven chunks.
  size_t maxResponseBytes = 1536 * 1024;
  // Files kept in skipped.txt. One more than this stops the sync with a message, before the
  // checkpoint moves past a file that could not be listed (it would never be asked for again).
  size_t maxSkipList = 2000;
  // Document requests that may fail one after the other, with none answered in between, while the
  // server still answers a small GET (a proxy that refuses POST, say). One more stops the sync
  // rather than skipping every file and reporting success. Answers too big for the device don't count.
  int maxFailuresInARow = 20;
  // Paths (under vaultDir) changed on the device and not pushed yet. A remote change to one is
  // written beside it as "<name> (conflict <conflictDate>).<ext>", and a remote delete or rename
  // leaves it where it is: local edits are never overwritten.
  std::function<bool(const std::string& path)> locallyChanged;
  std::string conflictDate;  // "2026-10-27"
  // Told each conflict copy written (a vault-relative path), so the device can push it too.
  std::function<void(const std::string& path)> onConflictCopy;
  // One line per answer that could not be used and per skipped file (the device logs it as an error).
  std::function<void(const std::string& line)> logError;
};

// The skip list's name in stateDir.
constexpr const char* SKIPPED_FILE = "skipped.txt";

// The log line for an answer that could not be used: the request kind (version, parameters,
// changes, docs, chunks), status, Content-Type, length, bytes read, whether the body was whole,
// the JSON error if any, and with `showBody` the first 80 bytes, non-printable bytes escaped.
std::string describeResponse(const char* kind, int status, const ResponseInfo& info, const std::string& body,
                             const char* jsonError, bool showBody);

// How many of a file's chunks to ask for in one request (1..chunkBatch), see maxResponseBytes.
size_t chunksPerRequest(const FileEntry& file, const SyncOptions& options);

// The sync state files in stateDir: files.tsv (id -> path) and revs.tsv (id -> last pulled _rev).
std::map<std::string, std::string> loadMap(const FileStore& fs, const std::string& file);
bool saveMap(const FileStore& fs, const std::string& file, const std::map<std::string, std::string>& map);

// "Folder/Note.md" -> "Folder/Note (conflict 2026-10-27).md".
std::string conflictPath(const std::string& path, const std::string& date);

struct SyncStats {
  int written = 0;
  int trashed = 0;
  int skipped = 0;    // files left out by this sync (too big, not fetched, not decoded): see skipped.txt
  int conflicts = 0;  // remote changes to notes changed on the device: kept beside them
  int pages = 0;
};

// Progress: files handled so far in this sync, and the current file's path.
using SyncProgress = std::function<void(int done, const std::string& path)>;

// Pulls everything after the saved checkpoint. False with a one-line reason in `error` (shown to
// the user as is) when the sync stopped; the checkpoint then stays where it was.
bool syncOnce(const Transport& net, const FileStore& fs, const Key master, const SyncOptions& options, SyncStats& stats,
              std::string& error, const SyncProgress& progress = nullptr);

// A path from the database that is safe to write under the vault folder (no "..", no leading
// '/', no characters FAT can't store).
bool safeVaultPath(const std::string& path);

}  // namespace livesync
