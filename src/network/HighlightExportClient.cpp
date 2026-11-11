#include "HighlightExportClient.h"

#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <TlsPolicyDevice.h>

#include "HighlightExportStore.h"
#include "WifiPowerSaveGuard.h"

namespace highlight_export_client {

namespace {
constexpr uint32_t TIMEOUT_MS = 20000;
// The floor for a TLS session, as in WikipediaClient.
constexpr size_t MIN_TLS_FREE_HEAP = 40000;
constexpr size_t MIN_TLS_MAX_ALLOC = 20000;
// An answer is only read for its status; no more than this much of its body is taken off the wire.
constexpr size_t MAX_ANSWER_BYTES = 16 * 1024;
}  // namespace

Job::Job() = default;
Job::~Job() { end(); }

Setup Job::begin(const highlightexport::Destination destination) {
  end();
  bool verify = false;
  if (destination == highlightexport::Destination::Readwise) {
    if (!EXPORT_STORE.hasReadwise()) return Setup::NotSetUp;
    url_ = highlightexport::READWISE_URL;
    authorization_ = "Token " + EXPORT_STORE.readwiseToken();
  } else {
    if (!EXPORT_STORE.hasMemos()) return Setup::NotSetUp;
    url_ = highlightexport::memosUrl(EXPORT_STORE.memosUrl());
    if (url_.empty()) return Setup::NotSetUp;
    authorization_ = "Bearer " + EXPORT_STORE.memosToken();
    verify = EXPORT_STORE.memosVerifyCert();
  }

  const auto heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < MIN_TLS_FREE_HEAP || heap.largestBlockBytes < MIN_TLS_MAX_ALLOC) {
    LOG_ERR("EXP", "Low heap for TLS: %u free, %u largest", static_cast<unsigned>(heap.freeBytes),
            static_cast<unsigned>(heap.largestBlockBytes));
    return Setup::LowMemory;
  }
  http_ = makeUniqueNoThrow<freeink::SecureHttpClient>();
  if (!http_) return Setup::LowMemory;
  http_->setTimeout(TIMEOUT_MS);
  http_->setUserAgent("VaultInk/" CROSSPOINT_VERSION " (https://github.com/Somnius/VaultInk)");
  http_->setFollowRedirects(0);  // a redirect must not carry the token to another address
  tlspolicy::Error tlsError = tlspolicy::Error::None;
  if (!tlspolicy::apply(*http_, url_, verify, &tlsError)) {
    http_.reset();
    return tlsError == tlspolicy::Error::NoClock ? Setup::NoClock : Setup::NoTls;
  }
  return Setup::Ok;
}

highlightexport::Outcome Job::post(const std::string& body) {
  if (!http_) return highlightexport::mapResponse(-1, "");
  WifiPowerSaveGuard psGuard;
  if (!http_->begin(url_)) return highlightexport::mapResponse(-1, "");
  http_->addHeader("Authorization", authorization_);
  http_->addHeader("Content-Type", "application/json");
  http_->addHeader("Accept", "application/json");
  size_t taken = 0;
  const int status = http_->sendRequest("POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(),
                                        [&taken](const uint8_t*, const size_t length) {
                                          taken += length;
                                          return taken <= MAX_ANSWER_BYTES;
                                        });
  // Stopping at the size limit can make the client report a failed transfer although the status
  // line was read.
  const int answered = http_->getStatus() > 0 ? http_->getStatus() : status;
  const std::string retryAfter = http_->getHeader("retry-after");
  // The address may hold a user name, so only the host and the status go to the log.
  LOG_DBG("EXP", "POST %s: %d (%u bytes sent)", tlspolicy::hostOf(url_).c_str(), answered,
          static_cast<unsigned>(body.size()));
  return highlightexport::mapResponse(answered, retryAfter);
}

void Job::end() {
  if (http_) http_->end();
  http_.reset();
  // The token leaves memory with the job.
  authorization_.assign(authorization_.size(), '\0');
  authorization_.clear();
  url_.clear();
}

}  // namespace highlight_export_client
