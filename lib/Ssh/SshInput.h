#pragma once

#include <TextInput.h>

#include <cstdint>
#include <string>
#include <string_view>

// What the SSH terminal sends to the server for a key: from a Bluetooth keyboard (textinput events,
// with terminal keys on) and from the key row on the screen. No platform code.
namespace sshinput {

// The bytes for one typed event. `ctrl`: the Ctrl key of the screen's key row is latched, so a
// character becomes its control character (c -> Ctrl-C = 0x03). `applicationCursor`: the server
// asked for ESC O A arrows (Terminal::applicationCursorKeys()). PageUp and PageDown give nothing:
// the screen uses them to look back through the lines.
std::string bytesFor(const textinput::Event& event, bool ctrl, bool applicationCursor);

// The control character for a typed character, 0 when it has none: letters (either case) give
// 1..26, and @ [ \ ] ^ _ ? give 0x00, 0x1B..0x1F and 0x7F. '@' and space cannot be told from "none"
// by this function: use isControlNul().
uint8_t controlOf(uint32_t codepoint);
inline bool isControlNul(const uint32_t codepoint) { return codepoint == '@' || codepoint == ' '; }

// A line typed on the on-screen keyboard: its text as UTF-8 with `ctrl` applied to the first
// character only, and a carriage return at the end when `enter`.
std::string bytesForLine(std::string_view text, bool ctrl, bool enter);

}  // namespace sshinput
