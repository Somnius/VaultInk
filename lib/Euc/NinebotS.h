#pragma once

#include "Euc.h"
#include "NinebotCommon.h"

// Ninebot One S1/S2/A1: Nordic UART, register reads to address 0x11 (eucNinebotS.js:5-55).
namespace euc::ninebot {

extern const GattProfile kProfileS;
extern const Variant kVariantS;

}  // namespace euc::ninebot
