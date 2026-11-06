#include "FileSearch.h"

#include <Utf8.h>

#include <algorithm>

#include "TextFold.h"

namespace filesearch {

bool skipped(const std::string& name, const bool showHidden) {
  if (name.empty() || name == "." || name == "..") return true;
  if (!showHidden && name[0] == '.') return true;
  return name == "System Volume Information";
}

std::string foldName(const std::string& name) { return textfold::fold(utf8ComposeNfc(name)); }

Walker::Walker(DirectorySource& source, const std::string& root, const std::string& query, const bool showHidden)
    : source(source), folded(foldName(query)), showHidden(showHidden) {
  if (folded.empty()) return;  // an empty query would list the whole card
  std::string start = root.empty() ? "/" : root;
  if (start.size() > 1 && start.back() == '/') start.pop_back();
  pending.push_back({std::move(start), 0});
}

bool Walker::step() {
  if (done()) return false;
  Pending current = std::move(pending.back());
  pending.pop_back();
  entries.clear();
  const bool read = source.list(current.path, entries);
  ++folders;
  if (!read) return !done();
  // Sub folders are pushed in reverse so the stack pops them in listing order.
  std::vector<std::string> subFolders;
  const std::string prefix = current.path == "/" ? current.path : current.path + "/";
  for (const Entry& entry : entries) {
    if (skipped(entry.name, showHidden)) continue;
    if (entry.isDirectory && current.depth < MAX_DEPTH) subFolders.push_back(entry.name);
    if (!entry.isDirectory && fileFilter && !fileFilter(entry.name)) continue;
    if (foldName(entry.name).find(folded) != std::string::npos) {
      if (hits.size() < MAX_HITS) {
        hits.push_back({entry.name, current.path, entry.isDirectory});
      } else {
        cut = true;  // a match that does not fit
      }
    }
  }
  std::for_each(subFolders.rbegin(), subFolders.rend(), [&](const std::string& name) {
    if (pending.size() < MAX_PENDING) {
      pending.push_back({prefix + name, current.depth + 1});
    } else {
      cut = true;  // a folder that is never read
    }
  });
  return !done();
}

void Walker::erase(const size_t index) {
  if (index < hits.size()) hits.erase(hits.begin() + static_cast<std::ptrdiff_t>(index));
}

}  // namespace filesearch
