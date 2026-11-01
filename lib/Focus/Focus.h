#pragma once

#include <cstddef>
#include <cstdint>

// Platform-free Pomodoro timer for VaultInk. The state machine is driven by wall-clock epoch
// seconds passed in by the caller, so it survives the device sleeping: update(now) ends a phase
// once no matter how long the device was away. No heap, no exceptions, no Arduino/ESP includes.
namespace focus {

enum class Phase : uint8_t { Idle = 0, Focus = 1, ShortBreak = 2, LongBreak = 3 };

enum class Event : uint8_t { None, PhaseEnded };

struct Config {
  uint8_t focusMin = 25;
  uint8_t shortMin = 5;
  uint8_t longMin = 15;
  uint8_t cyclesBeforeLong = 4;
  bool autoNext = false;  // start the next phase as soon as one ends
};

// focus 5..90, short 1..30, long 5..60, cycles 2..8.
Config clampConfig(const Config& c);

// Stable key for a phase ("idle", "focus", "short", "long"), used in logs and the save file.
const char* phaseKey(Phase p);
// Reverse of phaseKey; false on an unknown key.
bool phaseFromKey(const char* key, Phase& out);

// Minutes a phase lasts under a config (0 for Idle).
uint16_t phaseMinutes(const Config& c, Phase p);

class Timer {
 public:
  static constexpr uint8_t SAVE_VERSION = 1;
  // Big enough for serialize() of any state.
  static constexpr size_t SAVE_CAPACITY = 256;

  Timer() = default;
  explicit Timer(const Config& c) : cfg(clampConfig(c)) {}

  const Config& config() const { return cfg; }
  // Applying a config does not touch a running phase's end time.
  void setConfig(const Config& c) { cfg = clampConfig(c); }

  // Begins `p` (Idle stops) at `now`, replacing whatever ran before.
  void start(Phase p, int64_t now);
  // Pauses a running phase; no-op when idle or already paused.
  void pause(int64_t now);
  // Resumes a paused phase, shifting its end by the pause length; no-op otherwise.
  void resume(int64_t now);
  // Back to Idle; the finished-focus counters stay and nextPhase() offers the aborted phase again.
  void stop();
  // Ends the current phase at `now` as if its time had run out (counts a focus as done).
  // Returns PhaseEnded when a phase was running or paused, else None.
  Event skip(int64_t now);
  // Advances the clock. Returns PhaseEnded at most once per phase, however far `now` jumped.
  Event update(int64_t now);

  Phase phase() const { return cur; }
  // The phase start() would begin next, chosen by the cycle count (Focus when idle after a break
  // or fresh, a break after a focus).
  Phase nextPhase() const { return next; }
  bool running() const { return cur != Phase::Idle && !isPaused; }
  bool paused() const { return cur != Phase::Idle && isPaused; }

  // Seconds left in the current phase at `now` (0 when idle or overdue).
  uint32_t remainingSec(int64_t now) const;
  // Seconds spent in the current phase at `now`, not counting pauses (0 when idle).
  uint32_t elapsedSec(int64_t now) const;
  // Seconds the current phase lasts in total (0 when idle).
  uint32_t durationSec() const;

  // Focus phases finished (by time or skip) since the day counter was last reset / ever.
  uint16_t focusDoneToday() const { return doneToday; }
  uint32_t focusDoneTotal() const { return doneTotal; }
  // Focuses finished in the current cycle, 0..cyclesBeforeLong-1; wraps after a long break.
  uint8_t cycleIndex() const { return cycle; }
  // Zeroes the day counter (the UI calls it when the date changes).
  void resetToday() { doneToday = 0; }

  // Key=value lines ("v=1\nphase=focus\n..."). Returns the length written (without the NUL),
  // or 0 when `cap` is too small. The config is included so a reboot restores both.
  size_t serialize(char* buf, size_t cap) const;
  // Reads what serialize() wrote. Unknown keys and garbage lines are skipped, bad numbers fall
  // back to safe values; returns false on a missing/unknown version or an impossible state (the
  // timer is then reset to Idle with the counters it could read).
  bool deserialize(const char* data, size_t size);

 private:
  Phase phaseAfter(Phase ended) const;
  void finishPhase(int64_t at);
  void reset();

  Config cfg{};
  Phase cur = Phase::Idle;
  Phase next = Phase::Focus;
  bool isPaused = false;
  int64_t startAt = 0;       // epoch when the phase (re)started
  int64_t endAt = 0;         // epoch when the phase ends (shifted by pauses)
  int64_t pausedAt = 0;      // epoch of the pause, while paused
  uint32_t pausedTotal = 0;  // seconds spent paused in this phase
  uint8_t cycle = 0;
  uint16_t doneToday = 0;
  uint32_t doneTotal = 0;
};

// One TSV log line: "epoch\tphase\tminutes\tcompleted\n" (phase as phaseKey, completed 1/0).
// Returns the length written, or 0 when `cap` is too small.
size_t formatLogLine(char* buf, size_t cap, int64_t epoch, Phase phase, uint16_t minutes, bool completed);

}  // namespace focus
