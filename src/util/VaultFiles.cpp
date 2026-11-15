#include "VaultFiles.h"

#include <HalStorage.h>
#include <Logging.h>
#include <VaultPaths.h>
#include <VaultSdFs.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace vaultfiles {
namespace {

constexpr size_t NAME_LEN = 256;

}  // namespace

std::vector<Vault> findVaults() {
  std::vector<Vault> vaults;
  char name[NAME_LEN];
  HalFile root = Storage.open(VAULTS_ROOT);
  if (root && root.isDirectory()) {
    for (auto file = root.openNextFile(); file && vaults.size() < MAX_VAULTS; file = root.openNextFile()) {
      if (!file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      std::string path = std::string(VAULTS_ROOT) + "/" + name;
      if (!vaultpaths::isVault(path)) continue;
      vaults.push_back({name, std::move(path)});
    }
  }
  std::sort(vaults.begin(), vaults.end(), [](const Vault& a, const Vault& b) { return a.name < b.name; });
  return vaults;
}

std::string indexPathFor(const std::string& vaultPath) { return vaultindex::indexPathFor(vaultPath); }

const Vault* vaultOf(const std::vector<Vault>& vaults, const std::string& notePath) {
  // Longest match, so a vault nested in another vault gets its own entry.
  const Vault* best = nullptr;
  for (const auto& v : vaults) {
    if (notePath.size() > v.path.size() && notePath.compare(0, v.path.size(), v.path) == 0 &&
        notePath[v.path.size()] == '/' && (!best || v.path.size() > best->path.size())) {
      best = &v;
    }
  }
  return best;
}

namespace {
std::string lastNotePathFor(const std::string& vaultPath) {
  std::string path = indexPathFor(vaultPath);
  return path.substr(0, path.find_last_of('/')) + "/last.txt";
}

constexpr const char* RECENT_NOTES_FILE = "/.vaultink/recent-notes.txt";

std::vector<std::string> readRecentNotes() {
  std::vector<std::string> notes;
  std::string text;
  if (!Storage.exists(RECENT_NOTES_FILE) || !Storage.readFileToString("VLT", RECENT_NOTES_FILE, 4096, text)) {
    return notes;
  }
  for (size_t start = 0; start < text.size() && notes.size() < MAX_RECENT_NOTES;) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) notes.push_back(std::move(line));
    start = end + 1;
  }
  return notes;
}

bool isMarkdown(const std::string& path) {
  return path.size() > 3 &&
         (path.compare(path.size() - 3, 3, ".md") == 0 || path.compare(path.size() - 3, 3, ".MD") == 0);
}
}  // namespace

void rememberLastNote(const std::string& notePath) {
  const auto vaults = findVaults();
  const Vault* vault = vaultOf(vaults, notePath);
  if (!vault) return;
  const std::string file = lastNotePathFor(vault->path);
  Storage.ensureDirectoryExists(file.substr(0, file.find_last_of('/')).c_str());
  if (!Storage.writeFile(file.c_str(), String(notePath.c_str()))) LOG_ERR("VLT", "Could not save last note");
  if (!isMarkdown(notePath)) return;  // a canvas is the vault's last note, not a recent note
  auto notes = readRecentNotes();
  if (!notes.empty() && notes.front() == notePath) return;
  vaultpaths::pushRecent(notes, notePath, MAX_RECENT_NOTES);
  std::string text;
  for (const auto& note : notes) text += note + "\n";
  if (!Storage.writeFile(RECENT_NOTES_FILE, String(text.c_str()))) LOG_ERR("VLT", "Could not save recent notes");
}

std::vector<std::string> recentNotes() {
  auto notes = readRecentNotes();
  notes.erase(
      std::remove_if(notes.begin(), notes.end(), [](const std::string& note) { return !Storage.exists(note.c_str()); }),
      notes.end());
  return notes;
}

std::string lastNote(const std::string& vaultPath) {
  std::string path;
  if (!Storage.readFileToString("VLT", lastNotePathFor(vaultPath), 1024, path)) return {};
  while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
  return !path.empty() && Storage.exists(path.c_str()) ? path : std::string();
}

int clearSearchIndexes() {
  // Every /.vaultink/<id>/index.tsv, also of vaults since removed or renamed.
  std::vector<std::string> indexes;
  HalFile root = Storage.open("/.vaultink");
  if (!root || !root.isDirectory()) return 0;
  char name[NAME_LEN];
  for (auto dir = root.openNextFile(); dir; dir = root.openNextFile()) {
    if (!dir.isDirectory()) continue;
    dir.getName(name, sizeof(name));
    indexes.push_back(std::string("/.vaultink/") + name + "/index.tsv");
    indexes.push_back(std::string("/.vaultink/") + name + "/files.txt");
  }
  root.close();
  return static_cast<int>(std::count_if(indexes.begin(), indexes.end(), [](const std::string& index) {
    return Storage.exists(index.c_str()) && Storage.remove(index.c_str());
  }));
}

vaultindex::IndexFs sdFs() { return vaultsd::fs(); }

}  // namespace vaultfiles
