#include "ReadingPresets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace readingpresets {

namespace {

// The numeric fields, by JSON key. The name and the font family name are strings and handled apart.
struct Field {
  const char* key;
  uint8_t Layout::* member;
};

constexpr Field kFields[] = {
    {"font", &Layout::fontFamily},
    {"size", &Layout::fontPointSize},
    {"spacing", &Layout::lineSpacing},
    {"align", &Layout::paragraphAlignment},
    {"hyphen", &Layout::hyphenationEnabled},
    {"focus", &Layout::focusReadingEnabled},
    {"gaps", &Layout::evenWordGaps},
    {"word", &Layout::wordSpacing},
    {"char", &Layout::characterSpacing},
    {"margin", &Layout::screenMargin},
    {"indent", &Layout::paragraphIndentSpaces},
    {"lig", &Layout::textLigatures},
    {"kern", &Layout::textKerning},
    {"widow", &Layout::textWidowOrphan},
    {"headings", &Layout::textKeepHeadings},
    {"hanging", &Layout::textHanging},
};

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Cuts a UTF-8 string to at most `maxBytes` on a code point boundary, returning the new length.
size_t utf8Cut(const char* s, size_t len, size_t maxBytes) {
  if (len <= maxBytes) return len;
  size_t cut = maxBytes;
  while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
  return cut;
}

// --- JSON writer -------------------------------------------------------------------------------

struct Writer {
  char* buf;
  size_t cap;
  size_t need = 0;

  void put(const char* fmt, auto... args) {
    const size_t room = need < cap ? cap - need : 0;
    const int n = std::snprintf(room > 0 ? buf + need : nullptr, room, fmt, args...);
    if (n > 0) need += static_cast<size_t>(n);
  }
  void putString(const char* s) {
    put("\"");
    for (; *s != '\0'; ++s) {
      const unsigned char c = static_cast<unsigned char>(*s);
      if (c == '"' || c == '\\') {
        put("\\%c", *s);
      } else if (c < 0x20) {
        put("\\u%04x", static_cast<unsigned>(c));
      } else {
        put("%c", *s);
      }
    }
    put("\"");
  }
};

// --- JSON reader -------------------------------------------------------------------------------
// Just enough JSON for the file this module writes: objects, arrays, strings with the usual escapes,
// integers, true/false/null. Anything it does not understand is skipped as a value.

struct Reader {
  const char* p;
  const char* end;

  void skipSpace() {
    while (p < end && isSpace(*p)) ++p;
  }
  bool peek(const char c) {
    skipSpace();
    return p < end && *p == c;
  }
  bool take(const char c) {
    if (!peek(c)) return false;
    ++p;
    return true;
  }

  static void appendUtf8(char* out, size_t cap, size_t& len, uint32_t cp) {
    char tmp[4];
    size_t n = 0;
    if (cp < 0x80) {
      tmp[n++] = static_cast<char>(cp);
    } else if (cp < 0x800) {
      tmp[n++] = static_cast<char>(0xC0 | (cp >> 6));
      tmp[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      tmp[n++] = static_cast<char>(0xE0 | (cp >> 12));
      tmp[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      tmp[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      tmp[n++] = static_cast<char>(0xF0 | (cp >> 18));
      tmp[n++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      tmp[n++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      tmp[n++] = static_cast<char>(0x80 | (cp & 0x3F));
    }
    if (out != nullptr && len + n < cap) {
      std::memcpy(out + len, tmp, n);
      len += n;
    } else if (out == nullptr) {
      len += n;
    }
  }

  static int hexValue(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  // Reads a string into out (cap includes the NUL; what does not fit is dropped). out may be null to skip.
  bool readString(char* out, size_t cap) {
    if (!take('"')) return false;
    size_t len = 0;
    while (p < end && *p != '"') {
      char c = *p++;
      if (c == '\\') {
        if (p >= end) return false;
        c = *p++;
        switch (c) {
          case 'n':
            c = '\n';
            break;
          case 't':
            c = '\t';
            break;
          case 'r':
            c = '\r';
            break;
          case 'b':
            c = '\b';
            break;
          case 'f':
            c = '\f';
            break;
          case 'u': {
            if (end - p < 4) return false;
            uint32_t cp = 0;
            for (int i = 0; i < 4; ++i) {
              const int h = hexValue(p[i]);
              if (h < 0) return false;
              cp = (cp << 4) | static_cast<uint32_t>(h);
            }
            p += 4;
            // A surrogate pair comes as two escapes.
            if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
              uint32_t lo = 0;
              bool ok = true;
              for (int i = 0; i < 4; ++i) {
                const int h = hexValue(p[2 + i]);
                if (h < 0) ok = false;
                lo = (lo << 4) | static_cast<uint32_t>(h < 0 ? 0 : h);
              }
              if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                p += 6;
              }
            }
            appendUtf8(out, cap, len, cp);
            continue;
          }
          default:
            break;  // \" \\ \/ and unknown escapes: the character itself
        }
      }
      if (out != nullptr && len + 1 < cap) out[len++] = c;
    }
    if (p >= end) return false;
    ++p;  // closing quote
    if (out != nullptr && cap > 0) {
      // The buffer may have filled mid-sequence: drop a trailing, incomplete UTF-8 sequence.
      size_t lead = len;
      while (lead > 0 && (static_cast<unsigned char>(out[lead - 1]) & 0xC0) == 0x80) --lead;
      if (lead > 0) {
        const unsigned char first = static_cast<unsigned char>(out[lead - 1]);
        const size_t need = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC0 ? 2 : 1;
        if (need > 1 && len - (lead - 1) < need) len = lead - 1;
      }
      out[len] = '\0';
    }
    return true;
  }

  // Reads an integer; non-numbers (true/false/null) give false with `value` untouched.
  bool readNumber(long& value) {
    skipSpace();
    const char* start = p;
    bool negative = false;
    if (p < end && (*p == '-' || *p == '+')) {
      negative = *p == '-';
      ++p;
    }
    long v = 0;
    bool digits = false;
    while (p < end && *p >= '0' && *p <= '9') {
      if (v < 100000000L) v = v * 10 + (*p - '0');
      ++p;
      digits = true;
    }
    if (!digits) {
      p = start;
      return false;
    }
    // Drop a fraction or exponent; the fields are whole numbers.
    while (p < end && (*p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+' || (*p >= '0' && *p <= '9'))) {
      ++p;
    }
    value = negative ? -v : v;
    return true;
  }

  bool readBool(bool& value) {
    skipSpace();
    if (end - p >= 4 && std::memcmp(p, "true", 4) == 0) {
      p += 4;
      value = true;
      return true;
    }
    if (end - p >= 5 && std::memcmp(p, "false", 5) == 0) {
      p += 5;
      value = false;
      return true;
    }
    return false;
  }

  // Skips any value. False when the text runs out or is not JSON.
  bool skipValue(int depth = 0) {
    skipSpace();
    if (p >= end || depth > 16) return false;
    if (*p == '"') return readString(nullptr, 0);
    if (*p == '{' || *p == '[') {
      const char close = *p == '{' ? '}' : ']';
      ++p;
      if (take(close)) return true;
      for (;;) {
        if (close == '}') {
          if (!readString(nullptr, 0) || !take(':')) return false;
        }
        if (!skipValue(depth + 1)) return false;
        if (take(close)) return true;
        if (!take(',')) return false;
      }
    }
    // number / true / false / null: run to the next delimiter
    while (p < end && *p != ',' && *p != '}' && *p != ']' && !isSpace(*p)) ++p;
    return true;
  }

  bool readPreset(Preset& preset) {
    if (!take('{')) return false;
    if (take('}')) return true;
    for (;;) {
      char key[16];
      if (!readString(key, sizeof(key)) || !take(':')) return false;
      bool handled = false;
      if (std::strcmp(key, "name") == 0) {
        char name[kNameCapacity];
        if (!readString(name, sizeof(name))) return false;
        if (name[0] != '\0') std::memcpy(preset.name, name, sizeof(name));
        handled = true;
      } else if (std::strcmp(key, "saved") == 0) {
        handled = readBool(preset.saved);
      } else if (std::strcmp(key, "sdFont") == 0) {
        if (!readString(preset.layout.sdFontFamilyName, sizeof(preset.layout.sdFontFamilyName))) return false;
        handled = true;
      } else {
        for (const Field& f : kFields) {
          if (std::strcmp(key, f.key) != 0) continue;
          long v = 0;
          if (readNumber(v)) {
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            preset.layout.*(f.member) = static_cast<uint8_t>(v);
            handled = true;
          }
          break;
        }
      }
      if (!handled && !skipValue()) return false;
      if (take('}')) return true;
      if (!take(',')) return false;
    }
  }

  bool readSlots(PresetList& out) {
    if (!take('[')) return false;
    if (take(']')) return true;
    int slot = 0;
    for (;;) {
      if (slot < kSlots) {
        if (!readPreset(out.slots[slot])) return false;
      } else if (!skipValue()) {
        return false;
      }
      ++slot;
      if (take(']')) return true;
      if (!take(',')) return false;
    }
  }
};

}  // namespace

bool Layout::operator==(const Layout& other) const {
  if (std::strcmp(sdFontFamilyName, other.sdFontFamilyName) != 0) return false;
  return std::all_of(std::begin(kFields), std::end(kFields),
                     [this, &other](const Field& f) { return this->*(f.member) == other.*(f.member); });
}

void PresetList::reset() {
  for (int i = 0; i < kSlots; ++i) {
    slots[i] = Preset{};
    defaultName(i, slots[i].name, sizeof(slots[i].name));
  }
}

int PresetList::find(const Layout& layout) const {
  for (int i = 0; i < kSlots; ++i) {
    if (slots[i].saved && slots[i].layout == layout) return i;
  }
  return -1;
}

void defaultName(const int slot, char* out, const size_t cap) {
  if (cap == 0) return;
  std::snprintf(out, cap, "Preset %d", slot + 1);
}

bool setName(PresetList& list, const int slot, const char* name) {
  if (slot < 0 || slot >= kSlots) return false;
  char clean[kNameCapacity];
  size_t len = 0;
  if (name != nullptr) {
    const char* s = name;
    while (*s != '\0' && isSpace(*s)) ++s;
    size_t srcLen = std::strlen(s);
    while (srcLen > 0 && isSpace(s[srcLen - 1])) --srcLen;
    len = utf8Cut(s, srcLen, kNameCapacity - 1);
    for (size_t i = 0; i < len; ++i) {
      const unsigned char c = static_cast<unsigned char>(s[i]);
      clean[i] = c < 0x20 || c == 0x7F ? ' ' : s[i];
    }
  }
  clean[len] = '\0';
  if (len == 0) {
    defaultName(slot, list.slots[slot].name, sizeof(list.slots[slot].name));
  } else {
    std::memcpy(list.slots[slot].name, clean, len + 1);
  }
  return true;
}

bool save(PresetList& list, const int slot, const Layout& layout) {
  if (slot < 0 || slot >= kSlots) return false;
  list.slots[slot].layout = layout;
  list.slots[slot].saved = true;
  return true;
}

bool clear(PresetList& list, const int slot) {
  if (slot < 0 || slot >= kSlots) return false;
  list.slots[slot].saved = false;
  list.slots[slot].layout = Layout{};
  return true;
}

size_t serialize(const PresetList& list, char* buf, const size_t cap) {
  Writer w{buf, cap};
  if (cap > 0) buf[0] = '\0';
  w.put("{\"v\":%u,\"slots\":[", static_cast<unsigned>(kFileVersion));
  for (int i = 0; i < kSlots; ++i) {
    const Preset& preset = list.slots[i];
    w.put(i == 0 ? "{\"name\":" : ",{\"name\":");
    w.putString(preset.name);
    w.put(",\"saved\":%s", preset.saved ? "true" : "false");
    if (preset.saved) {
      w.put(",\"sdFont\":");
      w.putString(preset.layout.sdFontFamilyName);
      for (const Field& f : kFields) {
        w.put(",\"%s\":%u", f.key, static_cast<unsigned>(preset.layout.*(f.member)));
      }
    }
    w.put("}");
  }
  w.put("]}");
  if (cap > 0 && w.need >= cap) buf[cap - 1] = '\0';
  return w.need;
}

bool parse(const char* data, const size_t size, PresetList& out) {
  out.reset();
  if (data == nullptr) return false;
  Reader r{data, data + size};
  if (!r.take('{')) return false;
  if (r.take('}')) return true;
  for (;;) {
    char key[16];
    if (!r.readString(key, sizeof(key)) || !r.take(':')) {
      out.reset();
      return false;
    }
    bool ok = true;
    if (std::strcmp(key, "slots") == 0) {
      ok = r.readSlots(out);
    } else {
      ok = r.skipValue();
    }
    if (!ok) {
      out.reset();
      return false;
    }
    if (r.take('}')) break;
    if (!r.take(',')) {
      out.reset();
      return false;
    }
  }
  // An unsaved slot carries no values; a slot that came with a blank name gets its default back.
  for (int i = 0; i < kSlots; ++i) {
    if (!out.slots[i].saved) out.slots[i].layout = Layout{};
    if (out.slots[i].name[0] == '\0') defaultName(i, out.slots[i].name, sizeof(out.slots[i].name));
  }
  return true;
}

}  // namespace readingpresets
