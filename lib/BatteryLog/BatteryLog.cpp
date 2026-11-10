#include "BatteryLog.h"

#include <cstdio>
#include <cstdlib>

namespace batterylog {

std::string toLine(const Record& r) {
  char buffer[80];
  int n = snprintf(buffer, sizeof(buffer), "%lld\t%c\t%u\t%u\t%u", static_cast<long long>(r.epoch), r.sleep ? 'S' : 'W',
                   static_cast<unsigned>(r.percent), r.usb ? 1u : 0u, r.light ? 1u : 0u);
  if (r.millivolts != 0 || r.wakeCause >= 0) {
    char cause[2] = {r.wakeCause >= 0 && r.wakeCause <= 9 ? static_cast<char>('0' + r.wakeCause) : '-', '\0'};
    snprintf(buffer + n, sizeof(buffer) - static_cast<size_t>(n), "\t%u\t%s", static_cast<unsigned>(r.millivolts),
             cause);
  }
  return buffer;
}

bool fromLine(std::string_view line, Record& out) {
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
  std::string_view fields[8];
  size_t count = 0;
  size_t start = 0;
  while (count < 8) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string_view::npos) {
      fields[count++] = line.substr(start);
      break;
    }
    fields[count++] = line.substr(start, tab - start);
    start = tab + 1;
  }
  if ((count != 4 && count != 5 && count != 7) || fields[0].empty() || fields[1].size() != 1) return false;
  if (fields[1][0] != 'S' && fields[1][0] != 'W') return false;
  const std::string epoch(fields[0]);
  char* end = nullptr;
  const long long value = strtoll(epoch.c_str(), &end, 10);
  if (end == epoch.c_str() || *end != '\0' || value <= 0) return false;
  const std::string percent(fields[2]);
  const long p = strtol(percent.c_str(), &end, 10);
  if (end == percent.c_str() || *end != '\0' || p < 0 || p > 100) return false;
  if (fields[3] != "0" && fields[3] != "1") return false;
  if (count >= 5 && fields[4] != "0" && fields[4] != "1") return false;
  unsigned millivolts = 0;
  int cause = -1;
  if (count == 7) {
    const std::string mv(fields[5]);
    const unsigned long v = strtoul(mv.c_str(), &end, 10);
    if (mv.empty() || end == mv.c_str() || *end != '\0' || v > 65535) return false;
    millivolts = static_cast<unsigned>(v);
    if (fields[6].size() != 1) return false;
    if (fields[6][0] >= '0' && fields[6][0] <= '9') {
      cause = fields[6][0] - '0';
    } else if (fields[6][0] != '-') {
      return false;
    }
  }
  out.epoch = value;
  out.sleep = fields[1][0] == 'S';
  out.percent = static_cast<uint8_t>(p);
  out.usb = fields[3] == "1";
  out.lightKnown = count >= 5;
  out.light = count >= 5 && fields[4] == "1";
  out.millivolts = static_cast<uint16_t>(millivolts);
  out.wakeCause = static_cast<int8_t>(cause);
  return true;
}

void append(std::vector<Record>& records, const Record& r) {
  records.push_back(r);
  if (records.size() > MAX_RECORDS) records.erase(records.begin(), records.begin() + (records.size() - MAX_RECORDS));
}

namespace {

struct Sum {
  uint32_t drop = 0;
  uint64_t seconds = 0;
};

Rate rateOf(const Sum& sum) {
  Rate rate;
  rate.seconds = static_cast<uint32_t>(sum.seconds > 0xFFFFFFFFu ? 0xFFFFFFFFu : sum.seconds);
  if (sum.seconds == 0) return rate;
  if (sum.drop < 1 && sum.seconds < 6 * 3600) return rate;
  rate.known = true;
  rate.hundredthsPerHour =
      static_cast<uint32_t>((static_cast<uint64_t>(sum.drop) * 100 * 3600 + sum.seconds / 2) / sum.seconds);
  return rate;
}

}  // namespace

Stats compute(const std::vector<Record>& records) {
  Sum asleep;
  Sum awake;
  Sum awakeOff;
  Sum awakeOn;
  for (size_t i = 1; i < records.size(); ++i) {
    const Record& a = records[i - 1];
    const Record& b = records[i];
    if (a.sleep == b.sleep) continue;  // two sleeps or two wakes in a row: the middle one is missing
    if (a.epoch <= 0 || b.epoch <= a.epoch || b.epoch - a.epoch > MAX_PAIR_SECONDS) continue;
    if (a.usb || b.usb || b.percent > a.percent) continue;
    Sum& sum = a.sleep ? asleep : awake;
    const uint32_t drop = static_cast<uint32_t>(a.percent - b.percent);
    const uint64_t seconds = static_cast<uint64_t>(b.epoch - a.epoch);
    sum.drop += drop;
    sum.seconds += seconds;
    if (!a.sleep && b.lightKnown) {  // an awake pair ends at the S line that says if the light was on
      Sum& bySum = b.light ? awakeOn : awakeOff;
      bySum.drop += drop;
      bySum.seconds += seconds;
    }
  }
  return Stats{rateOf(asleep), rateOf(awake), rateOf(awakeOff), rateOf(awakeOn)};
}

}  // namespace batterylog
