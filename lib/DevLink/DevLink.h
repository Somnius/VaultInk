#pragma once

#include <cstddef>
#include <cstdint>

// The serial command link (docs/devlink.md). Platform-free and heap-free, so the host tests in
// test/dev_link cover it. The firmware feeds it bytes from the USB serial port and only dispatches.
// Three kinds of build (docs/devlink.md):
//   neither macro           the builds people install: only CMD:SCREENSHOT is answered.
//   VAULTINK_DEVLINK        the link is compiled in. With VAULTINK_DEVLINK_USER it is the read-only
//                           link of the user-installable developer build (x4pro-dev-user): PING HELP
//                           STATUS LOG BENCH REBOOT SCREENSHOT and nothing else.
//   + VAULTINK_DEVLINK_FULL the developer's own builds (x4pro-dev, x4pro-dev-verify): also
//                           BOOTLOADER, KEY, TAP, SWIPE, FLASH, LS, GET, PUT, FETCH, WAIT and SHOTCRC.
#if defined(VAULTINK_DEVLINK_FULL) && !defined(VAULTINK_DEVLINK)
#error "VAULTINK_DEVLINK_FULL needs VAULTINK_DEVLINK"
#endif
#if defined(VAULTINK_DEVLINK_USER) && !defined(VAULTINK_DEVLINK)
#error "VAULTINK_DEVLINK_USER needs VAULTINK_DEVLINK"
#endif
#if defined(VAULTINK_DEVLINK_USER) && defined(VAULTINK_DEVLINK_FULL)
#error "VAULTINK_DEVLINK_USER is the read-only link: it cannot be built with VAULTINK_DEVLINK_FULL"
#endif

namespace devlink {

// Collects bytes into lines without ever waiting for more. A line ends at '\n'; every '\r' is
// dropped. A line longer than MAX_LINE bytes is thrown away whole: nothing is returned until the
// next '\n', and the line after that one is read normally.
class LineAssembler {
 public:
  static constexpr size_t MAX_LINE = 256;

  // Takes one byte. True when it completed a line: line() and length() then hold it (without the
  // line end) until the next feed(). An empty line is a line too, with length 0.
  bool feed(char c, uint32_t nowMs = 0);
  // A half-finished line is forgotten when the next byte comes this long after the last one (a stray
  // keystroke must not glue itself to the next command, as the old read with a timeout never allowed).
  static constexpr uint32_t IDLE_DROP_MS = 1000;

  const char* line() const { return buffer; }
  size_t length() const { return ready ? used : 0; }
  // Lines thrown away for being too long.
  uint32_t dropped() const { return droppedLines; }

 private:
  char buffer[MAX_LINE + 1] = {};
  size_t used = 0;
  uint32_t droppedLines = 0;
  uint32_t lastByteMs = 0;
  bool ready = false;     // buffer holds a finished line; the next byte starts a new one
  bool skipping = false;  // inside a line that was too long
};

enum class Command : uint8_t {
  None,     // not a "CMD:" line
  Unknown,  // "CMD:" with a name that is not in the list
  Screenshot,
  Status,
  Log,
  Reboot,
  Bootloader,
  Ping,
  Help,
  Bench,  // the SD bench logger (DevLinkBench.h)
  // The names below are in the table only with VAULTINK_DEVLINK_FULL (the developer's own
  // builds): every other build, the read-only x4pro-dev-user included, parses them as Unknown,
  // and its name table is the size it was.
  Key,      // CMD:KEY <name> [hold_ms]
  Tap,      // CMD:TAP x y [hold_ms]
  Swipe,    // CMD:SWIPE x1 y1 x2 y2 [ms]
  Flash,    // CMD:FLASH <sd path>
  Ls,       // CMD:LS <dir>
  Get,      // CMD:GET <path> <offset> <len>
  Put,      // CMD:PUT <path> <offset> <base64>
  Wait,     // CMD:WAIT [activity] [timeout_ms]
  ShotCrc,  // CMD:SHOTCRC
  Fetch,    // CMD:FETCH <http url> <sd path> <sha256>
};

struct Parsed {
  Command command = Command::None;
  // What follows the name, without the blanks around it. Points into the parsed line and is not
  // terminated: use argsLength.
  const char* args = "";
  size_t argsLength = 0;
};

// Reads "CMD:<NAME> [args]". Blanks around the name and at the end are ignored; the name is
// case-sensitive, as CMD:SCREENSHOT always was.
Parsed parse(const char* line, size_t length);

// The command names, separated by single spaces, for HELP.
const char* commandList();

// Reads a decimal number with no sign and nothing after it. False for anything else, including
// an empty text and a value over 32 bits.
bool parseUnsigned(const char* text, size_t length, uint32_t& value);

// Builds one JSON object on one line in a fixed buffer. A field that does not fit is left out
// whole and marks the reply as too long: finish() then returns an error object instead, so a
// reader never sees a cut line.
class JsonLine {
 public:
  static constexpr size_t CAPACITY = 768;  // with the closing brace and the terminator

  JsonLine();

  JsonLine& addString(const char* key, const char* value);
  JsonLine& addBool(const char* key, bool value);
  JsonLine& addInt(const char* key, long long value);
  JsonLine& addUnsigned(const char* key, unsigned long long value);

  bool tooLong() const { return overflow; }
  // Closes the object and returns the text (no line end). Call it once, last.
  const char* finish();
  size_t length() const { return used; }

 private:
  bool startField(const char* key);
  bool put(char c);
  bool putText(const char* text);
  bool putEscaped(const char* text);

  char buffer[CAPACITY];
  size_t used = 0;
  bool overflow = false;
  bool first = true;
};

}  // namespace devlink
