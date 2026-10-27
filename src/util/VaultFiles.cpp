#include "VaultFiles.h"

#include <HalStorage.h>
#include <Logging.h>
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
  auto scan = [&](const char* dir, const bool needObsidian) {
    HalFile root = Storage.open(dir);
    if (!root || !root.isDirectory()) return;
    for (auto file = root.openNextFile(); file && vaults.size() < MAX_VAULTS; file = root.openNextFile()) {
      if (!file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      if (name[0] == '.' || std::strcmp(name, "System Volume Information") == 0) continue;
      const std::string path = std::string(std::strcmp(dir, "/") == 0 ? "" : dir) + "/" + name;
      if (path == VAULTS_ROOT) continue;
      if (needObsidian && !Storage.exists((path + "/.obsidian").c_str())) continue;
      vaults.push_back({name, path});
    }
  };
  scan(VAULTS_ROOT, false);
  scan("/", true);
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
}  // namespace

void rememberLastNote(const std::string& notePath) {
  const auto vaults = findVaults();
  const Vault* vault = vaultOf(vaults, notePath);
  if (!vault) return;
  const std::string file = lastNotePathFor(vault->path);
  Storage.ensureDirectoryExists(file.substr(0, file.find_last_of('/')).c_str());
  if (!Storage.writeFile(file.c_str(), String(notePath.c_str()))) LOG_ERR("VLT", "Could not save last note");
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
