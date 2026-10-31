#pragma once

#include <BatteryLog.h>

// The battery log on the SD card (kos25k #20): one line when the device goes to sleep and one when
// it wakes, for the drain-per-hour rows on the Battery page.
namespace batteryhistory {

// Called just before deep sleep. Skipped when the clock is not trusted.
void noteSleep();
// Called at boot. Logged only when the last line was a sleep, so a restart or a crash is not paired.
void noteWake();
batterylog::Stats load();

}  // namespace batteryhistory
