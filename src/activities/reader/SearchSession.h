#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Search in a book as the reader shows it (kos25k #24): the hits stay on, the page highlights the
// words, and a bar at the bottom steps through them or goes back to where the reader was. While it
// is active nothing about the reading position is saved, logged or synced.
struct SearchSession {
  struct Hit {
    int spine = 0;
    uint32_t offset = 0;  // visible text offset of the match
  };

  bool active = false;
  std::string query;  // prepared (folded) query, for the highlight
  std::vector<Hit> hits;
  int current = -1;
  // Where the reader was before the first jump.
  int originSpine = 0;
  int originPage = 0;
  std::optional<uint32_t> originOffset;

  // The last finished search of this book (kept while the reader is open): typing the same words again
  // shows it at once instead of reading the chapters again.
  struct Found {
    int spine = 0;
    uint32_t offset = 0;
    std::string label;
    std::string detail;
  };
  std::string lastKey;
  std::vector<Found> lastFound;

  void clear() {
    active = false;
    query.clear();
    hits.clear();
    hits.shrink_to_fit();
    current = -1;
  }
};
