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

enum PunctSpace : uint8_t {
  PUNCT_SPACE_NORMAL = 0,   // a normal space after a punctuation mark, as before
  PUNCT_SPACE_MINIMAL = 1,  // half a space, never stretched or shrunk
  PUNCT_SPACE_NONE = 2,     // no space: the next word follows the mark (the line may still break there)
  PUNCT_SPACE_COUNT = 3,
};

inline constexpr uint8_t CONDENSE_MIN_PERCENT = 50;
inline constexpr uint8_t CONDENSE_STEP_PERCENT = 10;
// Letter spacing on loose lines: off, small, medium, large, max (at most 1, 2, 3, 4 px a letter; the choice number
// is the cap in pixels, see letterSpacingCap).
inline constexpr uint8_t LETTER_SPACING_LEVELS = 5;
// The narrowest an ordinary word gap may end, as a share of the font's space, after Optical spacing and Space
// condensing together (0.77.1). Optical spacing alone never goes under this (100 - OPTICAL_MAX_PERCENT), and it is a
// little under the 2/3 of a space that TeX lets a justified gap shrink to. The space after a punctuation mark in
// Minimal or None is narrower on purpose and is not held to it.
inline constexpr uint8_t GAP_FLOOR_PERCENT = 60;
// Mixed into a packed word that is not 0, so sections cached by an older layout of the same settings are built
// again. 1 = 0.77.1 (gap floor, letter spacing shares, loose line balancing). 2 = total-fit line breaking for
// justified text with hyphenation on, and patterns by script for a book that names no language. That one also
// changes text laid out with the engine off, whose packed word is 0, so the section file keeps the revision in its
// own header field as well (layoutKey in Section.cpp) and compares it for every section.
inline constexpr uint32_t LAYOUT_REVISION = 2;
inline constexpr uint8_t MIN_LETTERS_MAX = 5;  // 0 = the language's own minimum
inline constexpr uint8_t LANGUAGE_COUNT = 13;
inline constexpr uint8_t PUNCT_MINIMAL_PERCENT = 50;  // Space after punctuation, Minimal: this share of a space
inline constexpr uint8_t OPTICAL_MAX_PERCENT = 40;    // Optical spacing moves a gap by at most this share of a space
inline constexpr uint8_t OPTICAL_MIN_PERCENT = 30;    // and never leaves it narrower than this share
// The pattern sets, in the order of the Hyphenation language choice (1..13; 0 = the book's language).
inline constexpr const char* LANGUAGE_TAGS[LANGUAGE_COUNT] = {"en", "fr", "de", "ru", "es", "it", "pl",
                                                              "sv", "uk", "fi", "pt", "el", "grc"};
// What the choice list shows for each: the language's own name, the same in every interface language.
inline constexpr const char* LANGUAGE_NAMES[LANGUAGE_COUNT] = {"English",
                                                               "Français",
                                                               "Deutsch",
                                                               "Русский",
                                                               "Español",
                                                               "Italiano",
                                                               "Polski",
                                                               "Svenska",
                                                               "Українська",
                                                               "Suomi",
                                                               "Português",
                                                               "Ελληνικά",
                                                               "Ελληνικά (πολυτονικά)"};

// The name of Hyphenation language choice 1..13; empty for 0 (the book's language, a translated string) and for a
// number outside the choices.
constexpr const char* languageName(const int choice) {
  return choice >= 1 && choice <= LANGUAGE_COUNT ? LANGUAGE_NAMES[choice - 1] : "";
}

struct Config {
  uint8_t condensePercent = 100;  // 100 = off; else a word gap may shrink to this share of its width
  uint8_t letterSpacing = 0;      // 0 off, else the most extra pixels between two letters on a loose line
  bool lockedSpace = false;       // the gap after an opening quote or a dash is never stretched or shrunk
  uint8_t hyphenMode = HYPHEN_PATTERNS;
  uint8_t minLeft = 0;   // letters before a pattern break; 0 = the language's own
  uint8_t minRight = 0;  // letters after it
  uint8_t language = 0;  // 0 = book language (Greek words still use Greek patterns), 1..13 = LANGUAGE_TAGS
  uint8_t punctSpace = PUNCT_SPACE_NORMAL;  // the space after , . ; : · ! ? » ) and the like
  bool opticalSpacing = false;              // word gaps corrected by the side room of the two glyphs around them
};

// Snaps a stored percentage to one of 100, 90, 80, 70, 60, 50. A value outside 50..100 (0 in a file edited by hand)
// is not a choice and means off, never the strongest condensing.
constexpr uint8_t sanitizeCondensePercent(const uint8_t percent) {
  if (percent >= 100 || percent < CONDENSE_MIN_PERCENT) return 100;
  return static_cast<uint8_t>(percent - percent % CONDENSE_STEP_PERCENT);
}

// A Config with every field inside its range, so a damaged settings file cannot ask for something odd.
constexpr Config sanitized(Config c) {
  c.condensePercent = sanitizeCondensePercent(c.condensePercent);
  if (c.letterSpacing >= LETTER_SPACING_LEVELS) c.letterSpacing = 0;  // not a choice: off, never the strongest
  if (c.hyphenMode >= HYPHEN_MODE_COUNT) c.hyphenMode = HYPHEN_PATTERNS;
  if (c.minLeft > MIN_LETTERS_MAX) c.minLeft = MIN_LETTERS_MAX;
  if (c.minRight > MIN_LETTERS_MAX) c.minRight = MIN_LETTERS_MAX;
  if (c.language > LANGUAGE_COUNT) c.language = 0;
  if (c.punctSpace >= PUNCT_SPACE_COUNT) c.punctSpace = PUNCT_SPACE_NORMAL;
  return c;
}

// All defaults pack to 0, which is what "the engine is off" means everywhere else. Bits: condensing 0-2, letter
// spacing 3-4 and 21 (its third bit, for Large and Max), locked space 5, hyphenation mode 6-7, letters before 8-10
// and after 11-13, language 14-17, space after punctuation 18-19, optical spacing 20, LAYOUT_REVISION 28-30 (only
// in a word that is not 0).
constexpr uint32_t pack(const Config& raw) {
  const Config c = sanitized(raw);
  const uint32_t condenseSteps = static_cast<uint32_t>(100 - c.condensePercent) / CONDENSE_STEP_PERCENT;  // 0..5
  const uint32_t settings = condenseSteps | (static_cast<uint32_t>(c.letterSpacing & 3U) << 3) |
                            (static_cast<uint32_t>(c.lockedSpace) << 5) | (static_cast<uint32_t>(c.hyphenMode) << 6) |
                            (static_cast<uint32_t>(c.minLeft) << 8) | (static_cast<uint32_t>(c.minRight) << 11) |
                            (static_cast<uint32_t>(c.language) << 14) | (static_cast<uint32_t>(c.punctSpace) << 18) |
                            (static_cast<uint32_t>(c.opticalSpacing) << 20) |
                            (static_cast<uint32_t>(c.letterSpacing >> 2) << 21);
  return settings == 0 ? 0 : settings | (LAYOUT_REVISION << 28);
}

constexpr Config unpack(const uint32_t packed) {
  Config c;
  const uint32_t condenseSteps = packed & 7U;
  c.condensePercent = static_cast<uint8_t>(100 - (condenseSteps > 5 ? 5 : condenseSteps) * CONDENSE_STEP_PERCENT);
  c.letterSpacing = static_cast<uint8_t>(((packed >> 3) & 3U) | (((packed >> 21) & 1U) << 2));
  c.lockedSpace = ((packed >> 5) & 1U) != 0;
  c.hyphenMode = static_cast<uint8_t>((packed >> 6) & 3U);
  c.minLeft = static_cast<uint8_t>((packed >> 8) & 7U);
  c.minRight = static_cast<uint8_t>((packed >> 11) & 7U);
  c.language = static_cast<uint8_t>((packed >> 14) & 15U);
  c.punctSpace = static_cast<uint8_t>((packed >> 18) & 3U);
  c.opticalSpacing = ((packed >> 20) & 1U) != 0;
  return sanitized(c);
}

// Whether words are hyphenated at all: the Hyphenation switch is on and the engine's Hyphenation mode is not Off.
// Everything that shows or uses the switch asks this, so the two can not disagree.
constexpr bool hyphenationOn(const uint8_t hyphenationSwitch, const uint8_t hyphenMode) {
  return hyphenationSwitch != 0 && hyphenMode != HYPHEN_OFF;
}

// Sets hyphenation on or off, for everything that shows it as one switch (the Text settings, the book's Text panel,
// the web settings page). Off clears the switch only and keeps the mode. On sets the switch, and a mode of Off goes
// back to Patterns, or hyphenationOn() would still say off.
constexpr void setHyphenation(uint8_t& hyphenationSwitch, uint8_t& hyphenMode, const bool on) {
  hyphenationSwitch = on ? 1 : 0;
  if (on && hyphenMode == HYPHEN_OFF) hyphenMode = HYPHEN_PATTERNS;
}

// What a press on the Hyphenation checkbox does: the opposite of what hyphenationOn() says now.
constexpr void toggleHyphenation(uint8_t& hyphenationSwitch, uint8_t& hyphenMode) {
  setHyphenation(hyphenationSwitch, hyphenMode, !hyphenationOn(hyphenationSwitch, hyphenMode));
}

inline std::atomic<uint32_t>& storage() {
  static std::atomic<uint32_t> value{0};
  return value;
}

inline void set(const uint32_t packed) { storage().store(packed, std::memory_order_relaxed); }
inline Config current() { return unpack(storage().load(std::memory_order_relaxed)); }

// Text that is not the book (a dictionary definition, the Text settings sample) is laid out with the engine off:
// while one of these is alive current() is all defaults, and the open book's value is back when it goes away. Only
// the activity on top lays text out, so a book's build never runs while one is alive.
class ScopedOff {
 public:
  ScopedOff() : saved(storage().exchange(0, std::memory_order_relaxed)) {}
  ~ScopedOff() { storage().store(saved, std::memory_order_relaxed); }
  ScopedOff(const ScopedOff&) = delete;
  ScopedOff& operator=(const ScopedOff&) = delete;

 private:
  uint32_t saved;
};

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

// The narrowest an ordinary word gap may end when the font's space is `space` pixels wide (rounded up).
constexpr int gapFloor(const int space) { return condensedGap(space, GAP_FLOOR_PERCENT); }

// How many pixels a word gap that is `gap` wide before condensing (the space, or what Optical spacing made of it)
// can give up in a condensed line: its condensing share, but never so much that it ends under gapFloor(space). The
// line breaker and the line builder both ask this, so a line the breaker let run over always fits.
constexpr int gapAllowance(const int gap, const int space, const int percent) {
  const int share = gapShrink(gap, percent);
  const int room = gap - gapFloor(space);
  if (room <= 0) return 0;
  return share < room ? share : room;
}

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

// The most extra pixels between two letters for a Letter spacing choice (0 off, 1 small ... 4 max): the choice
// number, but no more than a third of the font's space (and 2 px, what Medium always was), so the room between two
// letters stays well under the narrowest word gap (gapFloor) and the words still read as words.
constexpr int letterSpacingCap(const uint8_t level, const int space) {
  const int bound = space / 3 < 2 ? 2 : space / 3;
  return level < bound ? level : bound;
}

// Shares the spare pixels of a justified line. While every gap grows by at most `gapLimit` the letters are left
// alone. Beyond that the letters come first (0.77.1): every letter slot takes the same whole number of pixels, as
// many as fit in the spare room and at most `letterCap`, and the gaps take what is left, so they stay near their
// normal width. (Before, only the part over the limit went to the letters, so a line had to be far looser before
// Medium's second pixel showed.) Both shares are rounded down: perLetter * letterSlots + perGap * gapCount never
// exceeds spare.
constexpr LooseSplit splitLooseLine(const int spare, const size_t gapCount, const size_t letterSlots,
                                    const int gapLimit, const int letterCap) {
  if (spare <= 0 || gapCount == 0) return {0, 0};
  const int gaps = static_cast<int>(gapCount);
  const int plain = spare / gaps;
  if (letterCap <= 0 || letterSlots == 0 || plain <= gapLimit) return {0, plain};
  const int slots = static_cast<int>(letterSlots);
  int perLetter = spare / slots;
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

// --- Space after punctuation ---

// The marks whose following space Space after punctuation narrows or removes: comma, full stop, semicolon (also the
// Greek question mark), colon, the Greek ano teleia, question and exclamation marks, closing quotes and brackets.
constexpr bool isSpacedPunctuation(const uint32_t cp) {
  return cp == ',' || cp == '.' || cp == ';' || cp == ':' || cp == '!' || cp == '?' || cp == ')' || cp == ']' ||
         cp == '}' || cp == 0x00B7 || cp == 0x0387 || cp == 0x037E || cp == 0x00BB || cp == 0x201D || cp == 0x203A;
}

// The width of the space after such a mark, from the width of a normal space.
constexpr int punctuationGap(const int space, const uint8_t mode) {
  if (mode == PUNCT_SPACE_NONE) return 0;
  if (mode == PUNCT_SPACE_MINIMAL) return condensedGap(space, PUNCT_MINIMAL_PERCENT);
  return space;
}

// --- Optical spacing ---

// A word gap looks as wide as the space plus the empty room beside the two glyphs around it (`sideRoom`: the right
// side bearing of the last glyph of one word plus the left side bearing of the first glyph of the next). With every
// gap narrowed by its side room less the paragraph's average (`averageRoom`), the gaps look the same width and the
// text keeps its length on average. The change is at most OPTICAL_MAX_PERCENT of a space either way, and a gap is
// never left under OPTICAL_MIN_PERCENT of one (or under one pixel).
constexpr int opticalGap(const int space, const int sideRoom, const int averageRoom) {
  if (space <= 0) return space;
  const int limit = space * OPTICAL_MAX_PERCENT / 100;
  int change = sideRoom - averageRoom;
  if (change > limit) change = limit;
  if (change < -limit) change = -limit;
  const int floor = condensedGap(space, OPTICAL_MIN_PERCENT);
  const int gap = space - change;
  return gap < floor ? floor : gap;
}

}  // namespace typoengine
