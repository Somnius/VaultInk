#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Which saved Wi-Fi network to join when a job needs the network, and what to show when none
// can be joined. No radio and no store in here, so computer tests cover it (test/wifi_auto_join).
namespace wifijoin {

struct Seen {
  std::string ssid;
  int32_t rssi;
};

// Scans started in a row before a scan that failed or found nothing is taken as it is.
constexpr int MAX_SCAN_TRIES = 2;

// Saved networks tried without having been seen, after a scan that failed or found nothing.
constexpr size_t MAX_BLIND_TRIES = 3;

// What one scan gave.
enum class Scan : uint8_t {
  Ok,      // at least one network
  Empty,   // it ran and listed nothing (seen on a reader with the home network in range)
  Failed,  // it did not run
};

// The saved networks of one scan in the order to try them: strongest first, one entry per name
// (its strongest access point), names that are not broadcast left out. `lastSsid` is the last
// connected network: when it is still saved and the scan does not list it, it gets one try at the
// end, because a network that hides its name is never listed. Empty when there is none.
template <typename IsSaved>
std::vector<std::string> savedInRange(const std::vector<Seen>& scan, IsSaved&& isSaved,
                                      const std::string& lastSsid = std::string()) {
  std::vector<Seen> best;
  for (const auto& seen : scan) {
    if (seen.ssid.empty() || !isSaved(seen.ssid)) continue;
    const auto it = std::find_if(best.begin(), best.end(), [&seen](const Seen& b) { return b.ssid == seen.ssid; });
    if (it == best.end()) {
      best.push_back(seen);
    } else if (seen.rssi > it->rssi) {
      it->rssi = seen.rssi;
    }
  }
  std::stable_sort(best.begin(), best.end(), [](const Seen& a, const Seen& b) { return a.rssi > b.rssi; });
  std::vector<std::string> order;
  order.reserve(best.size() + 1);
  for (const auto& seen : best) order.push_back(seen.ssid);
  if (!lastSsid.empty() && isSaved(lastSsid) && std::find(order.begin(), order.end(), lastSsid) == order.end()) {
    order.push_back(lastSsid);
  }
  return order;
}

// The saved networks to try when no scan result can be used: the last connected one first, then
// the others as saved, MAX_BLIND_TRIES at most.
inline std::vector<std::string> blindOrder(const std::vector<std::string>& saved, const std::string& lastSsid) {
  std::vector<std::string> order;
  if (!lastSsid.empty() && std::find(saved.begin(), saved.end(), lastSsid) != saved.end()) order.push_back(lastSsid);
  for (const auto& ssid : saved) {
    if (order.size() >= MAX_BLIND_TRIES) break;
    if (ssid.empty() || std::find(order.begin(), order.end(), ssid) != order.end()) continue;
    order.push_back(ssid);
  }
  if (order.size() > MAX_BLIND_TRIES) order.resize(MAX_BLIND_TRIES);
  return order;
}

enum class Step : uint8_t {
  Join,    // connect to `ssid` without asking
  Rescan,  // the scan failed or found nothing: scan once more
  List,    // show the network list with `notice`
};

// Why the network list is shown instead of a connection.
enum class Notice : uint8_t {
  None,            // the user asked for the list
  NoSavedInRange,  // no saved network is in reach
  SavedFailed,     // every saved network in reach was tried and none connected
  ScanFailed,      // the scan itself failed
};

struct Next {
  Step step = Step::List;
  std::string ssid;
  Notice notice = Notice::None;
};

// The next step of an automatic join. `scanTries` counts the scans started so far; `candidates`
// comes from savedInRange() after a scan that listed networks and from blindOrder() otherwise;
// `attempted` holds the names already tried in this session.
inline Next nextStep(const Scan scan, const int scanTries, const std::vector<std::string>& candidates,
                     const std::vector<std::string>& attempted) {
  Next next;
  if (scan != Scan::Ok && scanTries < MAX_SCAN_TRIES) {
    next.step = Step::Rescan;
    return next;
  }
  for (const auto& ssid : candidates) {
    if (std::find(attempted.begin(), attempted.end(), ssid) == attempted.end()) {
      next.step = Step::Join;
      next.ssid = ssid;
      return next;
    }
  }
  if (scan == Scan::Failed) {
    next.notice = Notice::ScanFailed;
  } else if (scan == Scan::Empty || candidates.empty()) {
    next.notice = Notice::NoSavedInRange;
  } else {
    next.notice = Notice::SavedFailed;
  }
  return next;
}

}  // namespace wifijoin
