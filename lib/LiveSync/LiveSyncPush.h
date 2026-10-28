#pragma once

#include <cstdint>
#include <string>

#include "LiveSyncCrypto.h"
#include "LiveSyncEngine.h"

// Pushing one note edited on the device to Self-hosted LiveSync (Phase 4c, write format pinned in
// .work/livesync-write-spec.md). Chunks first (content-addressed, so a retry or a chunk the server
// already has is fine), then the note document on top of the revision the device last pulled.
// When the server's revision moved on since, nothing is written (Conflict): the next pull keeps
// the device's file and saves the server's text beside it as a conflict copy.
namespace livesync {

// What the ids need from the passphrase (made once at setup; the passphrase isn't kept).
struct PushIdentity {
  std::string hashedPass;  // hashedPassphrase()
  std::string passDigest;  // passphraseDigest()
};

struct PushItem {
  std::string path;      // vault-relative
  bool deleted = false;  // the note is gone on the device
  std::string content;   // the note's bytes (when not deleted)
  std::string knownId;   // the doc id from the last pull ("" for a note made on the device)
  std::string knownRev;  // the _rev last pulled ("" when none)
};

enum class PushResult : uint8_t {
  Pushed,    // the server has the device's version (newId / newRev filled)
  Conflict,  // the server changed since the last pull: not written
  Skipped,   // nothing to do (deleted on both sides), or a path whose id can't be made here
  Failed,    // network or server error (`error` says which)
};

// Pieces of at most this many bytes, cut on UTF-8 boundaries (the plugin's chunks are <= 1024).
constexpr size_t MAX_PIECE_BYTES = 1000;

PushResult pushNote(const Transport& net, const Key master, const PushIdentity& identity, const RandomFill& random,
                    const PushItem& item, int64_t nowMs, std::string& newId, std::string& newRev, std::string& error);

}  // namespace livesync
