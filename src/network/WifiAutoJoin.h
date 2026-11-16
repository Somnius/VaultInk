#pragma once

#include <algorithm>
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

// Scans started in a row before a failing scan is shown as a failure.
constexpr int MAX_SCAN_TRIES = 2;

// The saved networks of one scan in the order to try them: strongest first, one entry per name
// (its strongest access point), names that are not broadcast left out. `hiddenSsid` is a saved
// network known not to broadcast its name (joined through "Add hidden network"): a scan never
// lists it, so it is tried last. Empty when there is none.
template <typename IsSaved>
std::vector<std::string> savedInRange(const std::vector<Seen>& scan, IsSaved&& isSaved,
                                      const std::string& hiddenSsid = std::string()) {
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
  if (!hiddenSsid.empty() && isSaved(hiddenSsid) && std::find(order.begin(), order.end(), hiddenSsid) == order.end()) {
    order.push_back(hiddenSsid);
  }
  return order;
}

enum class Step : uint8_t {
  Join,    // connect to `ssid` without asking
  Rescan,  // the scan failed: scan once more
  List,    // show the network list with `notice`
};

// Why the network list is shown instead of a connection.
enum class Notice : uint8_t {
  None,            // the user asked for the list
  NoSavedInRange,  // the scan worked, no saved network is in reach
  SavedFailed,     // every saved network in reach was tried and none connected
  ScanFailed,      // the scan itself failed
};

struct Next {
  Step step = Step::List;
  std::string ssid;
  Notice notice = Notice::None;
};

// The next step of an automatic join. `scanTries` counts the scans started so far, `order` comes
// from savedInRange(), `attempted` holds the names already tried in this session.
inline Next nextStep(const bool scanFailed, const int scanTries, const std::vector<std::string>& order,
                     const std::vector<std::string>& attempted) {
  Next next;
  if (scanFailed) {
    if (scanTries < MAX_SCAN_TRIES) {
      next.step = Step::Rescan;
    } else {
      next.notice = Notice::ScanFailed;
    }
    return next;
  }
  for (const auto& ssid : order) {
    if (std::find(attempted.begin(), attempted.end(), ssid) == attempted.end()) {
      next.step = Step::Join;
      next.ssid = ssid;
      return next;
    }
  }
  next.notice = order.empty() ? Notice::NoSavedInRange : Notice::SavedFailed;
  return next;
}

}  // namespace wifijoin
