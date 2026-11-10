#pragma once

#include <Wikipedia.h>

#include <string>

// One Wikipedia page summary over HTTPS. Wi-Fi must be up already; the certificate is always
// checked (lib/TlsPolicy), and no more than wikipedia::MAX_BODY_BYTES of the answer are read.
namespace wikipedia_client {

enum class Result : uint8_t {
  Ok,              // `out` holds a title and an extract
  NotFound,        // no article by that name
  Disambiguation,  // the name covers several articles
  NoTitle,         // the selection gives no usable title
  LowMemory,       // not enough free heap for a TLS session
  NoClock,         // the date could not be set, so the certificate cannot be checked
  Network,         // no answer (DNS, connect, the certificate check, a timeout)
  BadAnswer,       // an answer that is not a summary
};

Result fetch(const std::string& language, const std::string& text, wikipedia::Summary& out);

}  // namespace wikipedia_client
