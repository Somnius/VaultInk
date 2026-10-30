#pragma once

#include <functional>
#include <string>

#include "activities/Activity.h"

// The lock screen (kos25k #22), and the screen that sets its PIN.
// Unlock: a 4-digit PIN on a keypad, or (corner mode) a long press on any corner. `onUnlock` runs
// once the screen is open again; the boot routing that the lock held back goes there.
// SetPin: the new PIN twice; the result is cancelled unless both match and it was saved.
class LockActivity final : public Activity {
 public:
  enum class Mode { Unlock, SetPin };

  LockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode,
               std::function<void()> onUnlock = nullptr);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  static constexpr int KEYS = 12;  // 1-9, cancel (SetPin only), 0, delete

  bool keyRect(int key, int& x, int& y, int& w, int& h) const;
  void pressKey(int key);
  void digitsComplete();
  void unlocked();
  bool inUnlockCorner(int x, int y) const;

  Mode mode;
  std::function<void()> onUnlock;
  bool cornerMode = false;
  bool done = false;            // unlocked once already
  bool holding = false;         // a finger is on the unlock corner
  unsigned long holdSince = 0;  // millis() when it landed
  std::string digits;
  std::string firstPin;           // SetPin: the first entry, waiting for the repeat
  const char* message = nullptr;  // i18n string under the dots, or nullptr
};
