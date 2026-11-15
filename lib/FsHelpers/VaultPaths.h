#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// Which paths on the SD card belong to Obsidian vaults. Only folders in /Vaults are vaults; a
// vault's files are notes, not books: the Library, the recent list and the favourites leave
// them out. No file access here, so the rules are tested on the computer.
namespace vaultpaths {

constexpr std::string_view VAULTS_ROOT = "/Vaults";

// `path` is /Vaults itself or anything in it. The folder name is compared without case, like
// the card's file system does ("/vaults/V/a.md" is the same folder).
inline bool isUnderVaults(const std::string_view path) {
  if (path.size() < VAULTS_ROOT.size()) return false;
  for (size_t i = 0; i < VAULTS_ROOT.size(); ++i) {
    const char a = path[i];
    const char b = VAULTS_ROOT[i];
    const char lowerA = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
    const char lowerB = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
    if (lowerA != lowerB) return false;
  }
  return path.size() == VAULTS_ROOT.size() || path[VAULTS_ROOT.size()] == '/';
}

// A folder name that can be a vault: not hidden (.trash) and not a system folder.
inline bool isVaultFolderName(const std::string_view name) {
  return !name.empty() && name.front() != '.' && name != "System Volume Information";
}

// The folder at `folderPath` is a vault: it sits right in /Vaults. A folder anywhere else is
// not, also when it holds .obsidian.
inline bool isVault(const std::string_view folderPath) {
  if (!isUnderVaults(folderPath) || folderPath.size() <= VAULTS_ROOT.size() + 1) return false;
  const std::string_view name = folderPath.substr(VAULTS_ROOT.size() + 1);
  return name.find('/') == std::string_view::npos && isVaultFolderName(name);
}

// Takes the entries of a vault's files out of a list of books (recent reads, favourites): the
// ones an older build saved. `Entry` has a `path`. Returns true if any entry was removed.
template <typename Entry>
bool dropVaultEntries(std::vector<Entry>& list) {
  const size_t before = list.size();
  list.erase(std::remove_if(list.begin(), list.end(), [](const Entry& e) { return isUnderVaults(e.path); }),
             list.end());
  return list.size() != before;
}

// Puts `path` first in a newest-first list of at most `maxEntries`, without a second copy.
inline void pushRecent(std::vector<std::string>& list, const std::string& path, const size_t maxEntries) {
  list.erase(std::remove(list.begin(), list.end(), path), list.end());
  list.insert(list.begin(), path);
  if (list.size() > maxEntries) list.resize(maxEntries);
}

}  // namespace vaultpaths
