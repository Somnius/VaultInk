#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Changing notes on the device (Phase 4a): ticking tasks, new notes, rename, delete to the vault's
// .trash. Every change is written through a temp file and recorded in the vault's list of local
// changes (/.vaultink/<id>/changed.txt), which a LiveSync pull reads so it never overwrites them.
namespace vaultedit {

// The folder of the vault that holds `path`, or "" when it is in none.
std::string vaultDirOf(const std::string& path);

// Records `path` (a file in a vault) as changed on the device. False when the list couldn't be
// written (the change is then not made).
bool markChanged(const std::string& path);
// The vault's changed paths, relative to `vaultDir`, as a lookup for the LiveSync pull.
std::function<bool(const std::string& relPath)> changedLookup(const std::string& vaultDir);
// The vault's changed paths, relative to `vaultDir`, in the order they were changed.
std::vector<std::string> changedList(const std::string& vaultDir);
// Takes a path off the list (it was sent to LiveSync). False when the list couldn't be written.
bool unmarkChanged(const std::string& vaultDir, const std::string& relPath);

// After a power cut during a save: "name.md" missing but "name.md.tmp" there (complete) -> renamed
// back; both there (the tmp may be partial) -> the tmp is removed.
void recoverTmp(const std::string& path);

enum class Result : uint8_t { Ok, Changed, BadName, Exists, Failed };

// Flips the task a "task:" link points at. Changed: the note is not what was laid out any more.
Result toggleTask(const std::string& notePath, const std::string& href);

// The editor's load and save (Phase 4b). readNote fails above 256 KB. saveNote refuses (Changed)
// when the file is no longer the bytes the editor loaded (`loadedHash`/`loadedSize` from
// contentHash), e.g. LiveSync pulled a new version meanwhile.
bool readNote(const std::string& notePath, std::string& text);
uint32_t contentHash(const std::string& text);
Result saveNote(const std::string& notePath, const std::string& text, uint32_t loadedHash, size_t loadedSize);

// A title the SD card and Obsidian accept: not empty, at most 100 bytes, no \ / : * ? " < > |,
// no leading dot.
bool validTitle(const std::string& title);

// "<folder>/<title>.md" holding "# <title>"; `outPath` is the new note.
Result createNote(const std::string& folder, const std::string& title, std::string& outPath);
// Renames a note in its folder; its reading position moves along. Links to it: util/LinkUpdate.
Result renameNote(const std::string& notePath, const std::string& title, std::string& outPath);
// Moves a note to "<vault>/.trash/<path in the vault>", with a time suffix when that name is taken.
Result trashNote(const std::string& notePath);

}  // namespace vaultedit
