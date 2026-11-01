#pragma once

#include <array>
#include <cstdint>

// The order of the Library's tabs (0.61.0, kos25k #18), stored as one uint32: slot i in bits 4i..4i+3 holds
// tab id + 1, 0 ends the list; 0 as a whole is the default order. Tab ids are the Library's own (Recent 0,
// Title 1, Author 2, Favourites 3, Folders 4) and never change, so a stored order keeps its meaning.
namespace libtabs {

inline constexpr int COUNT = 5;
using Order = std::array<uint8_t, COUNT>;

inline constexpr Order DEFAULT_ORDER = {0, 1, 2, 3, 4};

// The stored order, repaired: unknown ids and repeats are dropped, tabs it lacks are appended in default order.
inline Order decode(const uint32_t packed) {
  if (packed == 0) return DEFAULT_ORDER;
  Order out{};
  bool seen[COUNT] = {};
  int n = 0;
  for (int slot = 0; slot < COUNT; ++slot) {
    const int value = static_cast<int>((packed >> (4 * slot)) & 0xFu);
    if (value == 0) break;
    const int id = value - 1;
    if (id >= COUNT || seen[id]) continue;
    seen[id] = true;
    out[n++] = static_cast<uint8_t>(id);
  }
  for (const uint8_t id : DEFAULT_ORDER) {
    if (!seen[id]) out[n++] = id;
  }
  return out;
}

inline uint32_t encode(const Order& order) {
  if (order == DEFAULT_ORDER) return 0;
  uint32_t packed = 0;
  for (int slot = 0; slot < COUNT; ++slot) packed |= static_cast<uint32_t>(order[slot] + 1) << (4 * slot);
  return packed;
}

// Moves the tab at `from` to position `to`, shifting the ones between.
inline void move(Order& order, const int from, const int to) {
  if (from < 0 || from >= COUNT || to < 0 || to >= COUNT || from == to) return;
  const uint8_t id = order[from];
  if (from < to) {
    for (int i = from; i < to; ++i) order[i] = order[i + 1];
  } else {
    for (int i = from; i > to; --i) order[i] = order[i - 1];
  }
  order[to] = id;
}

// Where tab `id` sits in `order`.
inline int positionOf(const Order& order, const uint8_t id) {
  for (int i = 0; i < COUNT; ++i) {
    if (order[i] == id) return i;
  }
  return 0;
}

}  // namespace libtabs
