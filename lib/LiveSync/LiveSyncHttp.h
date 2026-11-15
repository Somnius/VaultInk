#pragma once

// One LiveSync request over an HTTP client that keeps its connection between requests (the SDK's
// SecureHttpClient on the device, a fake socket under it in the computer tests): the answer is read
// into `response` up to a size cap, and a connection whose answer was not read to its end is never
// used again.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "LiveSyncEngine.h"

namespace livesync {

struct CappedRequest {
  size_t cap = 0;  // most body bytes kept; an answer past it is dropped (STATUS_TOO_BIG)
  // The largest block that can be taken right now. Asked before the buffer grows past 64 KB (the
  // block and a quarter more must fit), since a failed allocation inside std::string resets the
  // device. Empty: not checked.
  std::function<size_t()> largestFreeBlock;
  std::function<void()> tick;  // called for every piece read and every wait (the watchdog)
};

struct CappedResult {
  bool tooBig = false;
  bool noMemory = false;
  // An answer had started and was not read to its end (the cap, no memory, a read that timed out,
  // a closed socket): its connection was closed, so the next request opens a new one. The server may
  // still be sending the rest of the old body, and a request written behind it is not looked at
  // until that is done.
  bool closedUnfinished = false;
};

// Why a request got no usable answer, for the message on the result screen.
enum class FailureCause : uint8_t {
  None,
  Certificate,  // the server was reached, the TLS handshake failed while the certificate is checked
  Unreachable,  // no connection could be made
  NoAnswer,     // connected, then no answer (or not all of one) within the timeout
};

// `status` as requestCapped returned it. `connectFailed`: no TCP connection; `handshakeFailed`: the
// TLS setup failed after it. `checked`: the certificate is checked. `workedBefore`: a request of
// this session was answered already, so the certificate and the name were accepted then and a
// handshake that fails now is not about them.
inline FailureCause failureCause(const int status, const bool complete, const bool connectFailed,
                                 const bool handshakeFailed, const bool checked, const bool workedBefore) {
  if (status == STATUS_TOO_BIG || status == STATUS_NO_MEMORY) return FailureCause::None;
  if (status >= 0) return complete ? FailureCause::None : FailureCause::NoAnswer;
  if (handshakeFailed) return checked && !workedBefore ? FailureCause::Certificate : FailureCause::Unreachable;
  if (connectFailed) return FailureCause::Unreachable;
  return FailureCause::NoAnswer;
}

// Sends the request `http` was prepared for (begin() and the headers are the caller's). Returns the
// HTTP status, a transport error below zero, or STATUS_TOO_BIG / STATUS_NO_MEMORY.
template <class Http>
int requestCapped(Http& http, const char* method, const std::string& body, std::string& response,
                  const CappedRequest& limits, ResponseInfo& info, CappedResult& result) {
  result = CappedResult{};
  info = ResponseInfo{};
  info.complete = false;
  response.clear();
  const auto onData = [&](const uint8_t* data, size_t len) {
    if (limits.tick) limits.tick();
    info.received += len;
    const size_t need = response.size() + len;
    const size_t announced = http.hasContentLength() ? http.getContentLength() : 0;
    if (need > limits.cap || announced > limits.cap) {
      result.tooBig = true;
      return false;
    }
    if (need > response.capacity()) {
      // With a Content-Length the buffer is taken once, else it doubles.
      const size_t want = std::min(limits.cap, std::max({need, announced, response.capacity() * 2}));
      if (want > 64 * 1024 && limits.largestFreeBlock && limits.largestFreeBlock() < want + want / 4) {
        result.noMemory = true;
        return false;
      }
      response.reserve(want);
    }
    response.append(reinterpret_cast<const char*>(data), len);
    return true;
  };
  const auto keepAlive = [&limits] {
    if (limits.tick) limits.tick();
    return false;
  };
  const int status =
      http.sendRequest(method, reinterpret_cast<const uint8_t*>(body.data()), body.size(), onData, keepAlive);
  info.contentType = http.getHeader("content-type");
  info.contentLength = http.hasContentLength() ? static_cast<int64_t>(http.getContentLength()) : -1;
  info.chunked = http.getHeader("transfer-encoding").find("chunked") != std::string::npos;
  // A body that stops early (the socket closed, a read waited out the timeout) still comes back
  // with its status: the engine must not parse it as if it were whole.
  info.complete = status >= 0 && http.responseComplete();
  if (!info.complete) {
    // The client closes such a connection itself; said again here so that no request of a sync can
    // follow an unread answer whatever the client does.
    http.end();
    result.closedUnfinished = status >= 0;
  }
  if (result.tooBig) return STATUS_TOO_BIG;
  if (result.noMemory) return STATUS_NO_MEMORY;
  return status;
}

}  // namespace livesync
