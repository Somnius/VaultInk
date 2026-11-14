#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

// Remembers, while a screen is open, the things that failed, so they are not tried again on every
// paint. The Gallery keeps here the thumbnails it could not decode (keyed by path, file size and
// box): such a picture gets its named frame at once instead of a decode and a second screen refresh
// each time the page is drawn. Nothing is written to the card.
class FailedKeys {
 public:
  // The oldest entry goes when this many are held; a forgotten one is simply tried once more.
  static constexpr size_t CAPACITY = 64;

  bool contains(const std::string& key) const { return std::find(keys.begin(), keys.end(), key) != keys.end(); }

  void add(const std::string& key) {
    if (contains(key)) return;
    if (keys.size() >= CAPACITY) keys.erase(keys.begin());
    keys.push_back(key);
  }

  void clear() { keys.clear(); }
  size_t size() const { return keys.size(); }

 private:
  std::vector<std::string> keys;
};
