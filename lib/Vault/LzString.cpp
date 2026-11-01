#include "LzString.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace lzstring {
namespace {

constexpr uint32_t RESET_VALUE = 32;  // base64: six bits per character, read from bit 5 down

// Value of a base64 alphabet character (ABC..xyz012..9+/=), -1 for whitespace, -2 for anything else.
int baseValue(const char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  if (c == '=') return 64;  // bit 6: never read, so it reads as zeros like in lz-string
  if (c == ' ' || c == '\n' || c == '\r' || c == '\t') return -1;
  return -2;
}

// The bit reader of lz-string's _decompress: `val` holds the current character's value, `position`
// the bit mask being read, `index` the next character. Past the end lz-string reads NaN, which
// behaves as zero bits; the caller checks `index > length` once per token as the library does.
class Bits {
 public:
  explicit Bits(const std::string_view in) : in(in) {}

  // Strips whitespace and validates the alphabet; false on a bad character or an oversized input.
  bool prepare() {
    if (std::any_of(in.begin(), in.end(), [](const char c) { return baseValue(c) == -2; })) return false;
    length = static_cast<size_t>(std::count_if(in.begin(), in.end(), [](const char c) { return baseValue(c) >= 0; }));
    if (length > MAX_INPUT_CHARS) return false;
    val = nextValue();
    index = 1;
    return true;
  }

  uint32_t read(const int numBits) {
    uint32_t bits = 0;
    for (int i = 0; i < numBits; ++i) {
      const uint32_t resb = val & position;
      position >>= 1;
      if (position == 0) {
        position = RESET_VALUE;
        val = nextValue();
        ++index;
      }
      if (resb) bits |= 1u << i;
    }
    return bits;
  }

  bool overrun() const { return index > length; }

 private:
  // The next base64 character's value, skipping whitespace; zero past the end.
  uint32_t nextValue() {
    while (pos < in.size()) {
      const int v = baseValue(in[pos++]);
      if (v >= 0) return static_cast<uint32_t>(v);
    }
    return 0;
  }

  std::string_view in;
  size_t pos = 0;
  size_t length = 0;
  size_t index = 0;
  uint32_t val = 0;
  uint32_t position = RESET_VALUE;
};

// Dictionary entries as compact arrays: entry i is the string of entry `parent[i]` followed by
// `last[i]`, and starts with `first[i]`. Entries 0..2 are lz-string's control codes.
struct Dictionary {
  std::unique_ptr<uint16_t[]> parent;
  std::unique_ptr<uint16_t[]> last;
  std::unique_ptr<uint16_t[]> first;
  uint32_t size = 4;  // entries 0..3 exist after the first character

  bool allocate() {
    parent.reset(new (std::nothrow) uint16_t[MAX_DICT]);
    last.reset(new (std::nothrow) uint16_t[MAX_DICT]);
    first.reset(new (std::nothrow) uint16_t[MAX_DICT]);
    return parent && last && first;
  }

  bool add(const uint32_t parentIndex, const uint16_t lastChar, const uint16_t firstChar) {
    if (size >= MAX_DICT) return false;
    parent[size] = static_cast<uint16_t>(parentIndex);
    last[size] = lastChar;
    first[size] = firstChar;
    ++size;
    return true;
  }

  void setSingle(const uint32_t index, const uint16_t ch) {
    parent[index] = index;  // a single character is its own root
    last[index] = ch;
    first[index] = ch;
  }
};

// UTF-16 code units to UTF-8, joining surrogate pairs; bounded by MAX_OUTPUT.
class Utf8Sink {
 public:
  explicit Utf8Sink(std::string& out) : out(out) {}

  bool put(const uint16_t unit) {
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      const bool ok = high == 0 || append(0xFFFD);
      high = unit;
      return ok;
    }
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
      if (high == 0) return append(0xFFFD);
      const uint32_t cp = 0x10000 + ((static_cast<uint32_t>(high) - 0xD800) << 10) + (unit - 0xDC00);
      high = 0;
      return append(cp);
    }
    if (high != 0) {
      high = 0;
      if (!append(0xFFFD)) return false;
    }
    return append(unit);
  }

  bool finish() {
    if (high == 0) return true;
    high = 0;
    return append(0xFFFD);
  }

 private:
  bool append(const uint32_t cp) {
    if (out.size() + 4 > MAX_OUTPUT) return false;
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return true;
  }

  std::string& out;
  uint16_t high = 0;
};

class Decoder {
 public:
  Decoder(const std::string_view in, std::string& out) : bits(in), sink(out) {}

  bool run() {
    if (!bits.prepare() || !dict.allocate()) return false;
    scratch.reserve(1024);
    if (scratch.capacity() < 1024) return false;

    // First character: a 2-bit code, 0 = 8-bit char, 1 = 16-bit char, 2 = end.
    uint16_t c = 0;
    const uint32_t first = bits.read(2);
    if (first == 3) return false;          // not a valid opening code
    if (!readChar(first, c)) return true;  // code 2: an empty string
    dict.setSingle(3, c);
    uint32_t w = 3;
    if (!sink.put(c)) return false;

    uint32_t enlargeIn = 4;
    int numBits = 3;
    while (true) {
      if (bits.overrun()) return false;
      uint32_t code = bits.read(numBits);
      if (code < 2) {
        uint16_t ch = 0;
        readChar(code, ch);
        if (dict.size >= MAX_DICT) return false;
        dict.setSingle(dict.size, ch);
        code = dict.size++;
        --enlargeIn;
      } else if (code == 2) {
        return sink.finish();
      }
      if (enlargeIn == 0) {
        enlargeIn = 1u << numBits;
        ++numBits;
      }

      uint16_t entryFirst = 0;
      if (code < dict.size) {
        if (!emit(code)) return false;
        entryFirst = dict.first[code];
      } else if (code == dict.size) {
        // The entry being defined: w followed by its own first character.
        if (!emit(w) || !sink.put(dict.first[w])) return false;
        entryFirst = dict.first[w];
      } else {
        return false;
      }
      if (!dict.add(w, entryFirst, dict.first[w])) return false;
      --enlargeIn;
      w = code;
      if (enlargeIn == 0) {
        enlargeIn = 1u << numBits;
        ++numBits;
      }
    }
  }

 private:
  // Reads a literal character after code 0 (8 bits) or 1 (16 bits); false for code 2 (end).
  bool readChar(const uint32_t code, uint16_t& out) {
    if (code == 0) {
      out = static_cast<uint16_t>(bits.read(8));
      return true;
    }
    if (code == 1) {
      out = static_cast<uint16_t>(bits.read(16));
      return true;
    }
    return false;
  }

  // Writes dictionary entry `index` by walking its parent chain.
  bool emit(uint32_t index) {
    scratch.clear();
    while (true) {
      if (scratch.size() * 2 >= MAX_OUTPUT) return false;  // longer than any output could be
      scratch.push_back(dict.last[index]);
      if (dict.parent[index] == index) break;
      index = dict.parent[index];
    }
    return std::all_of(scratch.rbegin(), scratch.rend(), [this](const uint16_t unit) { return sink.put(unit); });
  }

  Bits bits;
  Utf8Sink sink;
  Dictionary dict;
  std::vector<uint16_t> scratch;
};

}  // namespace

bool decompressFromBase64(const std::string_view base64, std::string& out) {
  out.clear();
  Decoder decoder(base64, out);
  if (decoder.run()) return true;
  out.clear();
  return false;
}

}  // namespace lzstring
