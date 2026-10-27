#pragma once

#include <functional>
#include <string>
#include <string_view>

// Resolves "vault:" links written by MarkdownToHtml to files, the way Obsidian does. File access
// goes through two callbacks so this builds and tests on the host.
namespace vaultlink {

struct Target {
  std::string note;    // "Folder/Note", "Note" or "pic.png", URL-decoded, no "vault:"
  std::string anchor;  // heading text or "^block", without '#'; empty for none
};

// "vault:Note#Heading" -> {"Note", "Heading"}. Returns false for anything not starting with "vault:".
bool parse(std::string_view href, Target& out);

// "Note" -> "Note.md"; names that already have an extension are kept.
std::string fileNameFor(std::string_view note);

// Case-insensitive name compare (ASCII letters; other scripts compare exactly).
bool sameName(std::string_view a, std::string_view b);

struct Fs {
  std::function<bool(const std::string& path)> exists;
  // Calls `entry` for each child of `dir` with its name and whether it is a folder.
  std::function<void(const std::string& dir, const std::function<void(const std::string& name, bool isDir)>& entry)>
      list;
  // Optional: streams every file path the vault's index knows (VaultIndex's file list) to `path`.
  // Returns false when the vault at `root` has no list yet; resolve() then scans its folders.
  std::function<bool(const std::string& root, const std::function<void(std::string_view path)>& path)> known;
};

// The folder above `notePath` that holds ".obsidian", or the note's own folder if none does.
std::string vaultRoot(const std::string& notePath, const Fs& fs);

// Full path of the file `target` points to, or "" if it doesn't exist. A target with a '/' is a
// path from the vault root (or, failing that, from the current note's folder). A bare name is
// looked for in the current note's folder first, then everywhere in the vault, shortest path
// first. Dot folders (.obsidian, .trash) are skipped; at most `maxEntries` entries are looked at.
// With `fs.known`, a bare name is looked up in the index's file list first; the scan runs only when
// the list has no match that still exists (no list yet, or a file added since it was written).
std::string resolve(const std::string& root, const std::string& currentDir, const Target& target, const Fs& fs,
                    size_t maxEntries = 20000);

}  // namespace vaultlink
