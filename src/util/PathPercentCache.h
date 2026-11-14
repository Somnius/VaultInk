#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Read percents by book path, kept while a list is on screen. The Library draws a percent on every
// row of the pinned recents and the favourites; without this each paint loaded every visible book's
// metadata again. With `defer` set a book not yet known is not loaded: the caller gets -1, `skipped`
// is raised and a later paint fills it in, so the list can be on screen before the percents.
class PathPercentCache {
 public:
  // Holds this many books; the oldest goes first. A screen shows about ten rows.
  static constexpr size_t CAPACITY = 48;

  template <typename Load>
  int get(const std::string& path, const bool defer, bool& skipped, Load&& load) {
    const auto it = std::find_if(entries.begin(), entries.end(), [&path](const auto& e) { return e.first == path; });
    if (it != entries.end()) return it->second;
    if (defer) {
      skipped = true;
      return -1;
    }
    const int percent = std::clamp(static_cast<int>(load(path)), -1, 100);
    if (entries.size() >= CAPACITY) entries.erase(entries.begin());
    entries.emplace_back(path, static_cast<int8_t>(percent));
    return percent;
  }

  void clear() { entries.clear(); }
  size_t size() const { return entries.size(); }

 private:
  std::vector<std::pair<std::string, int8_t>> entries;
};
