#pragma once

#include <string>

// The lock screen's rules (kos25k #22). The PIN lives in /.vaultink/lock-pin.txt: taking the SD
// card out and deleting that file turns the PIN lock off, which is the way back from a forgotten
// PIN. It stops someone picking the reader up, not someone who reads the card on a computer.
namespace screenlock {

// True when the lock is on and usable (PIN mode needs its file).
bool enabled();
bool hasPin();
bool checkPin(const std::string& pin);
bool savePin(const std::string& pin);
void removePin();

// Call just before deep sleep: remembers when it went to sleep.
void noteSleep();
// At boot: should the lock screen come first? (asleep as long as Lock After Sleeping or more, a cold start, or the
// lock screen was still up when it slept)
bool lockAtBoot();
// The lock screen is up / was passed.
void setPending(bool pending);

}  // namespace screenlock
