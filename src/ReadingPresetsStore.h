#pragma once

#include <ReadingPresets.h>

#include <string>

// Reading presets (kos25k #12): the ten slots of /.crosspoint/reading-presets.json, read on first use and kept in
// RAM. Copies the Text panel's layout settings to and from CrossPointSettings; the reader lays the book out
// again after apply() (applyTextSettingLive or applyReaderTextSettings), the Settings screen only saves.
// Saves go through a .tmp file; a file that can't be read is kept as .bad, never overwritten.
class ReadingPresetsStore {
 public:
  static constexpr int SLOTS = readingpresets::kSlots;

  static ReadingPresetsStore& getInstance() {
    static ReadingPresetsStore instance;
    return instance;
  }

  // Reads the file once (later calls are free).
  void ensureLoaded();
  const readingpresets::Preset& slot(int index);
  // The slot whose values equal the current settings (and, for a preset with gestures, the current gestures),
  // else -1.
  int currentSlot();

  // The current layout settings, as the Text panel shows them.
  static readingpresets::Layout capture();
  // The current reader gestures and Quick Menu entries (Settings > Reader > Gestures).
  static readingpresets::Gestures captureGestures();
  // Stores the current settings in `index` under `name` (empty: the slot keeps its name) and writes the file.
  // With `includeGestures` the preset also carries the current gestures; without, it is layout only (and a slot
  // that had gestures loses them).
  bool saveCurrent(int index, const std::string& name, bool includeGestures = false);
  // Copies the slot's values into SETTINGS and saves settings.json. False for an empty slot. A preset saved with
  // its gestures sets those too (`gesturesSet`, when given, says so); any other preset leaves the gestures alone.
  bool apply(int index, bool* gesturesSet = nullptr);

 private:
  ReadingPresetsStore() = default;
  bool save();

  readingpresets::PresetList list;
  bool loaded = false;
};

#define READING_PRESET_STORE ReadingPresetsStore::getInstance()
