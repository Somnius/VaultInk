#include "Focus.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace focus {

namespace {

constexpr int64_t SECONDS_PER_MINUTE = 60;
// Any phase longer than this in the save file is treated as corrupt.
constexpr int64_t MAX_PHASE_SEC = 90 * SECONDS_PER_MINUTE;

template <typename T>
T clampTo(T v, T lo, T hi) {
  return std::min(std::max(v, lo), hi);
}

// Signed decimal with optional '-'; false on anything else (or overflow of ~18 digits).
bool parseInt(const char* s, size_t len, int64_t& out) {
  if (len == 0) {
    return false;
  }
  size_t i = 0;
  bool neg = false;
  if (s[0] == '-') {
    neg = true;
    i = 1;
  }
  if (i >= len || len - i > 18) {
    return false;
  }
  int64_t v = 0;
  for (; i < len; ++i) {
    const char c = s[i];
    if (c < '0' || c > '9') {
      return false;
    }
    v = v * 10 + (c - '0');
  }
  out = neg ? -v : v;
  return true;
}

bool keyIs(const char* key, size_t keyLen, const char* name) {
  return keyLen == std::strlen(name) && std::memcmp(key, name, keyLen) == 0;
}

}  // namespace

Config clampConfig(const Config& c) {
  Config out = c;
  out.focusMin = clampTo<uint8_t>(c.focusMin, 5, 90);
  out.shortMin = clampTo<uint8_t>(c.shortMin, 1, 30);
  out.longMin = clampTo<uint8_t>(c.longMin, 5, 60);
  out.cyclesBeforeLong = clampTo<uint8_t>(c.cyclesBeforeLong, 2, 8);
  return out;
}

const char* phaseKey(Phase p) {
  switch (p) {
    case Phase::Focus:
      return "focus";
    case Phase::ShortBreak:
      return "short";
    case Phase::LongBreak:
      return "long";
    case Phase::Idle:
    default:
      return "idle";
  }
}

bool phaseFromKey(const char* key, Phase& out) {
  constexpr Phase all[] = {Phase::Idle, Phase::Focus, Phase::ShortBreak, Phase::LongBreak};
  const auto it =
      std::find_if(std::begin(all), std::end(all), [key](Phase p) { return std::strcmp(phaseKey(p), key) == 0; });
  if (it == std::end(all)) {
    return false;
  }
  out = *it;
  return true;
}

uint16_t phaseMinutes(const Config& c, Phase p) {
  switch (p) {
    case Phase::Focus:
      return c.focusMin;
    case Phase::ShortBreak:
      return c.shortMin;
    case Phase::LongBreak:
      return c.longMin;
    case Phase::Idle:
    default:
      return 0;
  }
}

// ---- Timer ----

void Timer::reset() {
  cur = Phase::Idle;
  isPaused = false;
  startAt = 0;
  endAt = 0;
  pausedAt = 0;
  pausedTotal = 0;
}

void Timer::start(Phase p, int64_t now) {
  if (p == Phase::Idle) {
    stop();
    return;
  }
  cur = p;
  isPaused = false;
  startAt = now;
  endAt = now + static_cast<int64_t>(phaseMinutes(cfg, p)) * SECONDS_PER_MINUTE;
  pausedAt = 0;
  pausedTotal = 0;
}

void Timer::pause(int64_t now) {
  if (!running()) {
    return;
  }
  isPaused = true;
  pausedAt = clampTo<int64_t>(now, startAt, endAt);
}

void Timer::resume(int64_t now) {
  if (!paused()) {
    return;
  }
  const int64_t gap = std::max<int64_t>(0, now - pausedAt);
  endAt += gap;
  pausedTotal = static_cast<uint32_t>(std::min<int64_t>(pausedTotal + gap, UINT32_MAX));
  isPaused = false;
  pausedAt = 0;
}

void Timer::stop() {
  // An aborted phase is what Start offers again.
  if (cur != Phase::Idle) {
    next = cur;
  }
  reset();
}

Phase Timer::phaseAfter(Phase ended) const {
  if (ended == Phase::Focus) {
    return cycle >= cfg.cyclesBeforeLong ? Phase::LongBreak : Phase::ShortBreak;
  }
  return Phase::Focus;
}

void Timer::finishPhase(int64_t at) {
  const Phase ended = cur;
  if (ended == Phase::Focus) {
    doneToday = static_cast<uint16_t>(std::min<uint32_t>(doneToday + 1u, UINT16_MAX));
    doneTotal = std::min<uint32_t>(doneTotal + 1u, UINT32_MAX);
    ++cycle;
  }
  next = phaseAfter(ended);
  if (ended == Phase::LongBreak) {
    cycle = 0;
  }
  if (cfg.autoNext) {
    start(next, at);
  } else {
    reset();
  }
}

Event Timer::skip(int64_t now) {
  if (cur == Phase::Idle) {
    return Event::None;
  }
  finishPhase(now);
  return Event::PhaseEnded;
}

Event Timer::update(int64_t now) {
  if (!running() || now < endAt) {
    return Event::None;
  }
  // Start the next phase at `now`, not at the old end: after hours asleep only one phase ends.
  finishPhase(now);
  return Event::PhaseEnded;
}

uint32_t Timer::durationSec() const {
  return static_cast<uint32_t>(phaseMinutes(cfg, cur)) * static_cast<uint32_t>(SECONDS_PER_MINUTE);
}

uint32_t Timer::remainingSec(int64_t now) const {
  if (cur == Phase::Idle) {
    return 0;
  }
  const int64_t ref = isPaused ? pausedAt : now;
  const int64_t left = endAt - ref;
  return static_cast<uint32_t>(clampTo<int64_t>(left, 0, static_cast<int64_t>(durationSec())));
}

uint32_t Timer::elapsedSec(int64_t now) const {
  if (cur == Phase::Idle) {
    return 0;
  }
  return durationSec() - remainingSec(now);
}

size_t Timer::serialize(char* buf, size_t cap) const {
  if (buf == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(
      buf, cap,
      "v=%u\n"
      "phase=%s\n"
      "next=%s\n"
      "paused=%u\n"
      "start=%lld\n"
      "end=%lld\n"
      "pausedAt=%lld\n"
      "pausedTotal=%lu\n"
      "cycle=%u\n"
      "today=%u\n"
      "total=%lu\n"
      "focusMin=%u\n"
      "shortMin=%u\n"
      "longMin=%u\n"
      "cycles=%u\n"
      "autoNext=%u\n",
      static_cast<unsigned>(SAVE_VERSION), phaseKey(cur), phaseKey(next), isPaused ? 1u : 0u,
      static_cast<long long>(startAt), static_cast<long long>(endAt), static_cast<long long>(pausedAt),
      static_cast<unsigned long>(pausedTotal), static_cast<unsigned>(cycle), static_cast<unsigned>(doneToday),
      static_cast<unsigned long>(doneTotal), static_cast<unsigned>(cfg.focusMin), static_cast<unsigned>(cfg.shortMin),
      static_cast<unsigned>(cfg.longMin), static_cast<unsigned>(cfg.cyclesBeforeLong), cfg.autoNext ? 1u : 0u);
  if (n <= 0 || static_cast<size_t>(n) >= cap) {
    buf[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

bool Timer::deserialize(const char* data, size_t size) {
  Timer t;
  t.cfg = cfg;
  bool versionOk = false;
  if (data == nullptr) {
    *this = t;
    return false;
  }
  size_t pos = 0;
  while (pos < size) {
    const char* line = data + pos;
    const size_t rest = size - pos;
    const char* nl = static_cast<const char*>(std::memchr(line, '\n', rest));
    const size_t lineLen = nl != nullptr ? static_cast<size_t>(nl - line) : rest;
    pos += lineLen + 1;
    if (lineLen == 0 || line[0] == '\0') {
      if (line[0] == '\0') {
        break;
      }
      continue;
    }
    const char* eq = static_cast<const char*>(std::memchr(line, '=', lineLen));
    if (eq == nullptr) {
      continue;
    }
    const size_t keyLen = static_cast<size_t>(eq - line);
    const char* val = eq + 1;
    size_t valLen = lineLen - keyLen - 1;
    if (valLen > 0 && val[valLen - 1] == '\r') {
      --valLen;
    }
    int64_t num = 0;
    const bool isNum = parseInt(val, valLen, num);
    if (keyIs(line, keyLen, "v")) {
      versionOk = isNum && num == SAVE_VERSION;
    } else if (keyIs(line, keyLen, "phase") || keyIs(line, keyLen, "next")) {
      char key[8] = {};
      if (valLen < sizeof(key)) {
        std::memcpy(key, val, valLen);
        Phase p = Phase::Idle;
        if (phaseFromKey(key, p)) {
          (keyIs(line, keyLen, "phase") ? t.cur : t.next) = p;
        }
      }
    } else if (!isNum) {
      continue;
    } else if (keyIs(line, keyLen, "paused")) {
      t.isPaused = num != 0;
    } else if (keyIs(line, keyLen, "start")) {
      t.startAt = num;
    } else if (keyIs(line, keyLen, "end")) {
      t.endAt = num;
    } else if (keyIs(line, keyLen, "pausedAt")) {
      t.pausedAt = num;
    } else if (keyIs(line, keyLen, "pausedTotal")) {
      t.pausedTotal = static_cast<uint32_t>(clampTo<int64_t>(num, 0, UINT32_MAX));
    } else if (keyIs(line, keyLen, "cycle")) {
      t.cycle = static_cast<uint8_t>(clampTo<int64_t>(num, 0, 8));
    } else if (keyIs(line, keyLen, "today")) {
      t.doneToday = static_cast<uint16_t>(clampTo<int64_t>(num, 0, UINT16_MAX));
    } else if (keyIs(line, keyLen, "total")) {
      t.doneTotal = static_cast<uint32_t>(clampTo<int64_t>(num, 0, UINT32_MAX));
    } else if (keyIs(line, keyLen, "focusMin")) {
      t.cfg.focusMin = static_cast<uint8_t>(clampTo<int64_t>(num, 0, UINT8_MAX));
    } else if (keyIs(line, keyLen, "shortMin")) {
      t.cfg.shortMin = static_cast<uint8_t>(clampTo<int64_t>(num, 0, UINT8_MAX));
    } else if (keyIs(line, keyLen, "longMin")) {
      t.cfg.longMin = static_cast<uint8_t>(clampTo<int64_t>(num, 0, UINT8_MAX));
    } else if (keyIs(line, keyLen, "cycles")) {
      t.cfg.cyclesBeforeLong = static_cast<uint8_t>(clampTo<int64_t>(num, 0, UINT8_MAX));
    } else if (keyIs(line, keyLen, "autoNext")) {
      t.cfg.autoNext = num != 0;
    }
  }
  t.cfg = clampConfig(t.cfg);
  if (t.next == Phase::Idle) {
    t.next = Phase::Focus;
  }
  if (t.cycle >= t.cfg.cyclesBeforeLong) {
    t.cycle = 0;
  }
  bool stateOk = versionOk;
  if (t.cur != Phase::Idle) {
    const int64_t len = t.endAt - t.startAt;
    const bool sane = len > 0 && len <= MAX_PHASE_SEC + static_cast<int64_t>(t.pausedTotal) &&
                      (!t.isPaused || (t.pausedAt >= t.startAt && t.pausedAt <= t.endAt));
    if (!sane) {
      t.reset();
      stateOk = false;
    }
  } else {
    t.reset();
  }
  *this = t;
  return stateOk;
}

size_t formatLogLine(char* buf, size_t cap, int64_t epoch, Phase phase, uint16_t minutes, bool completed) {
  if (buf == nullptr || cap == 0) {
    return 0;
  }
  const int n = std::snprintf(buf, cap, "%lld\t%s\t%u\t%u\n", static_cast<long long>(epoch), phaseKey(phase),
                              static_cast<unsigned>(minutes), completed ? 1u : 0u);
  if (n <= 0 || static_cast<size_t>(n) >= cap) {
    buf[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(n);
}

}  // namespace focus
