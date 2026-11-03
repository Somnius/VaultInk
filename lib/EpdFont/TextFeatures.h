#pragma once

#include <atomic>
#include <cstdint>

// The text features a reader can switch off or on under Settings > Reader > Experimental (kos25k #30). The reader's
// layout and the font code read them from here; the section cache keys on them (ReaderRenderSpec::textFeatures), so a
// book is laid out again when one changes. set() is called with the spec when a section is built or loaded.
namespace textfeatures {

enum : uint8_t {
  LIGATURES = 1,      // fi, fl, ff ... from the font (SD-card and TTF fonts)
  KERNING = 2,        // pair spacing from the font
  WIDOW_ORPHAN = 4,   // no single line alone at the top or bottom of a page
  KEEP_HEADINGS = 8,  // a heading stays with the paragraph after it
  HANGING = 16,       // opening quotes and closing punctuation hang into the margin
};

inline constexpr uint8_t DEFAULTS = LIGATURES | KERNING | WIDOW_ORPHAN | KEEP_HEADINGS;

inline std::atomic<uint8_t>& storage() {
  static std::atomic<uint8_t> value{DEFAULTS};
  return value;
}

inline void set(const uint8_t flags) { storage().store(flags, std::memory_order_relaxed); }
inline bool has(const uint8_t flag) { return (storage().load(std::memory_order_relaxed) & flag) != 0; }

}  // namespace textfeatures
