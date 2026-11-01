#pragma once

#include "Euc.h"
#include "NinebotCommon.h"

// Ninebot One E+/C/P: service FFE0, characteristic FFE1 for both directions, register reads to address 0x09
// (eucNinebotE.js:5-49).
namespace euc::ninebot {

extern const GattProfile kProfileE;
extern const Variant kVariantE;

}  // namespace euc::ninebot
