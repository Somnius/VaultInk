#include "DevLinkArgs.h"

#ifdef VAULTINK_DEVLINK_FULL

#include <cstring>

#include "DevLink.h"

namespace devlink {
namespace {

bool isBlank(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

bool sameText(const Token& token, const char* text) {
  return strlen(text) == token.length && memcmp(text, token.text, token.length) == 0;
}

bool parseHold(const Token& token, uint32_t& holdMs) {
  uint32_t value = 0;
  if (!parseUnsigned(token.text, token.length, value) || value < MIN_HOLD_MS || value > MAX_HOLD_MS) return false;
  holdMs = value;
  return true;
}

// Screen coordinates are far below this; the cap only keeps a typing mistake out.
bool parseCoordinate(const Token& token, uint32_t& value) {
  return parseUnsigned(token.text, token.length, value) && value <= 65535;
}

constexpr char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(const char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Splits "<path> <a> <b>" from the end: the path keeps its blanks.
bool splitPathAndTwo(const char* args, const size_t length, Token& path, Token& first, Token& second) {
  if (!args) return false;
  const char* end = args + length;
  const auto lastWord = [&](Token& word) {
    while (end > args && isBlank(end[-1])) --end;
    const char* start = end;
    while (start > args && !isBlank(start[-1])) --start;
    word.text = start;
    word.length = static_cast<size_t>(end - start);
    end = start;
    return word.length > 0;
  };
  if (!lastWord(second) || !lastWord(first)) return false;
  while (end > args && isBlank(end[-1])) --end;
  while (args < end && isBlank(*args)) ++args;
  path.text = args;
  path.length = static_cast<size_t>(end - args);
  return path.length > 0;
}

}  // namespace

size_t splitArgs(const char* args, const size_t length, Token* tokens, const size_t maxTokens) {
  size_t count = 0;
  const char* at = args;
  const char* end = args + length;
  while (at < end) {
    while (at < end && isBlank(*at)) ++at;
    if (at >= end) break;
    const char* start = at;
    while (at < end && !isBlank(*at)) ++at;
    if (count == maxTokens) return maxTokens + 1;
    tokens[count].text = start;
    tokens[count].length = static_cast<size_t>(at - start);
    ++count;
  }
  return count;
}

KeyName parseKeyName(const char* text, const size_t length, uint8_t& button) {
  static constexpr const char* NAMES[] = {"BACK", "CONFIRM", "LEFT", "RIGHT", "UP", "DOWN"};
  const Token token{text, length};
  for (uint8_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); ++i) {
    if (sameText(token, NAMES[i])) {
      button = i;
      return KeyName::Ok;
    }
  }
  return sameText(token, "POWER") ? KeyName::Power : KeyName::Unknown;
}

KeyName parseKeyArgs(const char* args, const size_t length, KeyArgs& out) {
  Token tokens[2];
  const size_t count = splitArgs(args, length, tokens, 2);
  if (count < 1 || count > 2) return KeyName::Unknown;
  KeyArgs parsed;
  const KeyName name = parseKeyName(tokens[0].text, tokens[0].length, parsed.button);
  if (name != KeyName::Ok) return name;
  if (count == 2 && !parseHold(tokens[1], parsed.holdMs)) return KeyName::Unknown;
  out = parsed;
  return KeyName::Ok;
}

bool parseTapArgs(const char* args, const size_t length, TouchArgs& out) {
  Token tokens[3];
  const size_t count = splitArgs(args, length, tokens, 3);
  if (count < 2 || count > 3) return false;
  TouchArgs parsed;
  parsed.holdMs = DEFAULT_TAP_HOLD_MS;
  if (!parseCoordinate(tokens[0], parsed.x1) || !parseCoordinate(tokens[1], parsed.y1)) return false;
  if (count == 3 && !parseHold(tokens[2], parsed.holdMs)) return false;
  parsed.x2 = parsed.x1;
  parsed.y2 = parsed.y1;
  out = parsed;
  return true;
}

bool parseSwipeArgs(const char* args, const size_t length, TouchArgs& out) {
  Token tokens[5];
  const size_t count = splitArgs(args, length, tokens, 5);
  if (count < 4 || count > 5) return false;
  TouchArgs parsed;
  parsed.holdMs = DEFAULT_SWIPE_MS;
  if (!parseCoordinate(tokens[0], parsed.x1) || !parseCoordinate(tokens[1], parsed.y1) ||
      !parseCoordinate(tokens[2], parsed.x2) || !parseCoordinate(tokens[3], parsed.y2)) {
    return false;
  }
  if (count == 5 && !parseHold(tokens[4], parsed.holdMs)) return false;
  out = parsed;
  return true;
}

uint16_t clampToExtent(const uint32_t value, const uint16_t extent) {
  if (extent == 0) return 0;
  return value >= extent ? static_cast<uint16_t>(extent - 1) : static_cast<uint16_t>(value);
}

void logicalToPanel(const uint8_t orientation, const uint16_t lx, const uint16_t ly, const uint16_t panelWidth,
                    const uint16_t panelHeight, uint16_t& px, uint16_t& py) {
  uint32_t x = lx;
  uint32_t y = ly;
  switch (orientation) {
    case 0:  // portrait: tapToLogical gives x = panelHeight - 1 - py, y = px
      x = ly;
      y = panelHeight - 1u - clampToExtent(lx, panelHeight);
      break;
    case 2:  // portrait inverted: x = py, y = panelWidth - 1 - px
      x = panelWidth - 1u - clampToExtent(ly, panelWidth);
      y = lx;
      break;
    case 1:  // landscape clockwise: x = panelWidth - 1 - px, y = panelHeight - 1 - py
      x = panelWidth - 1u - clampToExtent(lx, panelWidth);
      y = panelHeight - 1u - clampToExtent(ly, panelHeight);
      break;
    default:  // landscape counter-clockwise: the panel's own
      break;
  }
  px = clampToExtent(x, panelWidth);
  py = clampToExtent(y, panelHeight);
}

bool parseGetArgs(const char* args, const size_t length, FileArgs& out) {
  Token path;
  Token offset;
  Token count;
  if (!splitPathAndTwo(args, length, path, offset, count)) return false;
  FileArgs parsed;
  if (!parseUnsigned(offset.text, offset.length, parsed.offset) ||
      !parseUnsigned(count.text, count.length, parsed.length)) {
    return false;
  }
  parsed.path = path.text;
  parsed.pathLength = path.length;
  out = parsed;
  return true;
}

bool parsePutArgs(const char* args, const size_t length, FileArgs& out) {
  Token path;
  Token offset;
  Token data;
  if (!splitPathAndTwo(args, length, path, offset, data)) return false;
  FileArgs parsed;
  if (!parseUnsigned(offset.text, offset.length, parsed.offset)) return false;
  parsed.path = path.text;
  parsed.pathLength = path.length;
  parsed.data = data.text;
  parsed.dataLength = data.length;
  out = parsed;
  return true;
}

size_t base64Length(const size_t bytes) { return (bytes + 2) / 3 * 4; }

size_t base64Encode(const uint8_t* data, const size_t length, char* out, const size_t capacity) {
  const size_t needed = base64Length(length);
  if (needed > capacity) return 0;
  size_t at = 0;
  for (size_t i = 0; i < length; i += 3) {
    const size_t left = length - i;
    const uint32_t group = (static_cast<uint32_t>(data[i]) << 16) |
                           (left > 1 ? static_cast<uint32_t>(data[i + 1]) << 8 : 0u) |
                           (left > 2 ? static_cast<uint32_t>(data[i + 2]) : 0u);
    out[at++] = BASE64[(group >> 18) & 63];
    out[at++] = BASE64[(group >> 12) & 63];
    out[at++] = left > 1 ? BASE64[(group >> 6) & 63] : '=';
    out[at++] = left > 2 ? BASE64[group & 63] : '=';
  }
  return at;
}

bool base64Decode(const char* text, const size_t length, uint8_t* out, const size_t capacity, size_t& outLength) {
  if (!text || length == 0 || length % 4 != 0) return false;
  size_t at = 0;
  for (size_t i = 0; i < length; i += 4) {
    const bool last = i + 4 == length;
    const int a = base64Value(text[i]);
    const int b = base64Value(text[i + 1]);
    const bool padTwo = last && text[i + 2] == '=' && text[i + 3] == '=';
    const bool padOne = last && !padTwo && text[i + 3] == '=';
    const int c = padTwo ? 0 : base64Value(text[i + 2]);
    const int d = (padTwo || padOne) ? 0 : base64Value(text[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0) return false;
    // The bits that padding leaves unused must be zero, so one text stands for one content.
    if ((padTwo && (b & 15) != 0) || (padOne && (c & 3) != 0)) return false;
    const uint32_t group = (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12) |
                           (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);
    const size_t bytes = padTwo ? 1 : padOne ? 2 : 3;
    if (at + bytes > capacity) return false;
    out[at++] = static_cast<uint8_t>(group >> 16);
    if (bytes > 1) out[at++] = static_cast<uint8_t>(group >> 8);
    if (bytes > 2) out[at++] = static_cast<uint8_t>(group);
  }
  outLength = at;
  return true;
}

bool parseWaitArgs(const char* args, const size_t length, WaitArgs& out) {
  out = WaitArgs{};
  if (!args || length == 0) return true;
  const char* end = args + length;
  while (end > args && isBlank(end[-1])) --end;
  while (args < end && isBlank(*args)) ++args;
  if (args == end) return true;
  // The last word, when it is a number, is the time.
  const char* last = end;
  while (last > args && !isBlank(last[-1])) --last;
  bool digits = true;
  for (const char* at = last; at < end; ++at) digits = digits && *at >= '0' && *at <= '9';
  if (digits) {
    uint32_t value = 0;
    if (!parseUnsigned(last, static_cast<size_t>(end - last), value) || value < MIN_WAIT_MS || value > MAX_WAIT_MS) {
      return false;
    }
    out.timeoutMs = value;
    end = last;
    while (end > args && isBlank(end[-1])) --end;
  }
  const size_t nameLength = static_cast<size_t>(end - args);
  if (nameLength > MAX_ACTIVITY_NAME) return false;
  memcpy(out.activity, args, nameLength);
  out.activity[nameLength] = '\0';
  return true;
}

void WaitGate::start(const uint32_t nowMs, const uint32_t timeout) {
  startMs = nowMs;
  lastMs = nowMs;
  timeoutMs = timeout;
  restSinceMs = nowMs;
  restPasses = 0;
  active = true;
}

WaitGate::Result WaitGate::tick(const uint32_t nowMs, const bool atRest) {
  if (!active) return Result::Idle;
  lastMs = nowMs;
  // Unsigned subtraction: a wrapped clock still gives the gap.
  if (atRest) {
    if (restPasses == 0) restSinceMs = nowMs;
    ++restPasses;
    if (restPasses >= SETTLE_PASSES && nowMs - restSinceMs >= SETTLE_MS) {
      active = false;
      return Result::Done;
    }
  } else {
    restPasses = 0;
  }
  if (nowMs - startMs >= timeoutMs) {
    active = false;
    return Result::TimedOut;
  }
  return Result::Pending;
}

uint32_t crc32(const uint8_t* data, const size_t length, const uint32_t crc) {
  // Bit by bit, no table: 48 KB of framebuffer is a few milliseconds, and a table is 1 KB.
  uint32_t value = ~crc;
  for (size_t i = 0; i < length; ++i) {
    value ^= data[i];
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xEDB88320u & (0u - (value & 1u)));
  }
  return ~value;
}

}  // namespace devlink

#endif  // VAULTINK_DEVLINK_FULL
