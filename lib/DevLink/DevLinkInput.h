#pragma once

// Key and touch injection for the developer build (CMD:KEY, CMD:TAP, CMD:SWIPE; docs/devlink.md).
// A short queue of made-up events that HalGPIO plays where it reads the hardware: a key goes into
// the input library's own button state (its button hook), so debounce, press and release edges and
// the held time are the real ones; a touch is answered by this class in place of the touch
// controller, with the controller's own thresholds. Platform-free and heap-free: the caller passes
// the firmware's millis() clock. Compiled only with VAULTINK_DEVLINK_FULL.
#ifdef VAULTINK_DEVLINK_FULL

#include <cstddef>
#include <cstdint>

namespace devlink {

class InputInjector {
 public:
  static constexpr size_t MAX_EVENTS = 16;  // a key, a tap and a swipe are two events each
  // The pause between the end of one key or touch and the start of the next.
  static constexpr uint32_t GAP_MS = 50;
  // A state stays at least this long, and through one more pass of the input loop after that,
  // before the next event: the input library takes a button state only after it has read it
  // unchanged for more than 5 ms.
  static constexpr uint32_t MIN_VISIBLE_MS = 10;
  // The touch controller's thresholds (InputManager.h in the SDK), in panel pixels.
  static constexpr int TAP_SLOP_PX = 28;
  static constexpr int TAP_RELEASE_SLOP_PX = 59;
  static constexpr int SWIPE_MIN_PX = 60;
  static constexpr uint32_t SWIPE_MAX_MS = 700;
  static constexpr uint32_t LONG_PRESS_MS = 500;
  static constexpr uint8_t BUTTON_COUNT = 6;  // BACK CONFIRM LEFT RIGHT UP DOWN: never the power key

  void setPanel(uint16_t width, uint16_t height);

  // Both add two events (down, up) or nothing. False when the queue has no room for two, or the
  // button is not one of the six. Coordinates are panel pixels and are held inside the panel.
  bool queueKey(uint8_t button, uint32_t holdMs);
  bool queueTouch(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t holdMs);
  size_t queued() const { return count; }

  // Once per pass of the input loop, before the hardware is read: forgets the one-pass edges and
  // plays at most one event that is due.
  void advance(uint32_t nowMs);

  // The buttons held down now, as bits 0 to 5.
  uint8_t buttonMask() const { return buttons; }

  // The touch answers, named after HalGPIO's. Positions are 0..1 across the panel.
  bool touchDown() const { return touch.down; }
  bool wasTouchTap(float& nx, float& ny) const;
  bool wasTouchPressed(float& nx, float& ny) const;
  bool wasTouchReleased() const;
  bool isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const;
  bool isTouchHeldAt(float& nx, float& ny) const;
  bool wasTouchLongPress(float& nx, float& ny) const;
  bool wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const;
  bool wasTouchActivity() const { return touch.pressedEdge || touch.releasedEdge; }
  void suppressTouchContact();
  // True from a made-up touch going down until a real one is reported: whose held time counts.
  bool ownsLastTouch() const { return touch.ownsLast; }
  void noteRealTouch();
  unsigned long lastTouchHeldMs() const { return touch.lastHeldMs; }

 private:
  enum class Kind : uint8_t { KeyDown, KeyUp, TouchDown, TouchUp };
  struct Event {
    Kind kind = Kind::KeyDown;
    uint8_t button = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t endX = 0;
    uint16_t endY = 0;
    uint32_t delayMs = 0;  // after the event before it was played
    uint32_t holdMs = 0;
  };
  struct Touch {
    bool down = false;
    bool pressedEdge = false;
    bool releasedEdge = false;
    bool longPressEdge = false;
    bool longPressFired = false;
    bool movedBeyondTapSlop = false;
    bool movedBeyondReleaseSlop = false;
    bool suppressed = false;
    bool ownsLast = false;
    uint16_t startX = 0;
    uint16_t startY = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint16_t endX = 0;
    uint16_t endY = 0;
    uint32_t pressedAt = 0;
    uint32_t holdMs = 0;
    uint32_t lastHeldMs = 0;
  };

  bool push(const Event& event);
  void play(const Event& event, uint32_t nowMs);
  void moveTouch(uint16_t x, uint16_t y);
  void normalize(uint16_t x, uint16_t y, float& nx, float& ny) const;

  Event events[MAX_EVENTS];
  size_t head = 0;
  size_t count = 0;
  Touch touch;
  uint16_t panelWidth = 800;
  uint16_t panelHeight = 480;
  uint32_t nowCached = 0;
  uint32_t lastPlayedMs = 0;
  uint8_t buttons = 0;
  bool played = false;   // an event was played since boot
  bool settled = false;  // a pass ran MIN_VISIBLE_MS or more after the last event
};

}  // namespace devlink

#endif  // VAULTINK_DEVLINK_FULL
