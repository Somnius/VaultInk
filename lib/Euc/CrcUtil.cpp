#include "CrcUtil.h"

#include <numeric>

namespace euc::crc {

uint16_t crc16Xmodem(const uint8_t* data, const size_t size) {
  return std::accumulate(data, data + size, static_cast<uint16_t>(0), [](const uint16_t crc, const uint8_t byte) {
    uint16_t c = static_cast<uint16_t>(crc ^ static_cast<uint16_t>(byte << 8));
    for (int bit = 0; bit < 8; ++bit) {
      c = static_cast<uint16_t>((c & 0x8000) != 0 ? (c << 1) ^ 0x1021 : c << 1);
    }
    return c;
  });
}

uint8_t xor8(const uint8_t* data, const size_t size) {
  return std::accumulate(data, data + size, static_cast<uint8_t>(0),
                         [](const uint8_t acc, const uint8_t byte) { return static_cast<uint8_t>(acc ^ byte); });
}

uint8_t sum8(const uint8_t* data, const size_t size) {
  return static_cast<uint8_t>(std::accumulate(data, data + size, 0u));
}

uint16_t ninebotCheck(const uint8_t* data, const size_t size) {
  const uint32_t sum = std::accumulate(data, data + size, 0u);
  return static_cast<uint16_t>(0xFFFFu - (sum & 0xFFFFu));
}

}  // namespace euc::crc
