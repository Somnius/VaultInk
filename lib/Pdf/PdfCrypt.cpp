#include "PdfCrypt.h"

#include <cstring>
#include <utility>

namespace pdf::crypt {
namespace {

inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

// ---- MD5 (RFC 1321)
struct Md5State {
  uint32_t a = 0x67452301, b = 0xefcdab89, c = 0x98badcfe, d = 0x10325476;
};

void md5Block(Md5State& s, const uint8_t* p) {
  static const uint32_t K[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  static const int R[64] = {7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
                            14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
                            4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21};
  uint32_t m[16];
  for (int i = 0; i < 16; ++i) {
    m[i] = static_cast<uint32_t>(p[i * 4]) | static_cast<uint32_t>(p[i * 4 + 1]) << 8 |
           static_cast<uint32_t>(p[i * 4 + 2]) << 16 | static_cast<uint32_t>(p[i * 4 + 3]) << 24;
  }
  uint32_t a = s.a, b = s.b, c = s.c, d = s.d;
  for (int i = 0; i < 64; ++i) {
    uint32_t f;
    int g;
    if (i < 16) {
      f = (b & c) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & c);
      g = (5 * i + 1) % 16;
    } else if (i < 48) {
      f = b ^ c ^ d;
      g = (3 * i + 5) % 16;
    } else {
      f = c ^ (b | ~d);
      g = (7 * i) % 16;
    }
    const uint32_t next = d;
    d = c;
    c = b;
    b = b + rotl(a + f + K[i] + m[g], R[i]);
    a = next;
  }
  s.a += a;
  s.b += b;
  s.c += c;
  s.d += d;
}

// ---- AES-128
uint8_t sbox[256];
uint8_t invSbox[256];
bool tablesReady = false;

void makeTables() {
  if (tablesReady) return;
  uint8_t p = 1, q = 1;
  do {
    p = static_cast<uint8_t>(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));  // p * 3
    q ^= static_cast<uint8_t>(q << 1);                                 // q / 3
    q ^= static_cast<uint8_t>(q << 2);
    q ^= static_cast<uint8_t>(q << 4);
    if (q & 0x80) q ^= 0x09;
    const uint8_t x = static_cast<uint8_t>(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^
                                           ((q << 4) | (q >> 4)));
    sbox[p] = static_cast<uint8_t>(x ^ 0x63);
  } while (p != 1);
  sbox[0] = 0x63;
  for (int i = 0; i < 256; ++i) invSbox[sbox[i]] = static_cast<uint8_t>(i);
  tablesReady = true;
}

uint8_t xtime(uint8_t x) { return static_cast<uint8_t>((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }

uint8_t mul(uint8_t x, uint8_t y) {
  uint8_t r = 0;
  while (y) {
    if (y & 1) r ^= x;
    x = xtime(x);
    y >>= 1;
  }
  return r;
}

void expandKey(const uint8_t key[16], uint8_t round[11][16]) {
  uint8_t w[176];
  std::memcpy(w, key, 16);
  uint8_t rcon = 1;
  for (int i = 16; i < 176; i += 4) {
    uint8_t t[4] = {w[i - 4], w[i - 3], w[i - 2], w[i - 1]};
    if (i % 16 == 0) {
      const uint8_t first = t[0];
      t[0] = static_cast<uint8_t>(sbox[t[1]] ^ rcon);
      t[1] = sbox[t[2]];
      t[2] = sbox[t[3]];
      t[3] = sbox[first];
      rcon = xtime(rcon);
    }
    for (int k = 0; k < 4; ++k) w[i + k] = static_cast<uint8_t>(w[i - 16 + k] ^ t[k]);
  }
  for (int r = 0; r < 11; ++r) std::memcpy(round[r], w + r * 16, 16);
}

void decryptBlock(const uint8_t round[11][16], const uint8_t in[16], uint8_t out[16]) {
  uint8_t s[16];
  for (int i = 0; i < 16; ++i) s[i] = static_cast<uint8_t>(in[i] ^ round[10][i]);
  for (int r = 9; r >= 0; --r) {
    // InvShiftRows (state is column-major: s[col * 4 + row])
    uint8_t t[16];
    for (int c = 0; c < 4; ++c) {
      for (int row = 0; row < 4; ++row) t[((c + row) % 4) * 4 + row] = s[c * 4 + row];
    }
    for (int i = 0; i < 16; ++i) s[i] = static_cast<uint8_t>(invSbox[t[i]] ^ round[r][i]);
    if (r > 0) {  // InvMixColumns
      for (int c = 0; c < 4; ++c) {
        const uint8_t a0 = s[c * 4], a1 = s[c * 4 + 1], a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
        s[c * 4] = static_cast<uint8_t>(mul(a0, 14) ^ mul(a1, 11) ^ mul(a2, 13) ^ mul(a3, 9));
        s[c * 4 + 1] = static_cast<uint8_t>(mul(a0, 9) ^ mul(a1, 14) ^ mul(a2, 11) ^ mul(a3, 13));
        s[c * 4 + 2] = static_cast<uint8_t>(mul(a0, 13) ^ mul(a1, 9) ^ mul(a2, 14) ^ mul(a3, 11));
        s[c * 4 + 3] = static_cast<uint8_t>(mul(a0, 11) ^ mul(a1, 13) ^ mul(a2, 9) ^ mul(a3, 14));
      }
    }
  }
  std::memcpy(out, s, 16);
}

}  // namespace

void md5(const uint8_t* data, size_t len, uint8_t out[16]) {
  Md5State s;
  size_t i = 0;
  for (; i + 64 <= len; i += 64) md5Block(s, data + i);
  uint8_t tail[128] = {};
  const size_t rest = len - i;
  std::memcpy(tail, data + i, rest);
  tail[rest] = 0x80;
  const size_t total = rest + 1 + 8 <= 64 ? 64 : 128;
  const uint64_t bits = static_cast<uint64_t>(len) * 8;
  for (int k = 0; k < 8; ++k) tail[total - 8 + k] = static_cast<uint8_t>(bits >> (8 * k));
  md5Block(s, tail);
  if (total == 128) md5Block(s, tail + 64);
  const uint32_t words[4] = {s.a, s.b, s.c, s.d};
  for (int w = 0; w < 4; ++w) {
    for (int k = 0; k < 4; ++k) out[w * 4 + k] = static_cast<uint8_t>(words[w] >> (8 * k));
  }
}

std::string md5(const std::string& data) {
  uint8_t out[16];
  md5(reinterpret_cast<const uint8_t*>(data.data()), data.size(), out);
  return std::string(reinterpret_cast<const char*>(out), 16);
}

void rc4(const uint8_t* key, size_t keyLen, uint8_t* data, size_t len) {
  if (keyLen == 0) return;
  uint8_t s[256];
  for (int i = 0; i < 256; ++i) s[i] = static_cast<uint8_t>(i);
  uint8_t j = 0;
  for (int i = 0; i < 256; ++i) {
    j = static_cast<uint8_t>(j + s[i] + key[i % keyLen]);
    std::swap(s[i], s[j]);
  }
  uint8_t a = 0, b = 0;
  for (size_t n = 0; n < len; ++n) {
    a = static_cast<uint8_t>(a + 1);
    b = static_cast<uint8_t>(b + s[a]);
    std::swap(s[a], s[b]);
    data[n] ^= s[static_cast<uint8_t>(s[a] + s[b])];
  }
}

bool aes128CbcDecrypt(const uint8_t key[16], const uint8_t* in, size_t len, std::string& out) {
  out.clear();
  if (len < 32 || len % 16 != 0) return false;
  makeTables();
  uint8_t round[11][16];
  expandKey(key, round);
  out.resize(len - 16);
  const uint8_t* previous = in;
  for (size_t at = 16; at < len; at += 16) {
    uint8_t block[16];
    decryptBlock(round, in + at, block);
    for (int i = 0; i < 16; ++i) out[at - 16 + i] = static_cast<char>(block[i] ^ previous[i]);
    previous = in + at;
  }
  const uint8_t pad = static_cast<uint8_t>(out.back());
  if (pad == 0 || pad > 16 || pad > out.size()) return false;
  out.resize(out.size() - pad);
  return true;
}

}  // namespace pdf::crypt
