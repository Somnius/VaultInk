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

// Tries a saved network gets in one automatic join when its first try ran out of time. One 7 s try
// timed out on a reader with the home network listed at full signal (2026-11-18, joins there take
// about 3.7 s), and the join by hand right after connected.
constexpr int MAX_JOIN_TRIES = 2;

// A second try starts only while it can end within this time from the start of the automatic join
// (scans included). First tries are not cut by it: every saved network in range gets one, as
// before. So with one saved network that never answers the list is up after two 7 s tries plus
// the scan, about 17 s; second tries never run past 30 s; and when the first tries alone take
// 23 s or more (four networks that all time out) there is no second try at all.
constexpr uint32_t SECOND_TRY_BUDGET_MS = 30000;

inline bool secondTryFits(const uint32_t elapsedMs, const uint32_t tryMs) {
  return elapsedMs <= SECOND_TRY_BUDGET_MS && tryMs <= SECOND_TRY_BUDGET_MS - elapsedMs;
}

// The radio's reasons for a disconnect (wifi_err_reason_t) that this file acts on.
constexpr uint8_t REASON_ASSOC_LEAVE = 8;  // the reader left by itself, as before every try
constexpr uint8_t REASON_MIC_FAILURE = 14;
constexpr uint8_t REASON_4WAY_HANDSHAKE_TIMEOUT = 15;
constexpr uint8_t REASON_IE_IN_4WAY_DIFFERS = 17;
constexpr uint8_t REASON_CIPHER_SUITE_REJECTED = 24;
constexpr uint8_t REASON_802_1X_AUTH_FAILED = 23;
constexpr uint8_t REASON_BAD_CIPHER_OR_AKM = 29;
constexpr uint8_t REASON_NO_AP_FOUND = 201;
constexpr uint8_t REASON_AUTH_FAIL = 202;
constexpr uint8_t REASON_HANDSHAKE_TIMEOUT = 204;

// Whether a try that ran out of time with this disconnect reason is worth a second one. No for
// the reasons that say the network answered and the password or its security settings did not
// fit (a wrong password can sit at "disconnected" until the 7 s are over, so the status does not
// tell), and for "no such network". Yes for no reason at all (0, the case seen on the reader is
// not known to have had one) and for the ones that also mean the access point just did not
// answer in time: authentication expired (2), association failed (203), beacon timeout (200).
constexpr bool retryAfterTimeout(const uint8_t reason) {
  return !(reason == REASON_MIC_FAILURE || reason == REASON_4WAY_HANDSHAKE_TIMEOUT ||
           (reason >= REASON_IE_IN_4WAY_DIFFERS && reason <= REASON_CIPHER_SUITE_REJECTED) ||
           reason == REASON_BAD_CIPHER_OR_AKM || reason == REASON_NO_AP_FOUND || reason == REASON_AUTH_FAIL ||
           reason == REASON_HANDSHAKE_TIMEOUT);
}

// The reason to keep for the current try when the radio reports `incoming`: the reader's own
// leave is ignored (it is the disconnect that prepares the try and can arrive after the try's
// reason was cleared), and a reason that rules out a second try is not replaced by a later one.
constexpr uint8_t mergeReason(const uint8_t kept, const uint8_t incoming) {
  if (incoming == REASON_ASSOC_LEAVE) return kept;
  return retryAfterTimeout(kept) ? incoming : kept;
}

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
// `attempted` holds the names already tried in this session, one entry per try; `timedOut` the
// names whose try ended without an answer from the network (not refused, not "no such network",
// and retryAfterTimeout() of its disconnect reason). Those get tries up to MAX_JOIN_TRIES, after
// every candidate had its first, and only with `secondTries` (secondTryFits() of the time so far).
inline Next nextStep(const Scan scan, const int scanTries, const std::vector<std::string>& candidates,
                     const std::vector<std::string>& attempted, const std::vector<std::string>& timedOut = {},
                     const bool secondTries = true) {
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
  for (const auto& ssid : candidates) {
    if (secondTries && std::find(timedOut.begin(), timedOut.end(), ssid) != timedOut.end() &&
        std::count(attempted.begin(), attempted.end(), ssid) < MAX_JOIN_TRIES) {
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
