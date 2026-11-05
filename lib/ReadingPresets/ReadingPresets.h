#pragma once

#include <cstddef>
#include <cstdint>

// Reading presets (kos25k #12): ten named slots, each a copy of the Text panel's layout settings, kept in
// /.crosspoint/reading-presets.json. This file does the names and the JSON; the device-side store copies the
// values to and from CrossPointSettings. No heap, no exceptions, no Arduino/ESP includes, so the same bytes come
// out on the device and in the host tests.
namespace readingpresets {

constexpr int kSlots = 10;
constexpr size_t kNameCapacity = 25;            // 24 bytes of UTF-8 plus the NUL
constexpr size_t kFontFamilyNameCapacity = 32;  // same as CrossPointSettings::sdFontFamilyName
constexpr uint8_t kFileVersion = 1;

// The layout settings a preset carries, in the order of the Text panel plus Advanced Typography.
struct Layout {
  uint8_t fontFamily = 0;
  char sdFontFamilyName[kFontFamilyNameCapacity] = {};  // empty: the built-in fontFamily
  uint8_t fontPointSize = 0;
  uint8_t lineSpacing = 0;
  uint8_t paragraphAlignment = 0;
  uint8_t hyphenationEnabled = 0;
  uint8_t focusReadingEnabled = 0;
  uint8_t evenWordGaps = 0;
  uint8_t wordSpacing = 0;
  uint8_t characterSpacing = 0;
  uint8_t screenMargin = 0;
  uint8_t paragraphIndentSpaces = 0;
  uint8_t textLigatures = 0;
  uint8_t textKerning = 0;
  uint8_t textWidowOrphan = 0;
  uint8_t textKeepHeadings = 0;
  uint8_t textHanging = 0;

  bool operator==(const Layout& other) const;
  bool operator!=(const Layout& other) const { return !(*this == other); }
};

struct Preset {
  char name[kNameCapacity] = {};
  bool saved = false;
  Layout layout;
};

struct PresetList {
  Preset slots[kSlots] = {};

  PresetList() { reset(); }
  // Every slot empty with its default name ("Preset 1" .. "Preset 10").
  void reset();
  // Index of the first saved slot whose layout equals `layout`, else -1.
  int find(const Layout& layout) const;
};

// Writes "Preset N" (N = slot + 1) into `out`; `cap` includes the NUL.
void defaultName(int slot, char* out, size_t cap);
// Trims blanks, replaces control characters with spaces and cuts to kNameCapacity-1 bytes on a UTF-8
// boundary. An empty result falls back to the slot's default name. Returns false (name unchanged) for a
// slot out of range.
bool setName(PresetList& list, int slot, const char* name);
// Stores `layout` in `slot` and marks it saved. Returns false for a slot out of range.
bool save(PresetList& list, int slot, const Layout& layout);
// Empties a slot; its name stays. Returns false for a slot out of range.
bool clear(PresetList& list, int slot);

// Writes the list as JSON into buf (NUL-terminated when cap > 0) and returns the length it needs, snprintf
// style; when the return value is >= cap the output was cut.
size_t serialize(const PresetList& list, char* buf, size_t cap);
// Reads what serialize wrote. Missing keys keep their defaults, unknown keys are skipped, extra slots are
// ignored. False (and `out` reset) when the text is not a JSON object.
bool parse(const char* data, size_t size, PresetList& out);

}  // namespace readingpresets
