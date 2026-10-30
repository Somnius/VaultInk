#include "VaultNavigation.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MarkdownToHtml.h>
#include <VaultLink.h>
#include <VaultSdFs.h>

#include <vector>

#include "activities/ActivityManager.h"

namespace vaultnav {
namespace {

constexpr size_t MAX_HISTORY = 16;

// Notes opened by following links, oldest first. Loop task only.
std::vector<std::string> history;
std::string pendingAnchor;
// The reader being opened by follow()/goBack(): opening it must not clear the history.
std::string openingPath;

vaultlink::Fs sdFs() { return vaultsd::fs(); }

std::string folderOf(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
}

void open(const std::string& path) {
  openingPath = path;
  activityManager.goToReader(path);
}

void pushHistory(const std::string& from) {
  if (history.size() == MAX_HISTORY) history.erase(history.begin());
  history.push_back(from);
}

}  // namespace

void openFrom(const std::string& currentPath, const std::string& path) {
  pushHistory(currentPath);
  pendingAnchor.clear();
  open(path);
}

void openAtHeading(const std::string& path, const std::string& heading) {
  history.clear();
  // A block id is its own anchor; a heading goes by MarkdownToHtml's id.
  pendingAnchor = heading.empty() || heading.front() == '^' ? heading : MarkdownToHtml::headingId(heading);
  open(path);
}

Follow follow(const std::string& currentPath, const std::string& href, std::string* sameNoteAnchor) {
  vaultlink::Target target;
  if (!vaultlink::parse(href, target)) return Follow::NotVaultLink;
  const auto fs = sdFs();
  const std::string root = vaultlink::vaultRoot(currentPath, fs);
  const std::string path = vaultlink::resolve(root, folderOf(currentPath), target, fs);
  if (path.empty()) {
    LOG_DBG("VLT", "Link not found: %s", href.c_str());
    return Follow::NotFound;
  }
  const std::string anchor =
      target.anchor.empty() || target.anchor.front() == '^' ? target.anchor : MarkdownToHtml::headingId(target.anchor);
  if (path == currentPath) {
    if (sameNoteAnchor) *sameNoteAnchor = anchor;
    return Follow::SameNote;
  }
  pushHistory(currentPath);
  // Headings are linked by their text; the note's anchors use MarkdownToHtml's ids.
  pendingAnchor = anchor;
  LOG_DBG("VLT", "Following %s -> %s", href.c_str(), path.c_str());
  open(path);
  return Follow::Opened;
}

bool goBack() {
  if (history.empty()) return false;
  const std::string previous = history.back();
  history.pop_back();
  pendingAnchor.clear();
  open(previous);
  return true;
}

std::string takeAnchor() {
  std::string anchor;
  anchor.swap(pendingAnchor);
  return anchor;
}

void opened(const std::string& path) {
  if (path != openingPath) {
    history.clear();
    pendingAnchor.clear();
  }
  openingPath.clear();
}

}  // namespace vaultnav
