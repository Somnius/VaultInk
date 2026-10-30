#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Updating links when a note is renamed (Phase 4). Planned before the rename, while the links
// still resolve to the old file, and applied after it, so a note with the same name in another
// folder is never touched. Wiki-links only (lib/Vault/LinkRewrite.h).
namespace linkupdate {

struct Plan {
  struct Note {
    std::string path;
    std::vector<std::string> targets;  // link targets as written that lead to the renamed note
  };
  std::vector<Note> notes;
  size_t links = 0;
  size_t skipped = 0;    // linking notes that couldn't be read (over 256 KB, or a read error)
  bool indexed = false;  // the vault has a search index to find the links with
};

// The notes in the vault of `oldPath` that link to it, from the vault's search index.
Plan plan(const std::string& oldPath);

struct Outcome {
  size_t links = 0;
  size_t notes = 0;
  size_t failed = 0;  // notes that changed meanwhile or couldn't be saved; left as they were
};

// After the rename to `newPath`: points the planned links at `newTitle`.
Outcome apply(const Plan& plan, const std::string& oldPath, const std::string& newPath, const std::string& newTitle);

}  // namespace linkupdate
