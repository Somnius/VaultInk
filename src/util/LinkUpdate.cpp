#include "LinkUpdate.h"

#include <HalStorage.h>
#include <LinkRewrite.h>
#include <Logging.h>
#include <VaultIndex.h>
#include <VaultLink.h>
#include <VaultSdFs.h>

#include <algorithm>

#include "util/VaultEdit.h"
#include "util/VaultFiles.h"

namespace linkupdate {
namespace {

constexpr size_t MAX_LINKING_NOTES = 500;

std::string folderOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
}

}  // namespace

Plan plan(const std::string& oldPath) {
  Plan result;
  const auto vaults = vaultfiles::findVaults();
  const auto vault = std::find_if(vaults.begin(), vaults.end(), [&oldPath](const vaultfiles::Vault& v) {
    return oldPath.rfind(v.path + "/", 0) == 0;
  });
  if (vault == vaults.end()) return result;
  const std::string indexPath = vaultfiles::indexPathFor(vault->path);
  // Brought up to date first (only changed notes are read again), so new links count too.
  const auto fs = vaultsd::fs();
  if (!vaultindex::build(vault->path, indexPath, vaultfiles::sdFs()) && !Storage.exists(indexPath.c_str())) {
    return result;
  }
  result.indexed = true;
  for (const auto& hit : vaultindex::backlinks(indexPath, vault->path, oldPath, fs, MAX_LINKING_NOTES)) {
    std::string text;
    if (!vaultedit::readNote(hit.path, text)) {
      ++result.skipped;
      continue;
    }
    Plan::Note note{hit.path, {}};
    const std::string folder = folderOf(hit.path);
    // A dry run: count the links and keep the targets that really resolve to the old file.
    const size_t links = linkrewrite::rewrite(
        text,
        [&](std::string_view target) {
          if (std::find(note.targets.begin(), note.targets.end(), target) != note.targets.end()) return true;
          const vaultlink::Target t{std::string(target), ""};
          if (vaultlink::resolve(vault->path, folder, t, fs) != oldPath) return false;
          note.targets.emplace_back(target);
          return true;
        },
        "x");
    if (links == 0) continue;
    result.links += links;
    result.notes.push_back(std::move(note));
  }
  LOG_DBG("LNK", "Rename of %s: %u links in %u notes (%u skipped)", oldPath.c_str(),
          static_cast<unsigned>(result.links), static_cast<unsigned>(result.notes.size()),
          static_cast<unsigned>(result.skipped));
  return result;
}

Outcome apply(const Plan& plan, const std::string& oldPath, const std::string& newPath, const std::string& newTitle) {
  Outcome outcome;
  for (const auto& note : plan.notes) {
    const std::string& path = note.path == oldPath ? newPath : note.path;  // the note may link to itself
    std::string text;
    if (!vaultedit::readNote(path, text)) {
      ++outcome.failed;
      continue;
    }
    const uint32_t hash = vaultedit::contentHash(text);
    const size_t size = text.size();
    const size_t links = linkrewrite::rewrite(
        text,
        [&note](std::string_view target) {
          return std::find(note.targets.begin(), note.targets.end(), target) != note.targets.end();
        },
        newTitle);
    if (links == 0) continue;
    if (vaultedit::saveNote(path, text, hash, size) != vaultedit::Result::Ok) {
      LOG_ERR("LNK", "Couldn't update links in %s", path.c_str());
      ++outcome.failed;
      continue;
    }
    outcome.links += links;
    ++outcome.notes;
  }
  return outcome;
}

}  // namespace linkupdate
