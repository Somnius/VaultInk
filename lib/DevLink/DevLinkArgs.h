#pragma once

// Arguments of the developer build's commands (docs/devlink.md): KEY, TAP, SWIPE, GET, PUT, WAIT,
// base64, the CRC of a screenshot and the gate CMD:WAIT answers through. Platform-free and heap-free, tested in
// test/dev_link. Compiled only with VAULTINK_DEVLINK_FULL (x4pro-dev, x4pro-dev-verify): no other build has any of it.
#ifdef VAULTINK_DEVLINK_FULL

#include <cstddef>
#include <cstdint>

namespace devlink {

struct Token {
  const char* text = "";
  size_t length = 0;
};

// Cuts the text at blanks. Returns the number of words, or maxTokens + 1 when there are more.
size_t splitArgs(const char* args, size_t length, Token* tokens, size_t maxTokens);

// How long a key or a finger may be held, and what is used when the command names no time.
constexpr uint32_t MIN_HOLD_MS = 20;
constexpr uint32_t MAX_HOLD_MS = 5000;
constexpr uint32_t DEFAULT_KEY_HOLD_MS = 120;
constexpr uint32_t DEFAULT_TAP_HOLD_MS = 80;
constexpr uint32_t DEFAULT_SWIPE_MS = 250;

enum class KeyName : uint8_t { Ok, Power, Unknown };

// BACK CONFIRM LEFT RIGHT UP DOWN, as the button numbers 0 to 5 of HalGPIO. POWER is recognised
// only to refuse it by name: the link never presses the power key (no sleep, no power-off).
KeyName parseKeyName(const char* text, size_t length, uint8_t& button);

struct KeyArgs {
  uint8_t button = 0;
  uint32_t holdMs = DEFAULT_KEY_HOLD_MS;
};
// "<name> [hold_ms]". Ok fills `out`. Power: the name was POWER. Unknown: anything else that is
// wrong (the name, the hold time, the number of words).
KeyName parseKeyArgs(const char* args, size_t length, KeyArgs& out);

struct TouchArgs {
  uint32_t x1 = 0;
  uint32_t y1 = 0;
  uint32_t x2 = 0;
  uint32_t y2 = 0;
  uint32_t holdMs = 0;
};
// "x y [hold_ms]": x2/y2 are set to x/y.
bool parseTapArgs(const char* args, size_t length, TouchArgs& out);
// "x1 y1 x2 y2 [ms]"
bool parseSwipeArgs(const char* args, size_t length, TouchArgs& out);

// A coordinate held inside 0 .. extent - 1.
uint16_t clampToExtent(uint32_t value, uint16_t extent);

// The panel pixel under a screen pixel: the inverse of GfxRenderer::tapToLogical. `orientation` is
// GfxRenderer::Orientation as a number (0 portrait, 1 landscape clockwise, 2 portrait inverted,
// 3 landscape counter-clockwise, the panel's own).
void logicalToPanel(uint8_t orientation, uint16_t lx, uint16_t ly, uint16_t panelWidth, uint16_t panelHeight,
                    uint16_t& px, uint16_t& py);

struct FileArgs {
  const char* path = "";
  size_t pathLength = 0;
  uint32_t offset = 0;
  uint32_t length = 0;    // GET
  const char* data = "";  // PUT: base64
  size_t dataLength = 0;
};
// "<path> <offset> <len>": the last two words are numbers, the path is what comes before them and
// may hold blanks.
bool parseGetArgs(const char* args, size_t length, FileArgs& out);
// "<path> <offset> <base64>"
bool parsePutArgs(const char* args, size_t length, FileArgs& out);

// Standard base64 with padding. The encoder returns the text length (no terminator is written), or
// 0 when it does not fit. The decoder takes nothing but whole, padded groups: false for any other
// byte, a wrong length, or a result over `capacity`.
size_t base64Length(size_t bytes);
size_t base64Encode(const uint8_t* data, size_t length, char* out, size_t capacity);
bool base64Decode(const char* text, size_t length, uint8_t* out, size_t capacity, size_t& outLength);

// ---- CMD:WAIT ----

constexpr uint32_t MIN_WAIT_MS = 100;
constexpr uint32_t MAX_WAIT_MS = 60000;
constexpr uint32_t DEFAULT_WAIT_MS = 5000;
constexpr size_t MAX_ACTIVITY_NAME = 31;

struct WaitArgs {
  char activity[MAX_ACTIVITY_NAME + 1] = {};  // "": any screen
  uint32_t timeoutMs = DEFAULT_WAIT_MS;
};
// "[activity] [timeout_ms]". A last word that is a number is the time (100 to 60000); what comes
// before it is the screen's name as STATUS gives it, which may hold blanks. False for a time out
// of range or a name over 31 bytes. A name that is, or ends in, a word of digits needs the time
// written after it.
bool parseWaitArgs(const char* args, size_t length, WaitArgs& out);

// Decides when a pending CMD:WAIT is answered. The firmware calls tick() once per pass of the
// main loop with whether the reader is at rest now (no made-up input queued, nothing to draw, the
// wanted screen in front). One pass at rest is not enough: a key that was let go in this pass is
// acted on in a later one. So the answer is Done only after SETTLE_PASSES passes in a row at rest
// that also span SETTLE_MS. Platform-free; the caller passes millis().
class WaitGate {
 public:
  static constexpr uint32_t SETTLE_MS = 150;
  static constexpr uint32_t SETTLE_PASSES = 3;

  enum class Result : uint8_t { Idle, Pending, Done, TimedOut };

  void start(uint32_t nowMs, uint32_t timeoutMs);
  // Done and TimedOut are returned once: the gate is idle after either.
  Result tick(uint32_t nowMs, bool atRest);
  bool pending() const { return active; }
  // From start() to the last tick().
  uint32_t waitedMs() const { return lastMs - startMs; }

 private:
  uint32_t startMs = 0;
  uint32_t lastMs = 0;
  uint32_t timeoutMs = 0;
  uint32_t restSinceMs = 0;
  uint32_t restPasses = 0;
  bool active = false;
};

// ---- CMD:SHOTCRC ----

// CRC-32 as zlib, PNG and Python's zlib.crc32 compute it. Pass the result back in as `crc` to go
// on with more data; start with 0.
uint32_t crc32(const uint8_t* data, size_t length, uint32_t crc = 0);

}  // namespace devlink

#endif  // VAULTINK_DEVLINK_FULL
