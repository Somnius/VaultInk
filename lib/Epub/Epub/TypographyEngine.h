#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Typography Engine (kos25k #30): layout-only options for justified text and hyphenation, all off by default so a
// book lays out as before. The settings travel as one packed word (ReaderRenderSpec::typographyEngine), which the
// section cache keys on; set() is called with it when a section is built or loaded. The line breaker, extractLine and
// the Hyphenator read the current value from here. Everything in this header is plain arithmetic, so it is tested on
// a computer (test/typography_engine).
namespace typoengine {

enum HyphenMode : uint8_t {
  HYPHEN_PATTERNS = 0,   // language patterns, as before
  HYPHEN_SOFT_ONLY = 1,  // only at soft hyphens and visible hyphens already in the word
  HYPHEN_OFF = 2,        // the same as switching Hyphenation off
  HYPHEN_MODE_COUNT = 3,
};

inline constexpr uint8_t CONDENSE_MIN_PERCENT = 50;
inline constexpr uint8_t CONDENSE_STEP_PERCENT = 10;
inline constexpr uint8_t LETTER_SPACING_LEVELS = 3;  // off, small (1 px a letter), medium (2 px)
inline constexpr uint8_t MIN_LETTERS_MAX = 5;        // 0 = the language's own minimum
inline constexpr uint8_t LANGUAGE_COUNT = 13;
// The pattern sets, in the order of the Hyphenation language choice (1..13; 0 = the book's language).
inline constexpr const char* LANGUAGE_TAGS[LANGUAGE_COUNT] = {"en", "fr", "de", "ru", "es", "it", "pl",
                                                              "sv", "uk", "fi", "pt", "el", "grc"};

struct Config {
  uint8_t condensePercent = 100;  // 100 = off; else a word gap may shrink to this share of its width
  uint8_t letterSpacing = 0;      // 0 off, else the most extra pixels between two letters on a loose line
  bool lockedSpace = false;       // the gap after an opening quote or a dash is never stretched or shrunk
  uint8_t hyphenMode = HYPHEN_PATTERNS;
  uint8_t minLeft = 0;   // letters before a pattern break; 0 = the language's own
  uint8_t minRight = 0;  // letters after it
  uint8_t language = 0;  // 0 = book language (Greek words still use Greek patterns), 1..13 = LANGUAGE_TAGS
};

// Snaps a stored percentage to one of 100, 90, 80, 70, 60, 50.
constexpr uint8_t sanitizeCondensePercent(const uint8_t percent) {
  if (percent >= 100) return 100;
  if (percent <= CONDENSE_MIN_PERCENT) return CONDENSE_MIN_PERCENT;
  return static_cast<uint8_t>(percent - percent % CONDENSE_STEP_PERCENT);
}

// A Config with every field inside its range, so a damaged settings file cannot ask for something odd.
constexpr Config sanitized(Config c) {
  c.condensePercent = sanitizeCondensePercent(c.condensePercent);
  if (c.letterSpacing >= LETTER_SPACING_LEVELS) c.letterSpacing = LETTER_SPACING_LEVELS - 1;
  if (c.hyphenMode >= HYPHEN_MODE_COUNT) c.hyphenMode = HYPHEN_PATTERNS;
  if (c.minLeft > MIN_LETTERS_MAX) c.minLeft = MIN_LETTERS_MAX;
  if (c.minRight > MIN_LETTERS_MAX) c.minRight = MIN_LETTERS_MAX;
  if (c.language > LANGUAGE_COUNT) c.language = 0;
  return c;
}

// All defaults pack to 0, which is what "the engine is off" means everywhere else.
constexpr uint32_t pack(const Config& raw) {
  const Config c = sanitized(raw);
  const uint32_t condenseSteps = static_cast<uint32_t>(100 - c.condensePercent) / CONDENSE_STEP_PERCENT;  // 0..5
  return condenseSteps | (static_cast<uint32_t>(c.letterSpacing) << 3) | (static_cast<uint32_t>(c.lockedSpace) << 5) |
         (static_cast<uint32_t>(c.hyphenMode) << 6) | (static_cast<uint32_t>(c.minLeft) << 8) |
         (static_cast<uint32_t>(c.minRight) << 11) | (static_cast<uint32_t>(c.language) << 14);
}

constexpr Config unpack(const uint32_t packed) {
  Config c;
  const uint32_t condenseSteps = packed & 7U;
  c.condensePercent = static_cast<uint8_t>(100 - (condenseSteps > 5 ? 5 : condenseSteps) * CONDENSE_STEP_PERCENT);
  c.letterSpacing = static_cast<uint8_t>((packed >> 3) & 3U);
  c.lockedSpace = ((packed >> 5) & 1U) != 0;
  c.hyphenMode = static_cast<uint8_t>((packed >> 6) & 3U);
  c.minLeft = static_cast<uint8_t>((packed >> 8) & 7U);
  c.minRight = static_cast<uint8_t>((packed >> 11) & 7U);
  c.language = static_cast<uint8_t>((packed >> 14) & 15U);
  return sanitized(c);
}

inline std::atomic<uint32_t>& storage() {
  static std::atomic<uint32_t> value{0};
  return value;
}

inline void set(const uint32_t packed) { storage().store(packed, std::memory_order_relaxed); }
inline Config current() { return unpack(storage().load(std::memory_order_relaxed)); }

// --- Space condensing ---

// The narrowest a word gap of `gap` pixels may become. Rounded up, so a gap is never below the chosen share, and a
// gap that exists stays at least one pixel wide.
constexpr int condensedGap(const int gap, const int percent) {
  if (gap <= 0 || percent >= 100) return gap;
  const int narrow = (gap * percent + 99) / 100;
  return narrow < 1 ? 1 : narrow;
}

// How many pixels one gap can give up.
constexpr int gapShrink(const int gap, const int percent) { return gap - condensedGap(gap, percent); }

// A line that is `deficit` pixels too wide takes this much off each of its gaps. Rounded up: the line may end a
// few pixels short of the edge, never past it.
constexpr int shrinkPerGap(const int deficit, const size_t gapCount) {
  if (deficit <= 0 || gapCount == 0) return 0;
  const int gaps = static_cast<int>(gapCount);
  return (deficit + gaps - 1) / gaps;
}

// --- Letter spacing on loose lines ---

struct LooseSplit {
  int perLetter;  // extra pixels between two letters
  int perGap;     // extra pixels on each word gap
};

// Shares the spare pixels of a justified line. While every gap grows by at most `gapLimit` the letters are left
// alone. Beyond that, the part over the limit goes between the letters, at most `letterCap` each, and the gaps take
// what is left. Both shares are rounded down: perLetter * letterSlots + perGap * gapCount never exceeds spare.
constexpr LooseSplit splitLooseLine(const int spare, const size_t gapCount, const size_t letterSlots,
                                    const int gapLimit, const int letterCap) {
  if (spare <= 0 || gapCount == 0) return {0, 0};
  const int gaps = static_cast<int>(gapCount);
  const int plain = spare / gaps;
  if (letterCap <= 0 || letterSlots == 0 || plain <= gapLimit) return {0, plain};
  const int slots = static_cast<int>(letterSlots);
  const int overLimit = spare - (gapLimit < 0 ? 0 : gapLimit) * gaps;
  int perLetter = overLimit / slots;
  if (perLetter > letterCap) perLetter = letterCap;
  if (perLetter <= 0) return {0, plain};
  return {perLetter, (spare - perLetter * slots) / gaps};
}

// --- Locked space ---

constexpr bool isOpeningQuote(const uint32_t cp) {
  return cp == 0x00AB || cp == 0x2018 || cp == 0x201A || cp == 0x201C || cp == 0x201E || cp == 0x2039;
}

constexpr bool isDash(const uint32_t cp) { return cp == '-' || (cp >= 0x2012 && cp <= 0x2015); }

// True for a word made only of opening quotes and dashes ("—", "«", "–"): the gap after it is locked.
inline bool isLockedSpaceWord(const std::string_view word) {
  if (word.empty()) return false;
  size_t i = 0;
  while (i < word.size()) {
    const auto lead = static_cast<unsigned char>(word[i]);
    const size_t length = lead < 0x80           ? 1
                          : (lead >> 5) == 0x6  ? 2
                          : (lead >> 4) == 0xE  ? 3
                          : (lead >> 3) == 0x1E ? 4
                                                : 0;
    if (length == 0 || i + length > word.size()) return false;
    uint32_t cp = length == 1 ? lead : lead & (0xFFU >> (length + 1));
    for (size_t k = 1; k < length; ++k) cp = (cp << 6) | (static_cast<unsigned char>(word[i + k]) & 0x3FU);
    if (!isOpeningQuote(cp) && !isDash(cp)) return false;
    i += length;
  }
  return true;
}

}  // namespace typoengine
