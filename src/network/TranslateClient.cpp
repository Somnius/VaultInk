#include "TranslateClient.h"

#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <TlsPolicyDevice.h>

#include "TranslateStore.h"
#include "WifiPowerSaveGuard.h"

namespace translate_client {

namespace {
constexpr char USER_AGENT_NAME[] = "VaultInk/" CROSSPOINT_VERSION;
constexpr char USER_AGENT_URL[] = " (https://github.com/Somnius/VaultInk)";
constexpr uint32_t TIMEOUT_MS = 15000;
// The floor for a TLS session, as HttpDownloader::MIN_TLS_FREE_HEAP / MIN_TLS_MAX_ALLOC. (Not
// included from there: that header brings in SdFat, which has to come before the network headers.)
constexpr size_t MIN_TLS_FREE_HEAP = 40000;
constexpr size_t MIN_TLS_MAX_ALLOC = 20000;

Result resultOf(const translate::Status status) {
  switch (status) {
    case translate::Status::Ok:
      return Result::Ok;
    case translate::Status::NoTranslation:
      return Result::NotFound;
    case translate::Status::Quota:
      return Result::Quota;
    case translate::Status::BadLanguage:
      return Result::BadLanguage;
    case translate::Status::Refused:
      return Result::Refused;
    case translate::Status::ServerError:
      return Result::ServerError;
    case translate::Status::Invalid:
    default:
      return Result::BadAnswer;
  }
}
}  // namespace

bool usesOwnServer() { return TRANSLATE_STORE.hasServer(); }

Result fetch(const translate::Pair& pair, const std::string& text, translate::Result& out) {
  out = translate::Result{};
  if (translate::cleanQuery(text).empty()) return Result::NoQuery;
  if (!pair.ok()) return Result::NoPair;

  const bool ownServer = usesOwnServer();
  std::string url;
  std::string body;
  if (ownServer) {
    url = translate::libreTranslateUrl(TRANSLATE_STORE.serverUrl());
    if (url.empty()) return Result::BadServer;
    body = translate::libreTranslateBody(text, pair, TRANSLATE_STORE.apiKey());
  } else {
    // MyMemory has no documented way to detect the language: without the book's, it is not asked.
    if (pair.source.empty()) return Result::NoPair;
    url = translate::myMemoryUrl(text, pair);
  }
  if (url.empty() || (ownServer && body.empty())) return Result::NoQuery;

  const auto heap = HalMemory::getDefaultHeap();
  if (heap.freeBytes < MIN_TLS_FREE_HEAP || heap.largestBlockBytes < MIN_TLS_MAX_ALLOC) {
    LOG_ERR("TRN", "Low heap for TLS: %u free, %u largest", static_cast<unsigned>(heap.freeBytes),
            static_cast<unsigned>(heap.largestBlockBytes));
    return Result::LowMemory;
  }
  auto http = makeUniqueNoThrow<freeink::SecureHttpClient>();
  auto parser = makeUniqueNoThrow<translate::AnswerParser>();
  if (!http || !parser) return Result::LowMemory;

  WifiPowerSaveGuard psGuard;
  http->setTimeout(TIMEOUT_MS);
  http->setUserAgent(std::string(USER_AGENT_NAME) + USER_AGENT_URL);
#ifndef SIMULATOR               // the simulator's HTTP stub has no redirect or reuse settings
  http->setFollowRedirects(0);  // a redirect must not carry the selection or the key to another address
  http->setReuse(false);        // one request: a replayed POST is not wanted
#endif
  // MyMemory is always checked whatever is passed here; the user's server only with its switch on.
  tlspolicy::Error tlsError = tlspolicy::Error::None;
  if (!tlspolicy::apply(*http, url, ownServer && TRANSLATE_STORE.verifyCert(), &tlsError)) {
    return tlsError == tlspolicy::Error::NoClock        ? Result::NoClock
           : tlsError == tlspolicy::Error::CaUnreadable ? Result::NoCa
                                                        : Result::Network;
  }
  if (!http->begin(url)) return Result::Network;
  http->addHeader("Accept", "application/json");
  if (ownServer) http->addHeader("Content-Type", "application/json");

  // The address holds the selection (MyMemory) and the body may hold the key: only the host is logged.
  LOG_DBG("TRN", "%s %s", ownServer ? "POST" : "GET", tlspolicy::hostOf(url).c_str());
  // Both services say why in the body of an error answer, so those are read too, to the same limit.
  const auto onData = [&parser](const uint8_t* data, const size_t len) {
    return parser->feed(reinterpret_cast<const char*>(data), len);
  };
  // cppcheck-suppress unreadVariable // the log line below prints it
  const int status = ownServer
                         ? http->sendRequest("POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(), onData)
                         : http->GET(onData);
  // Stopping at the size limit can make the client report a failed transfer, but the status line
  // was read by then.
  const int answered = http->getStatus();
  http->end();
  // The key leaves memory with the request.
  body.assign(body.size(), '\0');
  LOG_DBG("TRN", "status %d (%d)", status, answered);
  if (answered <= 0) return Result::Network;

  out = ownServer ? parser->finishLibreTranslate(answered) : parser->finishMyMemory(answered);
  if (out.status != translate::Status::Ok && !out.detail.empty()) {
    LOG_ERR("TRN", "The service refused or failed (status %d)", status);  // never the text: it holds the selection
  }
  return resultOf(out.status);
}

}  // namespace translate_client
