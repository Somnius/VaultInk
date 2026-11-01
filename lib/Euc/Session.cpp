#include "Session.h"

#include <algorithm>

namespace euc {
namespace session {

bool isMoving(const Telemetry& t) {
  if (!t.has(F_SPEED)) {
    return true;
  }
  const float speed = t.speedKmh < 0.0f ? -t.speedKmh : t.speedKmh;
  return speed > kMovingKmh;
}

bool commandAllowed(const Command c, const Telemetry& t) {
  if (c >= Command::Count) {
    return false;
  }
  if (allowedWhileMoving(c)) {
    return true;
  }
  return !isMoving(t);
}

uint8_t clampRefreshSeconds(const uint32_t seconds) {
  const uint32_t s = std::clamp<uint32_t>(seconds, kMinRefreshSeconds, kMaxRefreshSeconds);
  return static_cast<uint8_t>(s);
}

}  // namespace session
}  // namespace euc
