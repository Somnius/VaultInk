#include "TrigramFilter.h"

#include <Utf8.h>

#include <algorithm>

namespace trigramfilter {
namespace {

uint32_t hashOf(const uint32_t a, const uint32_t b, const uint32_t c) {
  uint32_t h = a * 0x9E3779B1u;
  h = (h ^ (h >> 15)) + b * 0x85EBCA77u;
  h = (h ^ (h >> 13)) + c * 0xC2B2AE3Du;
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  return h;
}

constexpr uint32_t kBits = static_cast<uint32_t>(kBytes) * 8u;

void setBits(uint8_t* bits, const uint32_t h) {
  const uint32_t one = h % kBits;
  const uint32_t two = (h >> 16 | h << 16) % kBits;
  bits[one >> 3] = static_cast<uint8_t>(bits[one >> 3] | (1u << (one & 7)));
  bits[two >> 3] = static_cast<uint8_t>(bits[two >> 3] | (1u << (two & 7)));
}

bool hasBits(const uint8_t* bits, const uint32_t h) {
  const uint32_t one = h % kBits;
  const uint32_t two = (h >> 16 | h << 16) % kBits;
  return (bits[one >> 3] & (1u << (one & 7))) != 0 && (bits[two >> 3] & (1u << (two & 7))) != 0;
}

}  // namespace

void Collector::push(const uint32_t cp) {
  if (count_ >= 2) setBits(bits_, hashOf(last_[0], last_[1], cp));
  last_[0] = last_[1];
  last_[1] = cp;
  if (count_ < 2) ++count_;
}

std::vector<uint32_t> queryTrigrams(const std::string& preparedQuery) {
  std::vector<uint32_t> out;
  const auto* ptr = reinterpret_cast<const unsigned char*>(preparedQuery.c_str());
  uint32_t last[2] = {0, 0};
  int count = 0;
  while (true) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) break;
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0) {
      count = 0;
      continue;
    }
    if (count >= 2) out.push_back(hashOf(last[0], last[1], cp));
    last[0] = last[1];
    last[1] = cp;
    if (count < 2) ++count;
  }
  return out;
}

bool mayContain(const uint8_t* bits, const std::vector<uint32_t>& trigrams) {
  return std::all_of(trigrams.begin(), trigrams.end(), [bits](const uint32_t h) { return hasBits(bits, h); });
}

}  // namespace trigramfilter
