#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Battery drain per hour for Settings > System > Battery (kos25k #20). Each deep sleep and each wake
// leaves one line in /.vaultink/battery-log.tsv:
//   <epoch>\t<S or W>\t<percent>\t<usb 0/1>[\t<light 0/1>[\t<millivolts>\t<wake cause>]]
// The light field (0.65.0) says the front light was on at some point in the awake period that ends at an S line;
// lines from before have none, and their awake pairs stay out of the light-off and light-on rates.
// Millivolts and the wake cause (0.73.1, kos25k #34) tell a real drain from the gauge re-guessing its percent, and a
// wake nobody asked for from a normal one: 0 power key held, 1 after flash, 2 USB power, 3 other, 4 power key not
// held (a ghost wake, which goes straight back to sleep or stays on); '-' on an S line. Millivolts 0 = not read.
// The gauge only reports whole percent and nothing in the firmware measures current per task, so the
// page shows two rates, asleep (sleep to wake) and awake (wake to sleep), summed over many pairs.
namespace batterylog {

constexpr const char* LOG_PATH = "/.vaultink/battery-log.tsv";
constexpr size_t MAX_RECORDS = 128;
constexpr int64_t MAX_PAIR_SECONDS = 14 * 24 * 3600;  // longer gaps are a flat battery, not standby

struct Record {
  int64_t epoch = 0;   // trusted UTC seconds; 0 = untrusted, never stored
  bool sleep = false;  // true = going to sleep, false = woke up
  uint8_t percent = 0;
  bool usb = false;
  bool lightKnown = false;  // the line had a light field
  bool light = false;       // on an S line: the light was on during the awake period before it
  uint16_t millivolts = 0;  // 0 = not read (older lines)
  int8_t wakeCause = -1;    // -1 = none (S lines and older lines)
};

struct Rate {
  bool known = false;
  uint32_t hundredthsPerHour = 0;  // 12 = 0.12 %/h
  uint32_t seconds = 0;            // time the rate is based on
};

struct Stats {
  Rate asleep;
  Rate awake;          // every awake pair, light known or not
  Rate awakeLightOff;  // awake pairs whose light state is known
  Rate awakeLightOn;
};

std::string toLine(const Record& r);
bool fromLine(std::string_view line, Record& out);

// Newest last, at most MAX_RECORDS. A record that cannot follow the last one is still kept; the
// pairing below skips what does not make sense.
void append(std::vector<Record>& records, const Record& r);

// Sleep-to-wake and wake-to-sleep pairs only. A pair counts when both ends are dated and in order, no
// USB at either end, the percent did not rise, and it is under MAX_PAIR_SECONDS. Zero-drop pairs
// stay in the totals. A rate shows once there is at least a 1 % drop, or 6 h of data.
Stats compute(const std::vector<Record>& records);

}  // namespace batterylog
