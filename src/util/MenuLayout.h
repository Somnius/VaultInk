#pragma once

// The order and the hidden entries of the reader's toolbar panels (0.73.0, kos25k #15): the More panel and the
// Text panel. Platform-free so it is host-tested (test/menu_layout).
//
// An entry is a small id (0-31): a MenuAction for More, a row number for Text. The order is stored as a short
// string, one character per id ('0' + id), and the hidden entries as a bit mask. Both are repaired when read:
// unknown ids and repeats are dropped, entries the stored order lacks (added by a later version, or conditional
// ones such as Footnotes) keep their default place after the ones it names, and a hidden entry that is locked
// shows anyway.

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace menulayout {

inline constexpr int MAX_ID = 32;
inline constexpr char FIRST_CHAR = '0';

inline constexpr uint32_t maskOf(const int id) { return id >= 0 && id < MAX_ID ? (1u << id) : 0u; }

// `defaults` are the entries that exist right now, in their default order. Returns them in the stored order with
// the hidden ones removed. A panel never comes back empty: if every entry were hidden, the defaults are shown.
inline std::vector<int> arrange(const std::vector<int>& defaults, const std::string& order, const uint32_t hidden,
                                const uint32_t locked) {
  const auto exists = [&defaults](const int id) {
    return std::find(defaults.begin(), defaults.end(), id) != defaults.end();
  };
  std::vector<int> result;
  result.reserve(defaults.size());
  uint32_t placed = 0;
  for (const char c : order) {
    const int id = c - FIRST_CHAR;
    if (id < 0 || id >= MAX_ID || (placed & maskOf(id)) != 0 || !exists(id)) continue;
    placed |= maskOf(id);
    result.push_back(id);
  }
  for (const int id : defaults) {
    if (id < 0 || id >= MAX_ID || (placed & maskOf(id)) != 0) continue;
    placed |= maskOf(id);
    result.push_back(id);
  }
  std::vector<int> shown;
  shown.reserve(result.size());
  std::copy_if(result.begin(), result.end(), std::back_inserter(shown),
               [&](const int id) { return (hidden & maskOf(id)) == 0 || (locked & maskOf(id)) != 0; });
  return shown.empty() ? defaults : shown;
}

// The full order for the editor: every id in `defaults`, hidden ones included, in the stored order.
inline std::vector<int> fullOrder(const std::vector<int>& defaults, const std::string& order) {
  return arrange(defaults, order, 0, 0);
}

inline std::string encode(const std::vector<int>& ids) {
  std::string out;
  for (const int id : ids) {
    if (id >= 0 && id < MAX_ID) out += static_cast<char>(FIRST_CHAR + id);
  }
  return out;
}

// Moves the entry at `from` to `to` (both positions in `ids`), the others keep their order.
inline void move(std::vector<int>& ids, const int from, const int to) {
  if (from == to || from < 0 || to < 0 || from >= static_cast<int>(ids.size()) || to >= static_cast<int>(ids.size())) {
    return;
  }
  const int id = ids[static_cast<size_t>(from)];
  ids.erase(ids.begin() + from);
  ids.insert(ids.begin() + to, id);
}

}  // namespace menulayout
