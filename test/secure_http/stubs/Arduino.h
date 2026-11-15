#pragma once
// Host stub: a clock that only delay() moves, and the little of Arduino SecureHttpClient touches.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

inline unsigned long& fakeNow() {
  static unsigned long now = 0;
  return now;
}
inline unsigned long millis() { return fakeNow(); }
inline void delay(unsigned long ms) { fakeNow() += ms; }

class String {
 public:
  String(const char* s = "") : s_(s) {}
  const char* c_str() const { return s_.c_str(); }

 private:
  std::string s_;
};

class IPAddress {};
