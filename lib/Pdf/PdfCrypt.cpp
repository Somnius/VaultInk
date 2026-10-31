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

// `keyWords` is 4 (AES-128) or 8 (AES-256); `round` holds keyWords + 7 round keys.
int expandKey(const uint8_t* key, int keyWords, uint8_t round[15][16]) {
  const int rounds = keyWords + 6;
  const int total = 16 * (rounds + 1);
  uint8_t w[240];
  std::memcpy(w, key, static_cast<size_t>(keyWords) * 4);
  uint8_t rcon = 1;
  for (int i = keyWords * 4; i < total; i += 4) {
    uint8_t t[4] = {w[i - 4], w[i - 3], w[i - 2], w[i - 1]};
    if ((i / 4) % keyWords == 0) {
      const uint8_t first = t[0];
      t[0] = static_cast<uint8_t>(sbox[t[1]] ^ rcon);
      t[1] = sbox[t[2]];
      t[2] = sbox[t[3]];
      t[3] = sbox[first];
      rcon = xtime(rcon);
    } else if (keyWords > 6 && (i / 4) % keyWords == 4) {
      for (int k = 0; k < 4; ++k) t[k] = sbox[t[k]];
    }
    for (int k = 0; k < 4; ++k) w[i + k] = static_cast<uint8_t>(w[i - keyWords * 4 + k] ^ t[k]);
  }
  for (int r = 0; r <= rounds; ++r) std::memcpy(round[r], w + r * 16, 16);
  return rounds;
}

void encryptBlock(const uint8_t round[15][16], int rounds, const uint8_t in[16], uint8_t out[16]) {
  uint8_t s[16];
  for (int i = 0; i < 16; ++i) s[i] = static_cast<uint8_t>(in[i] ^ round[0][i]);
  for (int r = 1; r <= rounds; ++r) {
    uint8_t t[16];  // SubBytes + ShiftRows (state is column-major: s[col * 4 + row])
    for (int c = 0; c < 4; ++c) {
      for (int row = 0; row < 4; ++row) t[c * 4 + row] = sbox[s[((c + row) % 4) * 4 + row]];
    }
    if (r < rounds) {  // MixColumns
      for (int c = 0; c < 4; ++c) {
        const uint8_t a0 = t[c * 4], a1 = t[c * 4 + 1], a2 = t[c * 4 + 2], a3 = t[c * 4 + 3];
        s[c * 4] = static_cast<uint8_t>(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3 ^ round[r][c * 4]);
        s[c * 4 + 1] = static_cast<uint8_t>(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3 ^ round[r][c * 4 + 1]);
        s[c * 4 + 2] = static_cast<uint8_t>(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3) ^ round[r][c * 4 + 2]);
        s[c * 4 + 3] = static_cast<uint8_t>((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3) ^ round[r][c * 4 + 3]);
      }
    } else {
      for (int i = 0; i < 16; ++i) s[i] = static_cast<uint8_t>(t[i] ^ round[r][i]);
    }
  }
  std::memcpy(out, s, 16);
}

void decryptBlock(const uint8_t round[15][16], int rounds, const uint8_t in[16], uint8_t out[16]) {
  uint8_t s[16];
  for (int i = 0; i < 16; ++i) s[i] = static_cast<uint8_t>(in[i] ^ round[rounds][i]);
  for (int r = rounds - 1; r >= 0; --r) {
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

// ---- SHA-2 (FIPS 180-4)
inline uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
inline uint64_t rotr64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

void sha256Block(uint32_t h[8], const uint8_t* p) {
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = static_cast<uint32_t>(p[i * 4]) << 24 | static_cast<uint32_t>(p[i * 4 + 1]) << 16 |
           static_cast<uint32_t>(p[i * 4 + 2]) << 8 | static_cast<uint32_t>(p[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t v[8];
  std::memcpy(v, h, sizeof(v));
  for (int i = 0; i < 64; ++i) {
    const uint32_t s1 = rotr32(v[4], 6) ^ rotr32(v[4], 11) ^ rotr32(v[4], 25);
    const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
    const uint32_t t1 = v[7] + s1 + ch + K[i] + w[i];
    const uint32_t s0 = rotr32(v[0], 2) ^ rotr32(v[0], 13) ^ rotr32(v[0], 22);
    const uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
    const uint32_t t2 = s0 + maj;
    v[7] = v[6];
    v[6] = v[5];
    v[5] = v[4];
    v[4] = v[3] + t1;
    v[3] = v[2];
    v[2] = v[1];
    v[1] = v[0];
    v[0] = t1 + t2;
  }
  for (int i = 0; i < 8; ++i) h[i] += v[i];
}

void sha512Block(uint64_t h[8], const uint8_t* p) {
  static const uint64_t K[80] = {
      0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
      0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
      0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
      0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
      0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
      0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
      0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
      0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
      0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
      0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
      0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
      0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
      0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
      0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
      0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
      0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
      0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
      0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
      0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
      0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};
  uint64_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = 0;
    for (int k = 0; k < 8; ++k) w[i] = (w[i] << 8) | p[i * 8 + k];
  }
  for (int i = 16; i < 80; ++i) {
    const uint64_t s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
    const uint64_t s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint64_t v[8];
  std::memcpy(v, h, sizeof(v));
  for (int i = 0; i < 80; ++i) {
    const uint64_t s1 = rotr64(v[4], 14) ^ rotr64(v[4], 18) ^ rotr64(v[4], 41);
    const uint64_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
    const uint64_t t1 = v[7] + s1 + ch + K[i] + w[i];
    const uint64_t s0 = rotr64(v[0], 28) ^ rotr64(v[0], 34) ^ rotr64(v[0], 39);
    const uint64_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
    const uint64_t t2 = s0 + maj;
    v[7] = v[6];
    v[6] = v[5];
    v[5] = v[4];
    v[4] = v[3] + t1;
    v[3] = v[2];
    v[2] = v[1];
    v[1] = v[0];
    v[0] = t1 + t2;
  }
  for (int i = 0; i < 8; ++i) h[i] += v[i];
}

// Pads `data` per FIPS 180-4 (block 64 or 128 bytes, length field 8 or 16 bytes) and feeds each block.
template <typename Block>
void shaFeed(const uint8_t* data, size_t len, size_t blockSize, size_t lenField, Block block) {
  size_t i = 0;
  for (; i + blockSize <= len; i += blockSize) block(data + i);
  uint8_t tail[256] = {};
  const size_t rest = len - i;
  if (rest) std::memcpy(tail, data + i, rest);
  tail[rest] = 0x80;
  const size_t total = rest + 1 + lenField <= blockSize ? blockSize : 2 * blockSize;
  const uint64_t bits = static_cast<uint64_t>(len) * 8;
  for (int k = 0; k < 8; ++k) tail[total - 1 - k] = static_cast<uint8_t>(bits >> (8 * k));
  block(tail);
  if (total == 2 * blockSize) block(tail + blockSize);
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

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  shaFeed(data, len, 64, 8, [&h](const uint8_t* p) { sha256Block(h, p); });
  for (int i = 0; i < 8; ++i) {
    for (int k = 0; k < 4; ++k) out[i * 4 + k] = static_cast<uint8_t>(h[i] >> (24 - 8 * k));
  }
}

void sha512Family(const uint8_t* data, size_t len, uint64_t h[8], uint8_t* out, size_t outLen) {
  shaFeed(data, len, 128, 16, [h](const uint8_t* p) { sha512Block(h, p); });
  for (size_t i = 0; i < outLen; ++i) out[i] = static_cast<uint8_t>(h[i / 8] >> (56 - 8 * (i % 8)));
}

void sha384(const uint8_t* data, size_t len, uint8_t out[48]) {
  uint64_t h[8] = {0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
                   0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL};
  sha512Family(data, len, h, out, 48);
}

void sha512(const uint8_t* data, size_t len, uint8_t out[64]) {
  uint64_t h[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
                   0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  sha512Family(data, len, h, out, 64);
}

bool aes128CbcDecrypt(const uint8_t key[16], const uint8_t* in, size_t len, std::string& out) {
  out.clear();
  if (len < 32 || len % 16 != 0) return false;
  makeTables();
  uint8_t round[15][16];
  const int rounds = expandKey(key, 4, round);
  out.resize(len - 16);
  const uint8_t* previous = in;
  for (size_t at = 16; at < len; at += 16) {
    uint8_t block[16];
    decryptBlock(round, rounds, in + at, block);
    for (int i = 0; i < 16; ++i) out[at - 16 + i] = static_cast<char>(block[i] ^ previous[i]);
    previous = in + at;
  }
  const uint8_t pad = static_cast<uint8_t>(out.back());
  if (pad == 0 || pad > 16 || pad > out.size()) return false;
  out.resize(out.size() - pad);
  return true;
}

bool aes256CbcDecrypt(const uint8_t key[32], const uint8_t* in, size_t len, std::string& out) {
  out.clear();
  if (len < 32 || len % 16 != 0) return false;
  uint8_t iv[16];
  std::memcpy(iv, in, 16);
  if (!aes256CbcDecryptRaw(key, iv, in + 16, len - 16, out)) return false;
  const uint8_t pad = static_cast<uint8_t>(out.back());
  if (pad == 0 || pad > 16 || pad > out.size()) return false;
  out.resize(out.size() - pad);
  return true;
}

bool aes256CbcDecryptRaw(const uint8_t key[32], const uint8_t iv[16], const uint8_t* in, size_t len, std::string& out) {
  out.clear();
  if (len == 0 || len % 16 != 0) return false;
  makeTables();
  uint8_t round[15][16];
  const int rounds = expandKey(key, 8, round);
  out.resize(len);
  const uint8_t* previous = iv;
  for (size_t at = 0; at < len; at += 16) {
    uint8_t block[16];
    decryptBlock(round, rounds, in + at, block);
    for (int i = 0; i < 16; ++i) out[at + i] = static_cast<char>(block[i] ^ previous[i]);
    previous = in + at;
  }
  return true;
}

void aes128CbcEncryptRaw(const uint8_t key[16], const uint8_t iv[16], const uint8_t* in, size_t len, uint8_t* out) {
  makeTables();
  uint8_t round[15][16];
  const int rounds = expandKey(key, 4, round);
  uint8_t previous[16];
  std::memcpy(previous, iv, 16);
  for (size_t at = 0; at + 16 <= len; at += 16) {
    uint8_t block[16];
    for (int i = 0; i < 16; ++i) block[i] = static_cast<uint8_t>(in[at + i] ^ previous[i]);
    encryptBlock(round, rounds, block, out + at);
    std::memcpy(previous, out + at, 16);
  }
}

}  // namespace pdf::crypt
