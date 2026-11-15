#pragma once

#include <VaultIndex.h>

#include <string>
#include <vector>

// The vaults on the SD card and SD-backed file access for lib/Vault.
namespace vaultfiles {

constexpr const char* VAULTS_ROOT = "/Vaults";
constexpr size_t MAX_VAULTS = 32;

struct Vault {
  std::string name;
  std::string path;
};

// Every folder in /Vaults (at most MAX_VAULTS), sorted by name. A folder anywhere else is not a
// vault, also when it holds .obsidian: its files are ordinary files (vaultpaths::isVault).
std::vector<Vault> findVaults();

// The search index of a vault: /.vaultink/<id>/index.tsv, outside the vault.
std::string indexPathFor(const std::string& vaultPath);

// The vault a note belongs to (the one whose folder holds it), or nullptr.
const Vault* vaultOf(const std::vector<Vault>& vaults, const std::string& notePath);

// Each vault remembers the note read last (/.vaultink/<id>/last.txt next to its index).
void rememberLastNote(const std::string& notePath);
// The vault's last note if it still exists, else "".
std::string lastNote(const std::string& vaultPath);
// The notes read last in any vault, newest first (/.vaultink/recent-notes.txt, kept by
// rememberLastNote): vault notes are not in the books' recent list. Only notes that still exist.
constexpr size_t MAX_RECENT_NOTES = 5;
std::vector<std::string> recentNotes();

// Deletes every vault's search index (the next search rebuilds them). Returns how many.
int clearSearchIndexes();

// lib/Vault's file callbacks on the SD card.
vaultindex::IndexFs sdFs();

}  // namespace vaultfiles
