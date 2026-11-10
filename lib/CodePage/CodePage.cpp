#include "CodePage.h"

namespace codepage {
namespace {

// 0x80-0xBF of Windows-1251; 0xC0-0xFF is U+0410 onwards.
constexpr uint16_t CP1251_LOW[64] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
    0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0000, 0x2122,
    0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
    0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
    0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
};
// 0x80-0x9F of Windows-1252; 0xA0-0xFF is Latin-1.
constexpr uint16_t CP1252_LOW[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x0000, 0x017D, 0x0000, 0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
};
// 0x80-0xBF of Windows-1253; 0xC0-0xFF is U+0390 onwards (0xD2 and 0xFF undefined), shared with ISO-8859-7.
constexpr uint16_t CP1253_LOW[64] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x0000, 0x2030, 0x0000, 0x2039, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0000, 0x2122,
    0x0000, 0x203A, 0x0000, 0x0000, 0x0000, 0x0000, 0x00A0, 0x0385, 0x0386, 0x00A3, 0x00A4, 0x00A5, 0x00A6,
    0x00A7, 0x00A8, 0x00A9, 0x0000, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x2015, 0x00B0, 0x00B1, 0x00B2, 0x00B3,
    0x0384, 0x00B5, 0x00B6, 0x00B7, 0x0388, 0x0389, 0x038A, 0x00BB, 0x038C, 0x00BD, 0x038E, 0x038F,
};
// 0xA0-0xBF of ISO-8859-7.
constexpr uint16_t ISO8859_7_A0[32] = {
    0x00A0, 0x2018, 0x2019, 0x00A3, 0x20AC, 0x20AF, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x037A,
    0x00AB, 0x00AC, 0x00AD, 0x0000, 0x2015, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x0384, 0x0385,
    0x0386, 0x00B7, 0x0388, 0x0389, 0x038A, 0x00BB, 0x038C, 0x00BD, 0x038E, 0x038F,
};

uint16_t greekHigh(const uint8_t byte) {
  return byte == 0xD2 || byte == 0xFF ? 0 : static_cast<uint16_t>(0x0390 + (byte - 0xC0));
}

// Lowercased letters and digits only, so "Windows-1253", "windows_1253" and "WINDOWS1253" are one name.
size_t normalise(std::string_view name, char* out, const size_t cap) {
  size_t n = 0;
  for (char c : name) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) continue;
    if (n == cap) return 0;  // longer than any name known here
    out[n++] = c;
  }
  return n;
}

}  // namespace

bool fromName(const std::string_view name, Id& id) {
  char buf[16];
  const std::string_view n(buf, normalise(name, buf, sizeof(buf)));
  struct Name {
    const char* name;
    Id id;
  };
  static constexpr Name NAMES[] = {
      {"utf8", Id::Utf8},       {"usascii", Id::Utf8},       {"ascii", Id::Utf8},        {"windows1251", Id::Cp1251},
      {"cp1251", Id::Cp1251},   {"windows1252", Id::Cp1252}, {"cp1252", Id::Cp1252},     {"windows1253", Id::Cp1253},
      {"cp1253", Id::Cp1253},   {"iso88591", Id::Iso8859_1}, {"latin1", Id::Iso8859_1},  {"iso88597", Id::Iso8859_7},
      {"greek", Id::Iso8859_7}, {"elot928", Id::Iso8859_7},  {"ecma118", Id::Iso8859_7},
  };
  for (const Name& entry : NAMES) {
    if (n == entry.name) {
      id = entry.id;
      return true;
    }
  }
  return false;
}

bool fromWindowsNumber(const int number, Id& id) {
  switch (number) {
    case 1251:
      id = Id::Cp1251;
      return true;
    case 1252:
      id = Id::Cp1252;
      return true;
    case 1253:
      id = Id::Cp1253;
      return true;
    case 28591:
      id = Id::Iso8859_1;
      return true;
    case 28597:
      id = Id::Iso8859_7;
      return true;
    case 65001:
      id = Id::Utf8;
      return true;
    default:
      return false;
  }
}

bool fromRtfCharset(const int charset, Id& id) {
  if (charset == 161) {
    id = Id::Cp1253;
    return true;
  }
  if (charset == 204) {
    id = Id::Cp1251;
    return true;
  }
  return false;
}

uint16_t toUnicode(const Id id, const uint8_t byte) {
  if (id == Id::Utf8) return 0;
  if (byte < 0x80) return byte;
  switch (id) {
    case Id::Cp1251:
      return byte < 0xC0 ? CP1251_LOW[byte - 0x80] : static_cast<uint16_t>(0x0410 + (byte - 0xC0));
    case Id::Cp1252:
      return byte < 0xA0 ? CP1252_LOW[byte - 0x80] : byte;
    case Id::Cp1253:
      return byte < 0xC0 ? CP1253_LOW[byte - 0x80] : greekHigh(byte);
    case Id::Iso8859_1:
      return byte < 0xA0 ? 0 : byte;
    case Id::Iso8859_7:
      return byte < 0xA0 ? 0 : byte < 0xC0 ? ISO8859_7_A0[byte - 0xA0] : greekHigh(byte);
    default:
      return 0;
  }
}

size_t encodeUtf8(const uint32_t cp, char out[4]) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  if (cp > 0x10FFFF) return 0;
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

size_t Decoder::single(const Id id, const uint8_t byte, char* out) {
  const uint32_t cp = toUnicode(id, byte);
  if (cp == 0 || !xmlChar(cp)) return 0;  // undefined in the page, or a control character
  return encodeUtf8(cp, out);
}

size_t Decoder::flushPending(char* out) {
  size_t n = 0;
  for (uint8_t i = 0; i < pendingLen_; i++) n += single(Id::Cp1252, pending_[i], out + n);
  pendingLen_ = 0;
  need_ = 0;
  return n;
}

size_t Decoder::feed(const uint8_t* in, const size_t size, char* out) {
  size_t n = 0;
  if (id_ != Id::Utf8) {
    for (size_t i = 0; i < size; i++) n += single(id_, in[i], out + n);
    return n;
  }
  for (size_t i = 0; i < size; i++) {
    const uint8_t b = in[i];
    if (need_ > 0) {
      if ((b & 0xC0) == 0x80) {
        pending_[pendingLen_++] = b;
        if (--need_ > 0) continue;
        uint32_t cp = pending_[0] & (pendingLen_ == 2 ? 0x1F : pendingLen_ == 3 ? 0x0F : 0x07);
        for (uint8_t k = 1; k < pendingLen_; k++) cp = (cp << 6) | (pending_[k] & 0x3F);
        const uint32_t least = pendingLen_ == 2 ? 0x80 : pendingLen_ == 3 ? 0x800 : 0x10000;
        if (cp >= least && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
          if (xmlChar(cp)) {  // U+FFFE and U+FFFF are dropped
            for (uint8_t k = 0; k < pendingLen_; k++) out[n++] = static_cast<char>(pending_[k]);
          }
          pendingLen_ = 0;
        } else {
          n += flushPending(out + n);  // overlong or out of range: not UTF-8
        }
        continue;
      }
      n += flushPending(out + n);  // cut short: the lead byte was not UTF-8 after all; `b` is looked at afresh
    }
    if (b < 0x80) {
      if (xmlChar(b)) out[n++] = static_cast<char>(b);
    } else if (b >= 0xC2 && b <= 0xF4) {
      pending_[0] = b;
      pendingLen_ = 1;
      need_ = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
    } else {
      n += single(Id::Cp1252, b, out + n);
    }
  }
  return n;
}

size_t Decoder::finish(char* out) { return flushPending(out); }

}  // namespace codepage
