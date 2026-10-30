#include "ClippingExport.h"

#include <HalStorage.h>
#include <HighlightsNote.h>
#include <Logging.h>
#include <VaultIndex.h>

#include <algorithm>

#include "ClippingStore.h"
#include "util/VaultEdit.h"
#include "util/VaultFiles.h"

namespace clippingexport {
namespace {

// The vault whose "last note" file changed most recently: the one being used.
const vaultfiles::Vault* mostRecentVault(const std::vector<vaultfiles::Vault>& vaults) {
  const vaultfiles::Vault* best = nullptr;
  uint64_t bestStamp = 0;
  const auto fs = vaultfiles::sdFs();
  for (const auto& v : vaults) {
    const std::string index = vaultfiles::indexPathFor(v.path);
    const uint64_t stamp = fs.stamp(index.substr(0, index.find_last_of('/')) + "/last.txt") >> 32;
    if (!best || stamp > bestStamp) {
      best = &v;
      bestStamp = stamp;
    }
  }
  return best;
}

}  // namespace

Status exportToVault(std::string& notePath) {
  const size_t count = CLIPPINGS.clippingCount();
  if (count == 0) return Status::NoClippings;
  const auto vaults = vaultfiles::findVaults();
  const vaultfiles::Vault* vault = mostRecentVault(vaults);
  if (!vault) return Status::NoVault;

  // In reading order: by chapter, then where in it.
  std::vector<size_t> order(count);
  for (size_t i = 0; i < count; ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [](size_t a, size_t b) {
    const Clipping* x = CLIPPINGS.clippingAt(a);
    const Clipping* y = CLIPPINGS.clippingAt(b);
    if (x->spineIndex != y->spineIndex) return x->spineIndex < y->spineIndex;
    if (x->startOffset != y->startOffset) return x->startOffset < y->startOffset;
    return x->startPage < y->startPage;
  });
  std::vector<highlightsnote::Highlight> highlights;
  for (const size_t index : order) {
    std::string text;
    if (!CLIPPINGS.readClippingText(index, text)) continue;
    highlights.push_back({CLIPPINGS.clippingAt(index)->chapterTitle, std::move(text)});
  }
  if (highlights.empty()) return Status::NoClippings;

  const std::string folder = vault->path + "/Highlights";
  Storage.ensureDirectoryExists(folder.c_str());
  const std::string name = highlightsnote::fileNameFor(CLIPPINGS.title());
  std::string path = folder + "/" + name + ".md";
  std::string existing;
  if (Storage.exists(path.c_str()) && (!vaultedit::readNote(path, existing) || !highlightsnote::isExport(existing))) {
    path = folder + "/" + name + " (highlights).md";
  }
  const std::string text = highlightsnote::build(CLIPPINGS.title(), CLIPPINGS.author(), highlights);
  const std::string tmp = path + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("EXP", tmp, file)) return Status::SdError;
  const bool written = file.write(text.data(), text.size()) == text.size();
  file.flush();
  file.close();
  if (!written || !Storage.replaceFile(tmp.c_str(), path.c_str())) {
    Storage.remove(tmp.c_str());
    return Status::SdError;
  }
  vaultedit::markChanged(path);
  notePath = path;
  LOG_INF("EXP", "%u highlights -> %s", static_cast<unsigned>(highlights.size()), path.c_str());
  return Status::Ok;
}

}  // namespace clippingexport
