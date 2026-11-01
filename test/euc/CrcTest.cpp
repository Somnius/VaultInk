#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "CrcUtil.h"

using euc::crc::crc16Xmodem;
using euc::crc::ninebotCheck;
using euc::crc::sum8;
using euc::crc::xor8;

TEST(CrcUtil, Crc16XmodemCheckValue) {
  // The standard XMODEM check value for "123456789".
  const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  EXPECT_EQ(crc16Xmodem(s, sizeof s), 0x31C3);
  EXPECT_EQ(crc16Xmodem(s, 0), 0x0000);
}

TEST(CrcUtil, Crc16XmodemMatchesVescDashRequest) {
  // VESCDash GarminEUCBleDelegate.mc:129: 02 01 2F D5 8D 03 = COMM_GET_VALUES_SETUP with its CRC.
  const uint8_t payload[] = {0x2F};
  EXPECT_EQ(crc16Xmodem(payload, 1), 0xD58D);
}

TEST(CrcUtil, Xor8MatchesInmotionV2Constants) {
  // eucInmotionV2.js:21-31, check byte = XOR of everything before it (AA AA XOR to zero).
  const uint8_t live[] = {0xAA, 0xAA, 0x14, 0x01, 0x04};
  EXPECT_EQ(xor8(live, sizeof live), 0x11);
  const uint8_t getVer[] = {0xAA, 0xAA, 0x11, 0x02, 0x02, 0x06};
  EXPECT_EQ(xor8(getVer, sizeof getVer), 0x17);
  const uint8_t drlOn[] = {0xAA, 0xAA, 0x14, 0x03, 0x60, 0x2D, 0x01};
  EXPECT_EQ(xor8(drlOn, sizeof drlOn), 0x5B);
  const uint8_t lightsOn[] = {0xAA, 0xAA, 0x14, 0x03, 0x60, 0x50, 0x01};
  EXPECT_EQ(xor8(lightsOn, sizeof lightsOn), 0x26);
}

TEST(CrcUtil, Sum8MatchesInmotionV1Constants) {
  // eucInmotionV10.js:9 "live": the check is the sum of the unescaped body (0x13 0x01 0x55 0x0F ... 0x00).
  const uint8_t liveBody[] = {0x13, 0x01, 0x55, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF,
                              0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x05, 0x00, 0x00};
  EXPECT_EQ(sum8(liveBody, sizeof liveBody), 0x7D);
  // :10 "init" (pin code 000000) -> 0x9B, :12 "horn" -> 0x84, :13 "beep" -> 0x95.
  const uint8_t initBody[] = {0x07, 0x03, 0x55, 0x0F, 0x30, 0x30, 0x30, 0x30,
                              0x30, 0x30, 0x00, 0x00, 0x08, 0x05, 0x00, 0x00};
  EXPECT_EQ(sum8(initBody, sizeof initBody), 0x9B);
  const uint8_t hornBody[] = {0x09, 0x06, 0x55, 0x0F, 0x04, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0x00, 0x00};
  EXPECT_EQ(sum8(hornBody, sizeof hornBody), 0x84);
  const uint8_t beepBody[] = {0x09, 0x06, 0x55, 0x0F, 0x15, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x08, 0x05, 0x00, 0x00};
  EXPECT_EQ(sum8(beepBody, sizeof beepBody), 0x95);
}

TEST(CrcUtil, NinebotCheckMatchesEucWatchRequests) {
  // Each row: the bytes from the length byte to the last data byte, then the expected ck lo, ck hi.
  struct Row {
    std::vector<uint8_t> body;
    uint8_t lo;
    uint8_t hi;
  };
  const std::vector<Row> rows = {
      // eucNinebotS.js:6-20 (address 0x11)
      {{3, 17, 1, 80, 2}, 152, 255},
      {{3, 17, 1, 38, 2}, 194, 255},
      {{3, 17, 1, 62, 2}, 170, 255},
      {{3, 17, 1, 71, 2}, 161, 255},
      {{3, 17, 1, 185, 2}, 47, 255},
      {{3, 17, 1, 58, 2}, 174, 255},
      {{3, 17, 1, 41, 4}, 189, 255},
      {{3, 17, 1, 37, 2}, 195, 255},
      {{3, 17, 1, 182, 2}, 50, 255},
      {{3, 17, 1, 112, 2}, 120, 255},
      // eucNinebotE.js:6-15 (address 0x09)
      {{3, 9, 1, 80, 2}, 160, 255},
      {{3, 9, 1, 38, 2}, 202, 255},
      {{3, 9, 1, 62, 2}, 178, 255},
      {{3, 9, 1, 71, 2}, 169, 255},
      {{3, 9, 1, 185, 2}, 55, 255},
      {{3, 9, 1, 58, 2}, 182, 255},
      {{3, 9, 1, 37, 2}, 203, 255},
      {{3, 9, 1, 41, 4}, 197, 255},
      {{3, 9, 1, 182, 2}, 58, 255},
      {{3, 9, 1, 112, 2}, 128, 255},
      // eucNinebotZ.js:6 live request 5A A5 01 3E 14 01 B0 20 DB FE
      {{0x01, 0x3E, 0x14, 0x01, 0xB0, 0x20}, 0xDB, 0xFE},
  };
  for (const Row& r : rows) {
    const uint16_t ck = ninebotCheck(r.body.data(), r.body.size());
    EXPECT_EQ(ck & 0xFF, r.lo) << "register " << static_cast<int>(r.body[3]);
    EXPECT_EQ(ck >> 8, r.hi) << "register " << static_cast<int>(r.body[3]);
  }
}
