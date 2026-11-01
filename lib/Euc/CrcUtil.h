#pragma once

#include <cstddef>
#include <cstdint>

// Checksums shared by the poll-driven brands (InMotion, Ninebot, VESC). Platform-free.
namespace euc::crc {

// CRC16-XMODEM (poly 0x1021, init 0, no reflection), as VESCDash/source/util/CRC16XModem.mc computes it with a
// table. VESC frames carry it big-endian after the payload.
uint16_t crc16Xmodem(const uint8_t* data, size_t size);

// XOR of all bytes (InMotion V2 frame check, REF/InmotionAdapterV2.latest.java:2371-2378).
uint8_t xor8(const uint8_t* data, size_t size);

// Additive checksum, low 8 bits (InMotion V1, eucInmotionV10.js:68-83).
uint8_t sum8(const uint8_t* data, size_t size);

// Ninebot: low 16 bits of (0xFFFF - sum of the bytes from the length byte to the last data byte), stored
// little-endian. Reproduces every request constant in eucNinebotS.js:5-33, eucNinebotE.js:5-33 and
// eucNinebotZ.js:6-9 (see CrcTest.cpp).
uint16_t ninebotCheck(const uint8_t* data, size_t size);

}  // namespace euc::crc
