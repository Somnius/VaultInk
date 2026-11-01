#pragma once

#include <cstddef>
#include <cstdint>

#include "Alarms.h"
#include "Euc.h"
#include "Units.h"

// Saved wheels ("presets") and their text file. Format: key=value lines, one block per wheel, versioned, tolerant of
// unknown keys and bounded in every field, so a damaged or hand-edited file never crashes the reader:
//
//   version=1
//   [wheel]
//   nickname=My Sherman
//   maker=Veteran
//   model=Sherman S
//   brand=1
//   address=AA:BB:CC:DD:EE:FF
//   addressType=0
//   speedUnit=-1          (-1 = use the global setting)
//   ...
namespace euc {

struct WheelPreset {
  char nickname[24] = {};
  char maker[16] = {};
  char model[24] = {};
  Brand brand = Brand::Begode;
  char address[18] = {};    // "AA:BB:CC:DD:EE:FF"
  uint8_t addressType = 0;  // 0 public, 1 random (NimBLE's ble_addr_t type)
  // Unit overrides: -1 none (use the reader's setting), else the enum value.
  int8_t speedUnit = -1;
  int8_t tempUnit = -1;
  int8_t distanceUnit = -1;
  AlarmThresholds alarms;
  uint8_t refreshSeconds = 10;  // one of kRefreshChoices
  int64_t lastConnected = 0;    // epoch seconds, 0 never

  bool hasAddress() const { return address[0] != '\0'; }
  UnitSettings units(const UnitSettings& global) const;
};

constexpr uint8_t kRefreshChoices[] = {5, 10, 15, 30};
bool validRefresh(uint8_t seconds);

struct PresetFile {
  static constexpr size_t kMaxWheels = 16;
  static constexpr uint8_t kVersion = 1;
  WheelPreset wheels[kMaxWheels];
  uint8_t count = 0;

  WheelPreset* findByAddress(const char* address);
  const WheelPreset* findByAddress(const char* address) const;
  // Adds or replaces (by address); false when full.
  bool upsert(const WheelPreset& p);
  bool remove(size_t index);
};

// Valid "AA:BB:CC:DD:EE:FF" (hex, upper or lower case).
bool validAddress(const char* s);

// Writes the whole file into buf; returns the number of characters wanted (snprintf style; truncated when > cap).
// A full 16-wheel file fits in kFileCapacity.
constexpr size_t kPresetFileCapacity = 16 * 512;
int serializePresets(const PresetFile& f, char* buf, size_t cap);

// Reads a file back. Unknown keys are ignored, bad values keep the default, blocks past kMaxWheels are dropped, a
// wheel without a valid address or nickname is dropped. Returns false only when nothing usable was found (no
// "version=" line or no wheels). Never fails on garbage.
bool parsePresets(const char* text, size_t size, PresetFile& out);

}  // namespace euc
