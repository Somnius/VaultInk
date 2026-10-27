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
// file's chunks in small batches, written through a temp file; the id->path map and the checkpoint
// are saved only after the whole page is applied, so a failure retries the same page.
namespace livesync {

struct Transport {
  // `path` is relative to the database ("_changes?since=..."); returns the HTTP status (<0 on a
  // transport error) and fills `response` with the body.
  std::function<int(const char* method, const std::string& path, const std::string& body, std::string& response)>
      request;
};

struct Storage {
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
  std::string vaultDir;   // "/Vaults/Name": files land here
  std::string stateDir;   // "/.vaultink/<id>/livesync": checkpoint, id->path map, trash
  std::string saltB64;    // the pbkdf2salt the master key was derived with
  int changesPage = 100;  // `_changes` rows per request
  int docsBatch = 10;     // file docs per `_all_docs`
  int chunkBatch = 8;     // chunks per `_all_docs`
  uint64_t maxFileBytes = 32ull * 1024 * 1024;  // bigger files are skipped (counted, not written)
};

struct SyncStats {
  int written = 0;
  int trashed = 0;
  int skipped = 0;  // too big
  int pages = 0;
};

// Progress: files handled so far in this sync, and the current file's path.
using SyncProgress = std::function<void(int done, const std::string& path)>;

// Pulls everything after the saved checkpoint. False with a one-line reason in `error` (shown to
// the user as is) when the sync stopped; the checkpoint then stays where it was.
bool syncOnce(const Transport& net, const Storage& fs, const Key master, const SyncOptions& options, SyncStats& stats,
              std::string& error, const SyncProgress& progress = nullptr);

// A path from the database that is safe to write under the vault folder (no "..", no leading
// '/', no characters FAT can't store).
bool safeVaultPath(const std::string& path);

}  // namespace livesync
