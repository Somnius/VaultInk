// clang-format off
// HalStorage.h brings in SdFat, whose macros have to be seen before the network headers that
// TlsPolicyDevice.h includes (the same order OtaUpdater.cpp pins).
#include <HalStorage.h>
#include "TlsPolicyDevice.h"
// clang-format on

#include <Logging.h>
#include <TrustedTime.h>

#include "CaBundle.h"

namespace tlspolicy {

namespace {
// SecureHttpClient keeps the pointer it is given, so the text has to outlive every client: it is
// read once and kept until the next boot (a Wi-Fi session ends in a restart anyway).
std::string caPem;
constexpr size_t MAX_CA_FILE = 16 * 1024;
constexpr uint32_t CLOCK_SYNC_MS = 8000;
}  // namespace

bool hasOwnCa() { return Storage.exists(CA_FILE); }

bool apply(freeink::SecureHttpClient& http, const std::string& url, const bool userVerify, Error* error) {
  Clock clock;
  clock.now = [] { return trustedtime::trustedNow(); };
  clock.sync = [] { return trustedtime::syncNow(CLOCK_SYNC_MS); };
  CaFile caFile;
  caFile.exists = [] { return hasOwnCa(); };
  caFile.read = [](std::string& pem) { return Storage.readFileToString("TLS", CA_FILE, MAX_CA_FILE, pem); };

  const Decision d = decide(url, userVerify, clock, caFile, caPem);
  if (error) *error = d.error;
  if (!d.ok()) {
    LOG_ERR("TLS", "%s (%s)", errorText(d.error), hostOf(url).c_str());
    http.setCACert("");  // no roots: a request sent anyway fails closed
    return false;
  }
  switch (d.mode) {
    case Mode::CustomCa:
      http.setCACert(caPem.c_str());
      break;
    case Mode::Verify:
      http.setCACert(isPublicHost(hostOf(url)) ? ca_bundle::publicPem() : ca_bundle::PEM);
      break;
    case Mode::Insecure:
    case Mode::Plain:
    default:
      http.setInsecure();  // encrypted, not checked: the user's own server, check not turned on
      break;
  }
  return true;
}

}  // namespace tlspolicy
