#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "LiveSyncCrypto.h"

// Read side of a LiveSync database: turns CouchDB documents into vault files. No networking here;
// the caller fetches docs (`_changes`, `_all_docs?keys=`) and hands them over, so this runs in
// host tests against a dump of a real database.
namespace livesync {

// The newest remote generation VaultInk reads (`obsydian_livesync_version`), and the features of
// generation 13 it understands.
constexpr int MAX_REMOTE_VERSION = 13;

enum class DocKind : uint8_t { File, Skip };

struct FileEntry {
  std::string id;    // CouchDB _id ("f:..." when the path is obfuscated)
  std::string path;  // vault-relative, as Obsidian has it
  int64_t mtime = 0;
  uint64_t size = 0;
  std::vector<std::string> children;  // chunk ids, in order
  bool deleted = false;
  bool binary = false;  // "newnote": every chunk is base64 of a byte slice
};

// Whether the remote's version doc is one VaultInk can read (version <= 13, known features only).
bool remoteVersionSupported(JsonObjectConst versionDoc, std::string* why = nullptr);

// One document from the database. Chunks, control docs, internal files (i:, ix:, ps:) and legacy
// types are Skip. An obfuscated path is decrypted with `master` (Result::Ok and kind File).
Result readFileDoc(JsonObjectConst doc, const Key master, FileEntry& out, DocKind& kind);

// A chunk's stored data by id: `data` as in the doc, `encrypted` from its `e_` flag. False if missing.
using ChunkGetter = std::function<bool(const std::string& id, std::string& data, bool& encrypted)>;
// Receives the file's bytes in order; false stops the assembly.
using ByteSink = std::function<bool(const char* bytes, size_t len)>;

// Writes the file's content to `sink`, chunk by chunk (nothing is held for the whole file).
// BadFormat for a missing chunk or compressed data (VaultInk reads compression off only).
Result assemble(const FileEntry& file, const Key master, const ChunkGetter& chunk, const ByteSink& sink);

}  // namespace livesync
