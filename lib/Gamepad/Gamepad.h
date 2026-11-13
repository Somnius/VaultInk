#pragma once

// Gamepad scaffold: the buttons of one Bluetooth gamepad mapped to the reader's actions (lib/Gestures). Input
// plumbing only, no games. Platform-free, so it is host-tested (test/gamepad); GamepadJson.h is the device part
// (the settings keys), and the radio stays with lib/BlePageTurner, which hands this map every raw press.
//
// A button is known by the code of its raw HID edge, exactly as the SDK reports it
// (freeink::RawButtonEvent::code(), freeink-sdk/libs/network/BleKeyboardHost): value | byte index << 8 |
// report id << 16, where the byte is the first one of the report that differs from its rest frame. What that
// means for a gamepad (read from the SDK source, never tried with a real pad):
//  * Only the first 8 payload bytes of a report are compared, and only the FIRST byte that differs counts. A
//    second button held with the first is invisible when its byte comes later, and reads as another button when
//    its byte comes earlier.
//  * A button is a byte VALUE, not a bit: a pad that keeps several buttons in one bitmask byte gives one code
//    per combination held.
//  * An analogue stick or trigger that changes a byte gives a new code for every value it passes through, so
//    sticks and triggers cannot be learned, and a stick that does not sit exactly on its rest value hides every
//    button in the bytes after it. A hat switch is a byte value like any other: one code per direction.
//  * The one "axis pair" layout the SDK's decoder knows (a status byte and two 16-bit axes) comes as a tap with
//    byte index 0xFF and report id 0xFF and the zone as its value; it can be learned like a button.
//
// The map belongs to one pad (its Bluetooth address), so the buttons of a page-turner remote never run through
// it. An empty map changes nothing for anyone.

#include <Gestures.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace gamepad {

inline constexpr uint8_t kMaxButtons = 16;
inline constexpr uint32_t kCodeMask = 0x00FFFFFFu;
inline constexpr size_t kAddrLen = 18;  // "AA:BB:CC:DD:EE:FF"
// The saved text: "RRBBVV:A" per button (hex code, decimal action), joined by commas.
inline constexpr size_t kTextLen = static_cast<size_t>(kMaxButtons) * 10 + 1;
// A button not heard within this long ends the learning (the screen says so).
inline constexpr uint32_t kLearnWaitMs = 15000;

struct Map {
  char addr[kAddrLen] = {};
  uint8_t count = 0;
  uint32_t codes[kMaxButtons] = {};
  uint8_t actions[kMaxButtons] = {};  // gestures::Action, never Default or None
};

// What a pad button can do, in the order the settings screen lists them. Default and None are not choices: a
// button that does nothing is simply not in the map.
inline constexpr gestures::Action kLearnable[] = {
    gestures::NextPage,  gestures::PrevPage,    gestures::NextChapter,      gestures::PrevChapter, gestures::ReaderMenu,
    gestures::QuickMenu, gestures::ToggleNight, gestures::ToggleFrontlight, gestures::LightPanel,  gestures::Bookmark,
    gestures::Refresh,   gestures::GoHome,      gestures::Screenshot};
inline constexpr int kLearnableCount = static_cast<int>(sizeof(kLearnable) / sizeof(kLearnable[0]));
static_assert(kLearnableCount <= kMaxButtons, "one button per action must fit the map");

constexpr bool learnable(const uint8_t action) {
  for (int i = 0; i < kLearnableCount; ++i) {
    if (kLearnable[i] == action) return true;
  }
  return false;
}

// A raw edge is never 0, and the SDK's code has 24 bits.
constexpr bool validCode(const uint32_t code) { return code != 0 && (code & ~kCodeMask) == 0; }

inline bool sameAddr(const char* a, const char* b) {
  return a != nullptr && b != nullptr && a[0] != '\0' && strncmp(a, b, kAddrLen) == 0;
}

// True when the linked device is the pad this map was learned from and it has at least one button.
inline bool routes(const Map& map, const char* linkedAddr) { return map.count > 0 && sameAddr(map.addr, linkedAddr); }

// The action of a button, or gestures::Default when the map does not name it.
inline gestures::Action lookup(const Map& map, const uint32_t code) {
  for (uint8_t i = 0; i < map.count && i < kMaxButtons; ++i) {
    if (map.codes[i] == code) return static_cast<gestures::Action>(map.actions[i]);
  }
  return gestures::Default;
}

// The button of an action, or 0.
inline uint32_t buttonFor(const Map& map, const uint8_t action) {
  for (uint8_t i = 0; i < map.count && i < kMaxButtons; ++i) {
    if (map.actions[i] == action) return map.codes[i];
  }
  return 0;
}

inline void reset(Map& map) { map = Map{}; }

// Takes the action's button away. True when there was one.
inline bool clear(Map& map, const uint8_t action) {
  uint8_t kept = 0;
  for (uint8_t i = 0; i < map.count; ++i) {
    if (map.actions[i] == action) continue;
    map.codes[kept] = map.codes[i];
    map.actions[kept] = map.actions[i];
    ++kept;
  }
  const bool changed = kept != map.count;
  map.count = kept;
  if (map.count == 0) map.addr[0] = '\0';
  return changed;
}

enum class Learned : uint8_t {
  Saved,    // the button now does the action
  Moved,    // saved, and the action this button did before is no longer set (the conflict)
  NewPad,   // saved, and the map of another pad was dropped first: one pad at a time
  Refused,  // nothing changed: not a button code, not a learnable action, or no address
};

// Stores a learned button. An action has one button and a button does one thing: the old entries of both go.
inline Learned learn(Map& map, const char* addr, const uint32_t code, const uint8_t action) {
  if (addr == nullptr || addr[0] == '\0' || strlen(addr) >= kAddrLen || !validCode(code) || !learnable(action)) {
    return Learned::Refused;
  }
  Learned result = Learned::Saved;
  if (!sameAddr(map.addr, addr)) {
    if (map.count > 0) result = Learned::NewPad;
    map = Map{};
    snprintf(map.addr, sizeof(map.addr), "%s", addr);
  }
  uint8_t kept = 0;
  for (uint8_t i = 0; i < map.count; ++i) {
    if (map.actions[i] == action) continue;
    if (map.codes[i] == code) {
      result = Learned::Moved;
      continue;
    }
    map.codes[kept] = map.codes[i];
    map.actions[kept] = map.actions[i];
    ++kept;
  }
  map.count = kept;
  // One button per action and kLearnableCount <= kMaxButtons, so there is always room; the check stays for a map
  // filled some other way.
  if (map.count >= kMaxButtons) return Learned::Refused;
  map.codes[map.count] = code;
  map.actions[map.count] = action;
  ++map.count;
  return result;
}

// --- Learning: which button was pressed ---------------------------------------------------------------------

// A press is kept only once its release came: the SDK marks a release `wasRest` when the "press" was the pad
// idling on a non-zero byte, and that is not a button.
struct Listener {
  uint32_t down = 0;
};

// Feeds one raw edge. Returns the button to keep, or 0 while there is none yet.
inline uint32_t hear(Listener& l, const uint32_t code, const bool pressed, const bool wasRest) {
  if (pressed) {
    l.down = validCode(code) ? code : 0;
    return 0;
  }
  if (l.down == 0 || code != l.down) return 0;
  l.down = 0;
  return wasRest ? 0 : code;
}

// The wait ran out: a button that went down and never reported a release is kept as it is. 0: nothing was heard.
inline uint32_t heardAtTimeout(Listener& l) {
  const uint32_t code = l.down;
  l.down = 0;
  return code;
}

// --- The saved text -------------------------------------------------------------------------------------------

// "030102:2,0001FF:12". Returns the length written (0 for an empty map or a buffer that is too small).
inline size_t format(const Map& map, char* out, const size_t n) {
  if (out == nullptr || n == 0) return 0;
  out[0] = '\0';
  size_t used = 0;
  for (uint8_t i = 0; i < map.count && i < kMaxButtons; ++i) {
    const int wrote = snprintf(out + used, n - used, "%s%06X:%u", i == 0 ? "" : ",",
                               static_cast<unsigned>(map.codes[i] & kCodeMask), static_cast<unsigned>(map.actions[i]));
    if (wrote < 0 || static_cast<size_t>(wrote) >= n - used) {
      out[0] = '\0';
      return 0;
    }
    used += static_cast<size_t>(wrote);
  }
  return used;
}

// Reads the saved text into `map` for the pad `addr`. Anything that does not mean something is dropped, never
// turned into a button that does something odd: a code that is 0 or over 24 bits, an action this version does
// not know or cannot learn, a second entry for a button or an action (the first wins), entries past kMaxButtons.
// Returns how many buttons were kept; with none (or no address) the map is empty.
inline uint8_t parse(const char* addr, const char* text, Map& map) {
  map = Map{};
  if (addr == nullptr || addr[0] == '\0' || strlen(addr) >= kAddrLen || text == nullptr) return 0;
  const char* p = text;
  while (*p != '\0' && map.count < kMaxButtons) {
    uint32_t code = 0;
    int digits = 0;
    bool bad = false;
    for (; *p != '\0' && *p != ':' && *p != ','; ++p) {
      const char c = *p;
      const int v = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
      if (v < 0 || ++digits > 6) {
        bad = true;
        continue;
      }
      code = code << 4 | static_cast<uint32_t>(v);
    }
    uint32_t action = 0;
    int actionDigits = 0;
    if (*p == ':') {
      for (++p; *p != '\0' && *p != ','; ++p) {
        if (*p < '0' || *p > '9' || ++actionDigits > 3) {
          bad = true;
          continue;
        }
        action = action * 10 + static_cast<uint32_t>(*p - '0');
      }
    }
    if (*p == ',') ++p;
    if (bad || digits == 0 || actionDigits == 0 || !validCode(code) || action > 0xFF ||
        !learnable(static_cast<uint8_t>(action))) {
      continue;
    }
    if (lookup(map, code) != gestures::Default || buttonFor(map, static_cast<uint8_t>(action)) != 0) continue;
    map.codes[map.count] = code;
    map.actions[map.count] = static_cast<uint8_t>(action);
    ++map.count;
  }
  if (map.count > 0) snprintf(map.addr, sizeof(map.addr), "%s", addr);
  return map.count;
}

// "3:1=02" (report id 3, byte 1, value 0x02), or "0x43" for a button known only by the key the SDK's decoder
// read (byte index 0xFF): the same spelling the page turner's Map Buttons rows use.
inline void formatCode(char* out, const size_t n, const uint32_t code) {
  if (((code >> 8) & 0xFF) == 0xFF) {
    snprintf(out, n, "0x%02X", static_cast<unsigned>(code & 0xFF));
  } else {
    snprintf(out, n, "%u:%u=%02X", static_cast<unsigned>((code >> 16) & 0xFF),
             static_cast<unsigned>((code >> 8) & 0xFF), static_cast<unsigned>(code & 0xFF));
  }
}

}  // namespace gamepad
