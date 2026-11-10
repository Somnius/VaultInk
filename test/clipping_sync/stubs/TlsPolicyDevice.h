#pragma once
#include <string>

#include "SecureHttpClient.h"

// The real one reads the card and the clock; the sync test only needs the call to succeed.
namespace tlspolicy {
inline bool apply(freeink::SecureHttpClient& http, const std::string&, bool) {
  http.setInsecure();
  return true;
}
}  // namespace tlspolicy
