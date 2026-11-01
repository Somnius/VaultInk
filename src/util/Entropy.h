#pragma once

#include <cstdint>

// The random seed for dice and the other random picks (0.63.0): the chip's generator mixed with the date and
// time, the battery percentage and voltage and the clock jitter (lib/Dice/DiceEntropy.h has the mix and the reasons).
namespace entropy {

uint32_t seed();

}  // namespace entropy
