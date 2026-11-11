#pragma once

#include <HighlightExport.h>

#include <memory>
#include <string>

namespace freeink {
class SecureHttpClient;
}

// Posts highlight bodies (lib/HighlightExport) to Readwise or to the user's Memos server. Wi-Fi
// must be up already. One client is one job: the connection is kept between its requests.
//
// Readwise's certificate is always checked; the Memos server's when its Check certificate switch
// is on (lib/TlsPolicy). Redirects are not followed, so a token is only ever sent to the address
// it was set up for. The token is never logged.
namespace highlight_export_client {

enum class Setup : uint8_t {
  Ok,
  NotSetUp,   // no token, or no usable Memos address
  LowMemory,  // not enough free heap for a TLS session
  NoClock,    // the date could not be set, so the certificate cannot be checked
  NoTls,      // ca.pem could not be read
};

class Job {
 public:
  Job();
  ~Job();
  Job(const Job&) = delete;
  Job& operator=(const Job&) = delete;

  // Reads the token and address from EXPORT_STORE and sets up the certificate check.
  Setup begin(highlightexport::Destination destination);
  // One request with a JSON body.
  highlightexport::Outcome post(const std::string& body);
  void end();

 private:
  std::unique_ptr<freeink::SecureHttpClient> http_;
  std::string url_;
  std::string authorization_;
};

}  // namespace highlight_export_client
