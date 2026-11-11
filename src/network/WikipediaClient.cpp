#include "WikipediaClient.h"

#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <TlsPolicyDevice.h>

#include "WifiPowerSaveGuard.h"

namespace wikipedia_client {

namespace {
constexpr char USER_AGENT_NAME[] = "VaultInk/" CROSSPOINT_VERSION;
constexpr char USER_AGENT_URL[] = " (https://github.com/Somnius/VaultInk)";
constexpr uint32_t TIMEOUT_MS = 15000;
// A redirect (another spelling of the title) keeps this client and so the same certificate rule.
constexpr int MAX_REDIRECTS = 3;
// The floor for a TLS session, as HttpDownloader::MIN_TLS_FREE_HEAP / MIN_TLS_MAX_ALLOC. (Not
// included from there: that header brings in SdFat, which has to come before the network headers.)
constexpr size_t MIN_TLS_FREE_HEAP = 40000;
constexpr size_t MIN_TLS_MAX_ALLOC = 20000;
}  // namespace

Result fetch(const std::string& language, const std::string& text, wikipedia::Summary& out) {
  out = wikipedia::Summary{};
  const std::string url = wikipedia::summaryUrl(language, text);
  if (url.empty()) return Result::NoTitle;

  const auto heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < MIN_TLS_FREE_HEAP || heap.largestBlockBytes < MIN_TLS_MAX_ALLOC) {
    LOG_ERR("WIKI", "Low heap for TLS: %u free, %u largest", static_cast<unsigned>(heap.freeBytes),
            static_cast<unsigned>(heap.largestBlockBytes));
    return Result::LowMemory;
  }
  auto http = makeUniqueNoThrow<freeink::SecureHttpClient>();
  auto parser = makeUniqueNoThrow<wikipedia::SummaryParser>();
  if (!http || !parser) return Result::LowMemory;

  WifiPowerSaveGuard psGuard;
  http->setTimeout(TIMEOUT_MS);
  http->setUserAgent(std::string(USER_AGENT_NAME) + USER_AGENT_URL);
#ifndef SIMULATOR  // the simulator's HTTP stub has no redirect setting
  http->setFollowRedirects(MAX_REDIRECTS);
#endif
  tlspolicy::Error tlsError = tlspolicy::Error::None;
  if (!tlspolicy::apply(*http, url, /*userVerify=*/false, &tlsError)) {
    return tlsError == tlspolicy::Error::NoClock ? Result::NoClock : Result::Network;
  }
  if (!http->begin(url)) return Result::Network;
  http->addHeader("Accept", "application/json");

  LOG_DBG("WIKI", "GET %s", url.c_str());
  size_t errorBytes = 0;
  bool capped = false;
  // cppcheck-suppress unreadVariable // the log line below prints it
  const int status = http->GET([&](const uint8_t* data, const size_t len) {
    const int st = http->getStatus();
    if (st < 200 || st >= 300) {
      // An error page: let a short one drain, never read a long one.
      errorBytes += len;
      return errorBytes <= wikipedia::MAX_BODY_BYTES;
    }
    if (!parser->feed(reinterpret_cast<const char*>(data), len)) {
      capped = true;
      return false;
    }
    return true;
  });
  // Stopping at the size limit can make the client report a failed transfer, but the status line
  // was read by then.
  const int answered = http->getStatus();
  http->end();
  LOG_DBG("WIKI", "status %d (%d), capped %d", status, answered, capped ? 1 : 0);
  if (answered <= 0) return Result::Network;

  out = parser->finish(answered);
  switch (out.kind) {
    case wikipedia::Kind::Article:
      return Result::Ok;
    case wikipedia::Kind::NotFound:
      return Result::NotFound;
    case wikipedia::Kind::Disambiguation:
      return Result::Disambiguation;
    case wikipedia::Kind::Invalid:
    default:
      return Result::BadAnswer;
  }
}

}  // namespace wikipedia_client
