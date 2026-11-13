#include "DevLink.h"

#include <cstdio>
#include <cstring>

namespace devlink {
namespace {

bool isBlank(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

struct Name {
  const char* text;
  Command command;
};

constexpr Name NAMES[] = {
    {"SCREENSHOT", Command::Screenshot},
    {"STATUS", Command::Status},
    {"LOG", Command::Log},
    {"REBOOT", Command::Reboot},
    {"BOOTLOADER", Command::Bootloader},
    {"PING", Command::Ping},
    {"HELP", Command::Help},
};

// One slot is kept for the closing brace and one for the terminator.
constexpr size_t FIELD_LIMIT = JsonLine::CAPACITY - 2;

}  // namespace

bool LineAssembler::feed(const char c, const uint32_t nowMs) {
  if (ready) {
    ready = false;
    used = 0;
  }
  // Unsigned subtraction: a wrapped clock still gives the gap. A caller that passes no time (0) never expires.
  if (nowMs != 0 && used > 0 && nowMs - lastByteMs > IDLE_DROP_MS) {
    used = 0;
    skipping = false;
  }
  if (nowMs != 0) lastByteMs = nowMs;
  if (c == '\r') return false;
  if (c == '\n') {
    if (skipping) {
      skipping = false;
      used = 0;
      return false;
    }
    buffer[used] = '\0';
    ready = true;
    return true;
  }
  if (skipping) return false;
  if (used >= MAX_LINE) {
    skipping = true;
    used = 0;
    ++droppedLines;
    return false;
  }
  buffer[used++] = c;
  return false;
}

Parsed parse(const char* line, size_t length) {
  Parsed parsed;
  static constexpr char PREFIX[] = "CMD:";
  constexpr size_t prefixLength = sizeof(PREFIX) - 1;
  if (!line || length < prefixLength || memcmp(line, PREFIX, prefixLength) != 0) return parsed;

  const char* at = line + prefixLength;
  const char* end = line + length;
  while (at < end && isBlank(*at)) ++at;
  while (end > at && isBlank(end[-1])) --end;

  const char* nameEnd = at;
  while (nameEnd < end && !isBlank(*nameEnd)) ++nameEnd;
  const size_t nameLength = static_cast<size_t>(nameEnd - at);

  parsed.command = Command::Unknown;
  for (const Name& name : NAMES) {
    if (strlen(name.text) == nameLength && memcmp(name.text, at, nameLength) == 0) {
      parsed.command = name.command;
      break;
    }
  }

  const char* args = nameEnd;
  while (args < end && isBlank(*args)) ++args;
  parsed.args = args;
  parsed.argsLength = static_cast<size_t>(end - args);
  return parsed;
}

const char* commandList() { return "PING HELP STATUS LOG REBOOT BOOTLOADER SCREENSHOT"; }

bool parseUnsigned(const char* text, const size_t length, uint32_t& value) {
  if (!text || length == 0 || length > 10) return false;
  uint64_t total = 0;
  for (size_t i = 0; i < length; ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    total = total * 10 + static_cast<uint64_t>(text[i] - '0');
  }
  if (total > UINT32_MAX) return false;
  value = static_cast<uint32_t>(total);
  return true;
}

JsonLine::JsonLine() {
  buffer[0] = '{';
  buffer[1] = '\0';
  used = 1;
}

bool JsonLine::put(const char c) {
  if (used >= FIELD_LIMIT) return false;
  buffer[used++] = c;
  return true;
}

bool JsonLine::putText(const char* text) {
  for (; *text; ++text) {
    if (!put(*text)) return false;
  }
  return true;
}

bool JsonLine::putEscaped(const char* text) {
  if (!put('"')) return false;
  for (; text && *text; ++text) {
    const unsigned char c = static_cast<unsigned char>(*text);
    bool ok = true;
    if (c == '"' || c == '\\') {
      ok = put('\\') && put(static_cast<char>(c));
    } else if (c == '\n') {
      ok = putText("\\n");
    } else if (c == '\r') {
      ok = putText("\\r");
    } else if (c == '\t') {
      ok = putText("\\t");
    } else if (c < 0x20) {
      char code[8];
      snprintf(code, sizeof(code), "\\u%04x", static_cast<unsigned>(c));
      ok = putText(code);
    } else {
      ok = put(static_cast<char>(c));
    }
    if (!ok) return false;
  }
  return put('"');
}

bool JsonLine::startField(const char* key) {
  if (!first && !put(',')) return false;
  return putEscaped(key) && put(':');
}

JsonLine& JsonLine::addString(const char* key, const char* value) {
  if (overflow) return *this;
  const size_t mark = used;
  if (startField(key) && putEscaped(value)) {
    first = false;
  } else {
    used = mark;
    overflow = true;
  }
  return *this;
}

JsonLine& JsonLine::addBool(const char* key, const bool value) {
  if (overflow) return *this;
  const size_t mark = used;
  if (startField(key) && putText(value ? "true" : "false")) {
    first = false;
  } else {
    used = mark;
    overflow = true;
  }
  return *this;
}

JsonLine& JsonLine::addInt(const char* key, const long long value) {
  if (overflow) return *this;
  char number[24];
  snprintf(number, sizeof(number), "%lld", value);
  const size_t mark = used;
  if (startField(key) && putText(number)) {
    first = false;
  } else {
    used = mark;
    overflow = true;
  }
  return *this;
}

JsonLine& JsonLine::addUnsigned(const char* key, const unsigned long long value) {
  if (overflow) return *this;
  char number[24];
  snprintf(number, sizeof(number), "%llu", value);
  const size_t mark = used;
  if (startField(key) && putText(number)) {
    first = false;
  } else {
    used = mark;
    overflow = true;
  }
  return *this;
}

const char* JsonLine::finish() {
  if (overflow) {
    static constexpr char TOO_LONG[] = "{\"ok\":false,\"error\":\"reply too long\"}";
    memcpy(buffer, TOO_LONG, sizeof(TOO_LONG));
    used = sizeof(TOO_LONG) - 1;
    return buffer;
  }
  buffer[used++] = '}';
  buffer[used] = '\0';
  return buffer;
}

}  // namespace devlink
