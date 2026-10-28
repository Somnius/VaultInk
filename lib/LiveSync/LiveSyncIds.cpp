#include "LiveSyncIds.h"

#include <mbedtls/md.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace livesync {
namespace {

// UTF-8 to UTF-16 code units, as JavaScript strings hold them.
std::vector<uint16_t> toUtf16(std::string_view s) {
  std::vector<uint16_t> out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    uint32_t cp;
    size_t n;
    if (c < 0x80) {
      cp = c, n = 1;
    } else if ((c >> 5) == 6 && i + 1 < s.size()) {
      cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F), n = 2;
    } else if ((c >> 4) == 14 && i + 2 < s.size()) {
      cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F), n = 3;
    } else if ((c >> 3) == 30 && i + 3 < s.size()) {
      cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F), n = 4;
    } else {
      cp = 0xFFFD, n = 1;
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<uint16_t>(cp));
    }
    i += n;
  }
  return out;
}

std::string fromUtf16(const std::vector<uint16_t>& u, size_t count) {
  std::string out;
  for (size_t i = 0; i < count && i < u.size(); ++i) {
    uint32_t cp = u[i];
    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < count && i + 1 < u.size()) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (u[i + 1] - 0xDC00);
      ++i;
    }
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
  }
  return out;
}

// JavaScript's ToInt32 of a Number (the value of a double, not of a C cast).
int32_t jsInt32(const double d) {
  if (!std::isfinite(d)) return 0;
  double t = std::trunc(d);
  t = std::fmod(t, 4294967296.0);
  if (t < 0) t += 4294967296.0;
  return static_cast<int32_t>(static_cast<uint32_t>(t));
}

// mixedHash from octagonal-wheels purejs.ts, kept in doubles where the JS multiplies Numbers.
void mixedHash(const std::vector<uint16_t>& str, uint32_t& murmur, uint32_t& fnv) {
  double h1 = static_cast<int32_t>(murmur);
  uint32_t f = fnv;
  for (const uint16_t unit : str) {
    f ^= unit;
    f *= 0x01000193u;
    double k1 = static_cast<double>(unit) * static_cast<double>(0xcc9e2d51u);
    const int32_t k1i = jsInt32(k1);
    k1 = static_cast<int32_t>((static_cast<uint32_t>(k1i) << 15) | (static_cast<uint32_t>(k1i) >> 17));
    k1 = k1 * static_cast<double>(0x1b873593u);
    int32_t h = jsInt32(h1) ^ jsInt32(k1);
    h = static_cast<int32_t>((static_cast<uint32_t>(h) << 13) | (static_cast<uint32_t>(h) >> 19));
    h1 = static_cast<double>(h) * 5.0 + static_cast<double>(0xe6546b64u);
  }
  int32_t h = jsInt32(h1) ^ static_cast<int32_t>(str.size());
  uint32_t u = static_cast<uint32_t>(h);
  u ^= u >> 16;
  u *= 0x85ebca6bu;
  u ^= u >> 13;
  u *= 0xc2b2ae35u;
  u ^= u >> 16;
  murmur = u;
  fnv = f;
}

std::string base36u32(uint32_t v) { return base36(v); }

std::string sha256Hex(std::string_view s) {
  uint8_t digest[32];
  mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const unsigned char*>(s.data()), s.size(),
             digest);
  static constexpr char kHexDigits[] = "0123456789abcdef";
  std::string out;
  for (const uint8_t b : digest) {
    out += kHexDigits[b >> 4];
    out += kHexDigits[b & 15];
  }
  return out;
}

// JavaScript toLowerCase for the scripts vault names use; false when a letter is outside them.
bool lowerLikeJs(std::string_view in, std::string& out) {
  std::vector<uint16_t> u = toUtf16(in);
  for (auto& c : u) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<uint16_t>(c + 32);
    } else if (c < 0x80) {
    } else if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) {
      c = static_cast<uint16_t>(c + 32);  // Latin-1
    } else if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) {
      c = static_cast<uint16_t>(c + 32);  // Greek capitals, with dialytika
    } else if (c == 0x386) {
      c = 0x3AC;
    } else if (c >= 0x388 && c <= 0x38A) {
      c = static_cast<uint16_t>(c + 37);
    } else if (c == 0x38C) {
      c = 0x3CC;
    } else if (c == 0x38E || c == 0x38F) {
      c = static_cast<uint16_t>(c + 63);
    } else if (c >= 0x410 && c <= 0x42F) {
      c = static_cast<uint16_t>(c + 32);  // Cyrillic
    } else if (c >= 0x400 && c <= 0x40F) {
      c = static_cast<uint16_t>(c + 80);
    } else if ((c >= 0xDF && c <= 0xFF) || (c >= 0x3AC && c <= 0x3CE) || (c >= 0x430 && c <= 0x45F) ||
               (c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x9FFF) || (c >= 0xD800 && c <= 0xDFFF)) {
      // already lowercase, or a script without case (punctuation, CJK, emoji halves)
    } else {
      return false;
    }
  }
  out = fromUtf16(u, u.size());
  return true;
}

}  // namespace

size_t utf16Length(std::string_view utf8) { return toUtf16(utf8).size(); }

std::string base36(uint64_t value) {
  if (value == 0) return "0";
  std::string out;
  while (value) {
    out.insert(out.begin(), "0123456789abcdefghijklmnopqrstuvwxyz"[value % 36]);
    value /= 36;
  }
  return out;
}

uint64_t xxh64(const void* data, const size_t len, const uint64_t seed) {
  constexpr uint64_t P1 = 11400714785074694791ULL, P2 = 14029467366897019727ULL, P3 = 1609587929392839161ULL,
                     P4 = 9650029242287828579ULL, P5 = 2870177450012600261ULL;
  const auto rotl = [](uint64_t x, int r) { return (x << r) | (x >> (64 - r)); };
  const auto read64 = [](const uint8_t* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
  };
  const auto read32 = [](const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return static_cast<uint64_t>(v);
  };
  const auto round = [&](uint64_t acc, uint64_t input) { return rotl(acc + input * P2, 31) * P1; };
  const auto merge = [&](uint64_t acc, uint64_t val) { return (acc ^ round(0, val)) * P1 + P4; };
  const auto* p = static_cast<const uint8_t*>(data);
  const uint8_t* end = p + len;
  uint64_t h;
  if (len >= 32) {
    uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
    const uint8_t* limit = end - 32;
    do {
      v1 = round(v1, read64(p));
      v2 = round(v2, read64(p + 8));
      v3 = round(v3, read64(p + 16));
      v4 = round(v4, read64(p + 24));
      p += 32;
    } while (p <= limit);
    h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
    h = merge(h, v1);
    h = merge(h, v2);
    h = merge(h, v3);
    h = merge(h, v4);
  } else {
    h = seed + P5;
  }
  h += len;
  while (p + 8 <= end) {
    h ^= round(0, read64(p));
    h = rotl(h, 27) * P1 + P4;
    p += 8;
  }
  if (p + 4 <= end) {
    h ^= read32(p) * P1;
    h = rotl(h, 23) * P2 + P3;
    p += 4;
  }
  while (p < end) {
    h ^= (*p) * P5;
    h = rotl(h, 11) * P1;
    ++p;
  }
  h ^= h >> 33;
  h *= P2;
  h ^= h >> 29;
  h *= P3;
  h ^= h >> 32;
  return h;
}

std::string hashedPassphrase(std::string_view passphrase) {
  static const std::string SALT_OF_ID = std::string("a83hrf7f") + '\x03' + "y7sa8g31";
  const std::vector<uint16_t> pass = toUtf16(passphrase);
  const size_t using_ = static_cast<size_t>(std::floor(static_cast<double>(pass.size()) / 4.0 * 3.0));
  const std::string s = SALT_OF_ID + fromUtf16(pass, using_);
  const std::vector<uint16_t> su = toUtf16(s);
  const std::vector<uint16_t> input = toUtf16(std::to_string(su.size()) + s);
  uint32_t m = 1, f = 2166136261u;
  mixedHash(input, m, f);
  return base36u32(m) + base36u32(f);
}

std::string passphraseDigest(std::string_view passphrase) { return sha256Hex(passphrase); }

std::string chunkId(std::string_view piece, std::string_view hashedPass) {
  std::string s;
  s.reserve(piece.size() + hashedPass.size() + 12);
  s.append(piece);
  s += '-';
  s.append(hashedPass);
  s += '-';
  s += std::to_string(utf16Length(piece));
  return "h:+" + base36(xxh64(s.data(), s.size(), 0));
}

std::string noteId(std::string_view passphraseDigestHex, std::string_view path) {
  std::string lower;
  if (!lowerLikeJs(path, lower)) return "";
  if (!lower.empty() && lower[0] == '_') lower = "/" + lower;
  return "f:" + sha256Hex(std::string(passphraseDigestHex) + ":" + lower);
}

}  // namespace livesync
