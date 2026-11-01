#include "Presets.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace euc {

UnitSettings WheelPreset::units(const UnitSettings& global) const {
  UnitSettings u = global;
  if (speedUnit == 0 || speedUnit == 1) {
    u.speed = static_cast<SpeedUnit>(speedUnit);
  }
  if (tempUnit == 0 || tempUnit == 1) {
    u.temp = static_cast<TempUnit>(tempUnit);
  }
  if (distanceUnit == 0 || distanceUnit == 1) {
    u.distance = static_cast<DistanceUnit>(distanceUnit);
  }
  return u;
}

bool validRefresh(const uint8_t seconds) {
  return std::any_of(std::begin(kRefreshChoices), std::end(kRefreshChoices),
                     [&](const uint8_t c) { return c == seconds; });
}

bool validAddress(const char* s) {
  if (s == nullptr || std::strlen(s) != 17) {
    return false;
  }
  for (size_t i = 0; i < 17; ++i) {
    if (i % 3 == 2) {
      if (s[i] != ':') {
        return false;
      }
    } else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) {
      return false;
    }
  }
  return true;
}

namespace {
bool sameAddress(const char* a, const char* b) {
  for (; *a != '\0' && *b != '\0'; ++a, ++b) {
    if (std::toupper(static_cast<unsigned char>(*a)) != std::toupper(static_cast<unsigned char>(*b))) {
      return false;
    }
  }
  return *a == '\0' && *b == '\0';
}
}  // namespace

WheelPreset* PresetFile::findByAddress(const char* address) {
  const auto it =
      std::find_if(wheels, wheels + count, [&](const WheelPreset& w) { return sameAddress(w.address, address); });
  return it == wheels + count ? nullptr : it;
}

const WheelPreset* PresetFile::findByAddress(const char* address) const {
  const auto it =
      std::find_if(wheels, wheels + count, [&](const WheelPreset& w) { return sameAddress(w.address, address); });
  return it == wheels + count ? nullptr : it;
}

bool PresetFile::upsert(const WheelPreset& p) {
  if (WheelPreset* existing = findByAddress(p.address)) {
    *existing = p;
    return true;
  }
  if (count >= kMaxWheels) {
    return false;
  }
  wheels[count++] = p;
  return true;
}

bool PresetFile::remove(const size_t index) {
  if (index >= count) {
    return false;
  }
  std::move(wheels + index + 1, wheels + count, wheels + index);
  --count;
  wheels[count] = WheelPreset();
  return true;
}

namespace {
struct Out {
  char* buf;
  size_t cap;
  size_t pos = 0;  // characters wanted so far

  void add(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};

void Out::add(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const size_t room = pos < cap ? cap - pos : 0;
  const int n = vsnprintf(room > 0 ? buf + pos : nullptr, room, fmt, ap);
  va_end(ap);
  if (n > 0) {
    pos += static_cast<size_t>(n);
  }
  if (room > 0 && pos >= cap) {
    buf[cap - 1] = '\0';
  }
}

// Values are one line each; a newline or '=' inside a text field would break the format, so they become spaces.
void sanitize(char* dst, const size_t cap, const char* src) {
  size_t n = 0;
  for (; src[n] != '\0' && n + 1 < cap; ++n) {
    const char c = src[n];
    dst[n] = (c == '\n' || c == '\r' || c == '=') ? ' ' : c;
  }
  dst[n] = '\0';
}
}  // namespace

int serializePresets(const PresetFile& f, char* buf, const size_t cap) {
  Out o{buf, cap};
  if (cap > 0) {
    buf[0] = '\0';
  }
  o.add("version=%u\n", PresetFile::kVersion);
  for (size_t i = 0; i < f.count && i < PresetFile::kMaxWheels; ++i) {
    const WheelPreset& w = f.wheels[i];
    char text[24];
    o.add("[wheel]\n");
    sanitize(text, sizeof(text), w.nickname);
    o.add("nickname=%s\n", text);
    sanitize(text, sizeof(text), w.maker);
    o.add("maker=%s\n", text);
    sanitize(text, sizeof(text), w.model);
    o.add("model=%s\n", text);
    o.add("brand=%u\n", static_cast<unsigned>(w.brand));
    o.add("address=%s\n", w.address);
    o.add("addressType=%u\n", w.addressType);
    o.add("speedUnit=%d\n", w.speedUnit);
    o.add("tempUnit=%d\n", w.tempUnit);
    o.add("distanceUnit=%d\n", w.distanceUnit);
    o.add("alarmPwm=%.1f\n", static_cast<double>(w.alarms.pwmPct));
    o.add("alarmSpeed=%.1f\n", static_cast<double>(w.alarms.speedKmh));
    o.add("alarmTemp=%.1f\n", static_cast<double>(w.alarms.tempC));
    o.add("alarmCurrent=%.1f\n", static_cast<double>(w.alarms.currentA));
    o.add("alarmBattery=%.1f\n", static_cast<double>(w.alarms.lowBatteryPct));
    o.add("alarmWheel=%u\n", w.alarms.wheelAlarms ? 1u : 0u);
    o.add("refresh=%u\n", w.refreshSeconds);
    o.add("lastConnected=%lld\n", static_cast<long long>(w.lastConnected));
  }
  return static_cast<int>(o.pos);
}

namespace {
constexpr size_t kMaxValue = 64;

struct ParseState {
  PresetFile* out;
  WheelPreset current;
  bool inWheel = false;
  bool versionSeen = false;
  bool dropped = false;  // the current block is past kMaxWheels

  void flush() {
    if (inWheel && !dropped && validAddress(current.address) && current.nickname[0] != '\0') {
      if (out->count < PresetFile::kMaxWheels) {
        out->wheels[out->count++] = current;
      }
    }
    current = WheelPreset();
    inWheel = false;
  }
};

void copyText(char* dst, const size_t cap, const char* v) { snprintf(dst, cap, "%s", v); }

bool parseInt(const char* v, long long& out) {
  char* end = nullptr;
  out = std::strtoll(v, &end, 10);
  return end != v && *end == '\0';
}

bool parseFloat(const char* v, float& out) {
  char* end = nullptr;
  const double d = std::strtod(v, &end);
  if (end == v || *end != '\0') {
    return false;
  }
  out = static_cast<float>(d);
  return true;
}

void setFloat(const char* v, float& field, const float lo, const float hi) {
  float f = 0.0f;
  if (parseFloat(v, f) && f >= lo && f <= hi) {
    field = f;
  }
}

void setUnit(const char* v, int8_t& field) {
  long long n = 0;
  if (parseInt(v, n) && n >= -1 && n <= 1) {
    field = static_cast<int8_t>(n);
  }
}

void keyValue(ParseState& st, const char* key, const char* value) {
  if (std::strcmp(key, "version") == 0) {
    st.versionSeen = true;
    return;
  }
  if (!st.inWheel) {
    return;
  }
  WheelPreset& w = st.current;
  long long n = 0;
  if (std::strcmp(key, "nickname") == 0) {
    copyText(w.nickname, sizeof(w.nickname), value);
  } else if (std::strcmp(key, "maker") == 0) {
    copyText(w.maker, sizeof(w.maker), value);
  } else if (std::strcmp(key, "model") == 0) {
    copyText(w.model, sizeof(w.model), value);
  } else if (std::strcmp(key, "brand") == 0) {
    if (parseInt(value, n) && n >= 0 && n < static_cast<long long>(Brand::Count)) {
      w.brand = static_cast<Brand>(n);
    }
  } else if (std::strcmp(key, "address") == 0) {
    if (validAddress(value)) {
      copyText(w.address, sizeof(w.address), value);
    }
  } else if (std::strcmp(key, "addressType") == 0) {
    if (parseInt(value, n) && (n == 0 || n == 1)) {
      w.addressType = static_cast<uint8_t>(n);
    }
  } else if (std::strcmp(key, "speedUnit") == 0) {
    setUnit(value, w.speedUnit);
  } else if (std::strcmp(key, "tempUnit") == 0) {
    setUnit(value, w.tempUnit);
  } else if (std::strcmp(key, "distanceUnit") == 0) {
    setUnit(value, w.distanceUnit);
  } else if (std::strcmp(key, "alarmPwm") == 0) {
    setFloat(value, w.alarms.pwmPct, 0.0f, 100.0f);
  } else if (std::strcmp(key, "alarmSpeed") == 0) {
    setFloat(value, w.alarms.speedKmh, 0.0f, 200.0f);
  } else if (std::strcmp(key, "alarmTemp") == 0) {
    setFloat(value, w.alarms.tempC, 0.0f, 150.0f);
  } else if (std::strcmp(key, "alarmCurrent") == 0) {
    setFloat(value, w.alarms.currentA, 0.0f, 500.0f);
  } else if (std::strcmp(key, "alarmBattery") == 0) {
    setFloat(value, w.alarms.lowBatteryPct, 0.0f, 100.0f);
  } else if (std::strcmp(key, "alarmWheel") == 0) {
    if (parseInt(value, n) && (n == 0 || n == 1)) {
      w.alarms.wheelAlarms = n == 1;
    }
  } else if (std::strcmp(key, "refresh") == 0) {
    if (parseInt(value, n) && n > 0 && n < 256 && validRefresh(static_cast<uint8_t>(n))) {
      w.refreshSeconds = static_cast<uint8_t>(n);
    }
  } else if (std::strcmp(key, "lastConnected") == 0) {
    if (parseInt(value, n) && n >= 0) {
      w.lastConnected = n;
    }
  }
  // unknown keys: ignored
}

void parseLine(ParseState& st, const char* s, size_t n) {
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
    --n;
  }
  while (n > 0 && (*s == ' ' || *s == '\t')) {
    ++s;
    --n;
  }
  if (n == 0 || *s == '#') {
    return;
  }
  if (n == 7 && std::strncmp(s, "[wheel]", 7) == 0) {
    st.flush();
    st.inWheel = true;
    st.dropped = st.out->count >= PresetFile::kMaxWheels;
    return;
  }
  const char* eq = static_cast<const char*>(std::memchr(s, '=', n));
  if (eq == nullptr) {
    return;
  }
  char key[24];
  char value[kMaxValue];
  size_t klen = static_cast<size_t>(eq - s);
  while (klen > 0 && (s[klen - 1] == ' ' || s[klen - 1] == '\t')) {
    --klen;
  }
  klen = std::min(klen, sizeof(key) - 1);
  std::memcpy(key, s, klen);
  key[klen] = '\0';
  const char* v = eq + 1;
  while (v < s + n && (*v == ' ' || *v == '\t')) {
    ++v;
  }
  const size_t vlen = std::min(static_cast<size_t>(s + n - v), sizeof(value) - 1);
  std::memcpy(value, v, vlen);
  value[vlen] = '\0';
  keyValue(st, key, value);
}
}  // namespace

bool parsePresets(const char* text, const size_t size, PresetFile& out) {
  out = PresetFile();
  ParseState st{&out, WheelPreset(), false, false, false};
  size_t start = 0;
  for (size_t i = 0; i <= size; ++i) {
    if (i == size || text[i] == '\n' || text[i] == '\0') {
      parseLine(st, text + start, i - start);
      start = i + 1;
      if (i < size && text[i] == '\0') {
        break;  // embedded NUL: the rest is not text
      }
    }
  }
  st.flush();
  return st.versionSeen && out.count > 0;
}

}  // namespace euc
