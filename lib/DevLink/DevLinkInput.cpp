#include "DevLinkInput.h"

#ifdef VAULTINK_DEVLINK_FULL

namespace devlink {
namespace {

int distance(const uint16_t a, const uint16_t b) { return a > b ? a - b : b - a; }

uint16_t inside(const uint16_t value, const uint16_t extent) {
  if (extent == 0) return 0;
  return value >= extent ? static_cast<uint16_t>(extent - 1) : value;
}

}  // namespace

void InputInjector::setPanel(const uint16_t width, const uint16_t height) {
  if (width > 0) panelWidth = width;
  if (height > 0) panelHeight = height;
}

bool InputInjector::push(const Event& event) {
  if (count >= MAX_EVENTS) return false;
  events[(head + count) % MAX_EVENTS] = event;
  ++count;
  return true;
}

bool InputInjector::queueKey(const uint8_t button, const uint32_t holdMs) {
  if (button >= BUTTON_COUNT || count + 2 > MAX_EVENTS) return false;
  Event down;
  down.kind = Kind::KeyDown;
  down.button = button;
  down.delayMs = GAP_MS;
  Event up = down;
  up.kind = Kind::KeyUp;
  up.delayMs = holdMs;
  return push(down) && push(up);
}

bool InputInjector::queueTouch(const uint16_t x1, const uint16_t y1, const uint16_t x2, const uint16_t y2,
                               const uint32_t holdMs) {
  if (count + 2 > MAX_EVENTS) return false;
  Event down;
  down.kind = Kind::TouchDown;
  down.x = inside(x1, panelWidth);
  down.y = inside(y1, panelHeight);
  down.endX = inside(x2, panelWidth);
  down.endY = inside(y2, panelHeight);
  down.delayMs = GAP_MS;
  down.holdMs = holdMs;
  Event up = down;
  up.kind = Kind::TouchUp;
  up.delayMs = holdMs;
  return push(down) && push(up);
}

void InputInjector::moveTouch(const uint16_t x, const uint16_t y) {
  touch.x = x;
  touch.y = y;
  const int dx = distance(x, touch.startX);
  const int dy = distance(y, touch.startY);
  if (dx > TAP_SLOP_PX || dy > TAP_SLOP_PX) touch.movedBeyondTapSlop = true;
  if (dx > TAP_RELEASE_SLOP_PX || dy > TAP_RELEASE_SLOP_PX) touch.movedBeyondReleaseSlop = true;
}

void InputInjector::play(const Event& event, const uint32_t nowMs) {
  switch (event.kind) {
    case Kind::KeyDown:
      buttons = static_cast<uint8_t>(buttons | (1u << event.button));
      break;
    case Kind::KeyUp:
      buttons = static_cast<uint8_t>(buttons & ~(1u << event.button));
      break;
    case Kind::TouchDown: {
      const uint32_t keptHeld = touch.lastHeldMs;
      touch = Touch{};
      touch.lastHeldMs = keptHeld;
      touch.down = true;
      touch.pressedEdge = true;
      touch.ownsLast = true;
      touch.startX = touch.x = event.x;
      touch.startY = touch.y = event.y;
      touch.endX = event.endX;
      touch.endY = event.endY;
      touch.pressedAt = nowMs;
      touch.holdMs = event.holdMs;
      break;
    }
    case Kind::TouchUp:
      if (!touch.down) break;
      moveTouch(event.endX, event.endY);
      touch.down = false;
      touch.releasedEdge = true;
      // The time the command asked for, not the time it took: a pass of the loop that came late
      // must not turn a swipe into a drag.
      touch.lastHeldMs = event.holdMs;
      break;
  }
}

void InputInjector::advance(const uint32_t nowMs) {
  nowCached = nowMs;
  touch.pressedEdge = false;
  touch.releasedEdge = false;
  touch.longPressEdge = false;

  const bool wasSettled = settled;
  if (played && nowMs - lastPlayedMs >= MIN_VISIBLE_MS) settled = true;

  if (count > 0) {
    const Event& next = events[head];
    if (!played || (wasSettled && nowMs - lastPlayedMs >= next.delayMs)) {
      play(next, nowMs);
      head = (head + 1) % MAX_EVENTS;
      --count;
      played = true;
      settled = false;
      lastPlayedMs = nowMs;
      return;
    }
  }

  if (!touch.down) return;
  const uint32_t held = nowMs - touch.pressedAt;
  if (touch.holdMs > 0 && (touch.endX != touch.startX || touch.endY != touch.startY)) {
    // A swipe moves in a straight line over its time.
    const uint32_t part = held >= touch.holdMs ? touch.holdMs : held;
    const auto along = [&](const uint16_t from, const uint16_t to) {
      const int32_t span = static_cast<int32_t>(to) - static_cast<int32_t>(from);
      return static_cast<uint16_t>(from + span * static_cast<int32_t>(part) / static_cast<int32_t>(touch.holdMs));
    };
    moveTouch(along(touch.startX, touch.endX), along(touch.startY, touch.endY));
  }
  // Only a touch that was asked to last that long is a long press: a tap whose release came late
  // stays a tap.
  if (!touch.movedBeyondTapSlop && !touch.longPressFired && !touch.suppressed && touch.holdMs >= LONG_PRESS_MS &&
      held >= LONG_PRESS_MS) {
    touch.longPressFired = true;
    touch.longPressEdge = true;
  }
}

void InputInjector::normalize(const uint16_t x, const uint16_t y, float& nx, float& ny) const {
  // The middle of the pixel, so GfxRenderer::tapToLogical (which cuts nx * width down) gets it back.
  nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(panelWidth);
  ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(panelHeight);
}

bool InputInjector::wasTouchTap(float& nx, float& ny) const {
  if (!touch.releasedEdge || touch.suppressed || touch.movedBeyondReleaseSlop) return false;
  normalize(touch.startX, touch.startY, nx, ny);
  return true;
}

bool InputInjector::wasTouchPressed(float& nx, float& ny) const {
  if (!touch.pressedEdge) return false;
  normalize(touch.startX, touch.startY, nx, ny);
  return true;
}

bool InputInjector::wasTouchReleased() const { return touch.releasedEdge && !touch.suppressed; }

bool InputInjector::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
  if (!touch.down || touch.movedBeyondTapSlop || touch.suppressed) return false;
  normalize(touch.startX, touch.startY, nx, ny);
  heldMs = nowCached - touch.pressedAt;
  return true;
}

bool InputInjector::isTouchHeldAt(float& nx, float& ny) const {
  if (!touch.down || touch.suppressed) return false;
  normalize(touch.x, touch.y, nx, ny);
  return true;
}

bool InputInjector::wasTouchLongPress(float& nx, float& ny) const {
  if (!touch.longPressEdge) return false;
  normalize(touch.startX, touch.startY, nx, ny);
  return true;
}

bool InputInjector::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  if (!touch.releasedEdge || touch.suppressed || touch.lastHeldMs > SWIPE_MAX_MS) return false;
  if (distance(touch.x, touch.startX) < SWIPE_MIN_PX && distance(touch.y, touch.startY) < SWIPE_MIN_PX) return false;
  normalize(touch.startX, touch.startY, nxStart, nyStart);
  normalize(touch.x, touch.y, nxEnd, nyEnd);
  return true;
}

void InputInjector::suppressTouchContact() {
  if (touch.down || touch.releasedEdge) touch.suppressed = true;
}

void InputInjector::noteRealTouch() {
  if (!touch.down && !touch.releasedEdge) touch.ownsLast = false;
}

}  // namespace devlink

#endif  // VAULTINK_DEVLINK_FULL
