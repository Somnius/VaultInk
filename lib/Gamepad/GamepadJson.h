#pragma once

#include <ArduinoJson.h>

#include "Gamepad.h"

// The device part of lib/Gamepad: the map in the host's settings.json, under the keys gamepadAddr (the pad's
// Bluetooth address) and gamepadButtons (gamepad::format). Neither key is written while nothing is mapped, and a
// file without them loads as an empty map: nothing changes for anyone who never used it.
namespace gamepad {

inline void writeJson(const Map& map, JsonDocument& doc) {
  if (map.count == 0) return;
  char text[kTextLen];
  if (format(map, text, sizeof(text)) == 0) return;
  doc["gamepadAddr"] = map.addr;
  doc["gamepadButtons"] = text;
}

inline void readJson(Map& map, JsonVariantConst doc) {
  // Copied out first: parse() clears the map, and the strings belong to the document.
  char addr[kAddrLen] = {};
  const char* savedAddr = doc["gamepadAddr"] | "";
  if (strlen(savedAddr) < sizeof(addr)) snprintf(addr, sizeof(addr), "%s", savedAddr);
  parse(addr, doc["gamepadButtons"] | "", map);
}

}  // namespace gamepad
