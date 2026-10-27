#pragma once

// Settings -> System -> Debug Logs (off by default): the device's log lines go to /debug/ on the SD
// card, for sending to the developer when something goes wrong. Nothing leaves the device by
// itself. /debug/log-0.txt is the newest file; at 256 KB it moves to log-1 (up to log-3), so the
// logs never take more than about 1 MB. A crash also leaves /debug/crash-<n>.txt.
namespace debuglog {

// At boot, after the settings are loaded: starts logging when the setting is on, and keeps a copy
// of the crash report when this boot follows a crash.
void begin(bool rebootedFromPanic);
// The setting changed.
void apply();
// Writes what is buffered (every few seconds from the main loop, and before sleep or a restart).
void flush();
// Called often from the main loop: flushes now and then.
void tick();
// Deletes /debug.
void clear();

}  // namespace debuglog
