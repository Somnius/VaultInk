#pragma once

#include <cstdint>

#include "Euc.h"

// Session rules shared by every brand: the command safety gate and the screen refresh interval clamp.
namespace euc {
namespace session {

constexpr float kMovingKmh = 2.0f;         // WheelDash's rule: above this the wheel is "moving"
constexpr uint8_t kMinRefreshSeconds = 5;  // e-ink: never faster
constexpr uint8_t kMaxRefreshSeconds = 30;

// False when the wheel is moving (speed > 2 km/h, or speed unknown) and the command is not in Euc.h's
// allowedWhileMoving list. Count is never allowed.
bool commandAllowed(Command c, const Telemetry& t);

// True when the wheel is moving or its speed is unknown (unknown counts as moving: fail safe).
bool isMoving(const Telemetry& t);

// Snaps a requested refresh to [kMinRefreshSeconds, kMaxRefreshSeconds]; 0 or anything below 5 becomes 5.
uint8_t clampRefreshSeconds(uint32_t seconds);

}  // namespace session
}  // namespace euc
