#pragma once

// The device side of the developer link (docs/devlink.md). Only the developer builds define
// VAULTINK_DEVLINK (x4pro-dev and x4pro-dev-verify with VAULTINK_DEVLINK_FULL, the read-only
// x4pro-dev-user with VAULTINK_DEVLINK_USER); every other build compiles none of this.
#ifdef VAULTINK_DEVLINK

#include <DevLink.h>

#include <cstdint>

namespace devlinkdevice {

// The transmit queue of the USB serial port in this build, so one reply (devlink::JsonLine::CAPACITY
// at most) goes out as one write. The core's default is 256 bytes.
constexpr unsigned TX_QUEUE_BYTES = 1024;

// The wake cause the battery log got at boot (batteryhistory::noteWake), for STATUS.
void noteWakeCause(int cause);

// Answers one command with one JSON line on the USB serial port (LS and FLASH send lines without
// "ok" first: entries, progress). SCREENSHOT is not handled here: main.cpp answers it the same way
// in every build. REBOOT and BOOTLOADER do not return. FLASH returns only when the image is
// written, and does not restart. Call it from the main loop, after the USB Drive early return.
// Without VAULTINK_DEVLINK_FULL only PING, HELP, STATUS, LOG, BENCH and REBOOT are answered:
// BOOTLOADER gets an error, and KEY, TAP, SWIPE, FLASH, LS, GET and PUT are not in the name table.
void handle(const devlink::Parsed& parsed, unsigned long msSinceInput, uint32_t droppedLines);

// The SD bench logger: while it is switched on (CMD:BENCH <minutes>), appends one row to
// /.vaultink/bench.csv when the interval has passed. Call it from the main loop only, after the
// USB Drive early return (the card is not ours there). It never waits: when the screen is being
// drawn it does nothing and tries again on the next pass. It starts no radio and no timer, and
// does not run in or before sleep.
void benchTick();

}  // namespace devlinkdevice

#endif  // VAULTINK_DEVLINK
