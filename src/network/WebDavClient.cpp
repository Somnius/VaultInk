// The simulator has host network shims in place of SecureNet and WiFiClient (no bare TLS client, no
// raw sockets with this API), so there this file only says that the server cannot be reached.
#ifndef SIMULATOR

// clang-format off
// HalStorage.h brings in SdFat, whose macros have to be seen before the network headers.
#include <HalStorage.h>
#include "RemoteFsClient.h"
// clang-format on

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureClient.h>
#include <SecureHttpClient.h>
#include <TlsPolicyDevice.h>
#include <WebDav.h>
#include <WiFiClient.h>

#include <utility>
#include <vector>

#include "RemoteServerStore.h"
#include "WifiPowerSaveGuard.h"

namespace remotefs {

namespace {

constexpr uint32_t TIMEOUT_MS = 30000;
// After a failed upload the server may still have sent its reason; wait this long for it.
constexpr uint32_t REFUSAL_WAIT_MS = 3000;
// No more than this much of an error page is read.
constexpr size_t MAX_ERROR_BODY_BYTES = 8 * 1024;
constexpr size_t MAX_STATUS_LINE_BYTES = 256;
// The floor for a TLS session, as HttpDownloader::MIN_TLS_FREE_HEAP / MIN_TLS_MAX_ALLOC.
constexpr size_t MIN_TLS_FREE_HEAP = 40000;
constexpr size_t MIN_TLS_MAX_ALLOC = 20000;
constexpr char USER_AGENT[] = "VaultInk/" CROSSPOINT_VERSION;

bool isRedirect(const int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool lowHeapForTls() {
  if (ESP.getFreeHeap() >= MIN_TLS_FREE_HEAP && ESP.getMaxAllocHeap() >= MIN_TLS_MAX_ALLOC) return false;
  LOG_ERR("DAV", "Low heap for TLS: %u free, %u largest", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  return true;
}

Error tlsError(const tlspolicy::Error error) {
  return error == tlspolicy::Error::NoClock ? Error::NoClock : Error::Certificate;
}

// What a status means when it is not the success the request hoped for.
Error fromStatus(const int status) {
  if (status <= 0) return Error::Network;
  if (status >= 200 && status < 300) return Error::Ok;
  if (status >= 300 && status < 400) return Error::Redirected;
  if (status == 401 || status == 403) return Error::Auth;
  if (status == 404 || status == 409 || status == 410) return Error::NotFound;  // 409: the parent folder is missing
  return Error::Refused;
}

bool writeAll(::Client& conn, const uint8_t* data, size_t len) {
  while (len > 0) {
    const size_t n = conn.write(data, len);
    if (n == 0) return false;
    data += n;
    len -= n;
  }
  return true;
}

// One line of the answer, without CR LF. False on a timeout, a closed connection or a long line.
bool readLine(::Client& conn, std::string& line, const uint32_t waitMs, const Transfer& transfer) {
  line.clear();
  const unsigned long start = millis();
  while (millis() - start < waitMs) {
    if (transfer.cancelled()) return false;
    if (conn.available() > 0) {
      const int c = conn.read();
      if (c < 0) continue;
      if (c == '\n') {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
      }
      if (line.size() >= MAX_STATUS_LINE_BYTES) return false;
      line += static_cast<char>(c);
    } else if (!conn.connected()) {
      return false;
    } else {
      delay(2);
    }
  }
  return false;
}

// The status of the answer to a request we wrote ourselves; 0 when there is none. A "100 Continue"
// is passed over.
int readStatus(::Client& conn, const uint32_t waitMs, const Transfer& transfer) {
  std::string line;
  for (int i = 0; i < 3; ++i) {
    if (!readLine(conn, line, waitMs, transfer)) return 0;
    const int status = webdav::statusOf(line);
    if (status != 100) return status;
    while (readLine(conn, line, waitMs, transfer) && !line.empty()) {
    }
  }
  return 0;
}

class WebDavClient final : public Client {
 public:
  WebDavClient(webdav::Url base, std::string user, std::string password, const bool verifyCert)
      : base_(std::move(base)), user_(std::move(user)), password_(std::move(password)), verifyCert_(verifyCert) {}

  Error list(const std::string& dir, remotefiles::Listing& out, const Transfer& transfer) override;
  Error download(const std::string& dir, const std::string& name, const std::string& localPath, uint64_t expectedSize,
                 const Transfer& transfer) override;
  Error upload(const std::string& localPath, const std::string& dir, const std::string& name,
               const Transfer& transfer) override;
  Error makeDir(const std::string& dir, const std::string& name) override;

 private:
  struct Answer {
    Error early = Error::Ok;  // the request was not sent, or the redirect was not followed
    int status = 0;
    size_t contentLength = 0;
    bool hasLength = false;
    bool complete = false;  // the whole body arrived
    bool aborted = false;
  };
  // (status, data, len) -> false to stop reading.
  using Sink = std::function<bool(int, const uint8_t*, size_t)>;

  Answer request(const char* method, const std::string& url, const std::vector<webdav::Header>& headers,
                 const std::string* body, const Sink& sink, const Transfer& transfer);

  webdav::Url base_;
  std::string user_;
  std::string password_;
  bool verifyCert_;
};

// One request, following redirects by hand so that each hop is checked: the same host only, never
// from https to http (webdav::redirectAllowed), and the certificate rule applied again for the
// address of that hop. The method and the body are kept across a redirect.
WebDavClient::Answer WebDavClient::request(const char* method, const std::string& url,
                                           const std::vector<webdav::Header>& headers, const std::string* body,
                                           const Sink& sink, const Transfer& transfer) {
  Answer answer;
  WifiPowerSaveGuard psGuard;
  std::string current = url;
  webdav::Url first;
  webdav::Url previous;
  for (int hop = 0; hop <= webdav::MAX_REDIRECTS; ++hop) {
    webdav::Url target;
    if (!webdav::parseUrl(current, target)) {
      answer.early = hop == 0 ? Error::BadAddress : Error::Redirected;
      return answer;
    }
    if (hop == 0) {
      first = target;
    } else if (!webdav::redirectAllowed(previous, target)) {
      LOG_ERR("DAV", "Redirect refused: %s", current.c_str());
      answer.early = Error::Redirected;
      return answer;
    }
    if (target.https && lowHeapForTls()) {
      answer.early = Error::LowMemory;
      return answer;
    }
    auto http = makeUniqueNoThrow<freeink::SecureHttpClient>();
    if (!http) {
      answer.early = Error::LowMemory;
      return answer;
    }
    http->setTimeout(TIMEOUT_MS);
    http->setUserAgent(USER_AGENT);
    http->setFollowRedirects(0);
    http->setReuse(false);
    tlspolicy::Error tls = tlspolicy::Error::None;
    if (!tlspolicy::apply(*http, current, verifyCert_, &tls)) {
      answer.early = tlsError(tls);
      return answer;
    }
    if (!http->begin(current)) {
      answer.early = Error::BadAddress;
      return answer;
    }
    if (!user_.empty()) http->setBasicAuth(user_, password_);
    for (const auto& h : headers) http->addHeader(h.first, h.second);

    LOG_DBG("DAV", "%s %s", method, current.c_str());
    http->sendRequest(
        method, body ? reinterpret_cast<const uint8_t*>(body->data()) : nullptr, body ? body->size() : 0,
        [&](const uint8_t* data, const size_t len) {
          const int status = http->getStatus();
          if (isRedirect(status)) return true;  // a short page: let it drain
          return sink ? sink(status, data, len) : true;
        },
        [&transfer] { return transfer.cancelled(); });
    answer.status = http->getStatus();
    answer.complete = http->responseComplete();
    answer.aborted = http->aborted() || transfer.cancelled();
    answer.hasLength = http->hasContentLength();
    answer.contentLength = http->getContentLength();
    if (answer.aborted || !isRedirect(answer.status)) {
      // The server moved us from http to https on the same host and path: stay there, so the
      // upload (which follows no redirects) goes to the encrypted address too.
      if (hop > 0 && answer.status >= 200 && answer.status < 300 && target.https && !base_.https &&
          target.host == base_.host && target.path == first.path) {
        base_.https = true;
        base_.port = target.port;
      }
      return answer;
    }
    const std::string location = http->getHeader("location");
    std::string next;
    if (location.empty() || !freeink::SecureHttpClient::resolveUrl(current, location, next)) {
      answer.early = Error::BadAnswer;
      return answer;
    }
    previous = target;
    current = std::move(next);
  }
  answer.early = Error::Redirected;  // too many hops
  return answer;
}

Error WebDavClient::list(const std::string& dir, remotefiles::Listing& out, const Transfer& transfer) {
  auto parser = makeUniqueNoThrow<webdav::MultistatusParser>(webdav::decodedRequestPath(base_, dir));
  if (!parser || parser->error() == webdav::MultistatusParser::Error::NoMemory) return Error::LowMemory;

  const std::string body = webdav::PROPFIND_BODY;
  size_t errorBytes = 0;
  bool parserStopped = false;
  const Answer answer = request(
      "PROPFIND", webdav::resourceUrl(base_, dir, true),
      {{"Depth", "1"}, {"Content-Type", "application/xml; charset=utf-8"}}, &body,
      [&](const int status, const uint8_t* data, const size_t len) {
        if (status != 207) {
          errorBytes += len;
          return errorBytes <= MAX_ERROR_BODY_BYTES;
        }
        if (!parser->feed(reinterpret_cast<const char*>(data), len)) {
          parserStopped = true;
          return false;
        }
        return true;
      },
      transfer);
  if (answer.early != Error::Ok) return answer.early;
  if (answer.aborted) return Error::Aborted;
  if (answer.status != 207) {
    LOG_ERR("DAV", "PROPFIND status %d", answer.status);
    // A 200 is a web page (a sign-in page, a plain web server), a 405 a server without WebDAV.
    if ((answer.status >= 200 && answer.status < 300) || answer.status == 405 || answer.status == 501) {
      return Error::BadAnswer;
    }
    return fromStatus(answer.status);
  }
  if (!parserStopped && !answer.complete) return Error::Network;  // the connection dropped midway
  if (!parser->finish()) {
    return parser->error() == webdav::MultistatusParser::Error::NoMemory ? Error::LowMemory : Error::BadAnswer;
  }
  out = std::move(parser->listing());
  parser.reset();
  remotefiles::sortEntries(out.entries);
  return Error::Ok;
}

Error WebDavClient::download(const std::string& dir, const std::string& name, const std::string& localPath,
                             const uint64_t expectedSize, const Transfer& transfer) {
  // Staged in <dest>.part: a failed or cancelled download never replaces a file that is there.
  const std::string partPath = localPath + ".part";
  Storage.remove(partPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("DAV", partPath, file)) return Error::File;

  size_t received = 0;
  size_t errorBytes = 0;
  bool writeFailed = false;
  const Answer answer = request(
      "GET", webdav::resourceUrl(base_, remotefiles::joinPath(dir, name), false), {}, nullptr,
      [&](const int status, const uint8_t* data, const size_t len) {
        if (status != 200) {
          errorBytes += len;
          return errorBytes <= MAX_ERROR_BODY_BYTES;
        }
        if (file.write(data, len) != len) {
          writeFailed = true;
          return false;
        }
        received += len;
        if (transfer.progress) transfer.progress(received, static_cast<size_t>(expectedSize));
        return true;
      },
      transfer);
  // Close before any remove() on the same path.
  if (file.isOpen()) file.close();

  Error result = Error::Ok;
  if (answer.early != Error::Ok) {
    result = answer.early;
  } else if (answer.aborted) {
    result = Error::Aborted;
  } else if (writeFailed) {
    result = Error::File;
  } else if (answer.status != 200) {
    LOG_ERR("DAV", "GET status %d", answer.status);
    result = answer.status > 200 && answer.status < 300 ? Error::BadAnswer : fromStatus(answer.status);
  } else if (!answer.complete || (answer.hasLength && received != answer.contentLength)) {
    LOG_ERR("DAV", "GET cut short at %u bytes", static_cast<unsigned>(received));
    result = Error::Network;
  }
  if (result != Error::Ok) {
    Storage.remove(partPath.c_str());
    return result;
  }
  if (!Storage.replaceFile(partPath.c_str(), localPath.c_str())) {
    Storage.remove(partPath.c_str());
    return Error::File;
  }
  return Error::Ok;
}

// PUT with a Content-Length, the file streamed from the card through one 4 KB buffer. The request
// is written here because SecureHttpClient (read-only SDK) only sends a body held in memory. No
// chunked encoding (many servers refuse a chunked PUT) and no redirect: a server that answers 3xx
// to the upload gets Error::Redirected.
Error WebDavClient::upload(const std::string& localPath, const std::string& dir, const std::string& name,
                           const Transfer& transfer) {
  const std::string url = webdav::resourceUrl(base_, remotefiles::joinPath(dir, name), false);
  webdav::Url target;
  if (!webdav::parseUrl(url, target)) return Error::BadAddress;

  HalFile file;
  if (!Storage.openFileForRead("DAV", localPath, file)) return Error::File;
  struct Closer {
    HalFile& f;
    ~Closer() {
      if (f.isOpen()) f.close();
    }
  } closer{file};
  if (file.isDirectory()) return Error::File;
  const uint64_t size = file.fileSize64();

  std::vector<webdav::Header> headers;
  const std::string authorization = webdav::basicAuthorization(user_, password_);
  if (!authorization.empty()) headers.emplace_back("Authorization", authorization);
  headers.emplace_back("Content-Type", "application/octet-stream");
  const std::string head = webdav::requestHead("PUT", target, headers, static_cast<int64_t>(size), USER_AGENT);
  if (head.empty()) return Error::BadAddress;

  auto buffer = makeUniqueNoThrow<uint8_t[]>(webdav::IO_BUFFER_BYTES);
  if (!buffer) return Error::LowMemory;

  WifiPowerSaveGuard psGuard;
  WiFiClient plain;
  std::unique_ptr<freeink::SecureClient> tls;
  ::Client* conn = nullptr;  // Arduino's, not remotefs::Client
  if (target.https) {
    if (lowHeapForTls()) return Error::LowMemory;
    tls = makeUniqueNoThrow<freeink::SecureClient>();
    if (!tls) return Error::LowMemory;
    tlspolicy::Error tlsErr = tlspolicy::Error::None;
    if (!tlspolicy::apply(*tls, url, verifyCert_, &tlsErr)) return tlsError(tlsErr);
    tls->setTimeout(TIMEOUT_MS);
    tls->setAbortCallback([&transfer] { return transfer.cancelled(); });
    if (!tls->connect(target.host.c_str(), target.port)) return tls->aborted() ? Error::Aborted : Error::Network;
    conn = tls.get();
  } else {
    plain.setTimeout(TIMEOUT_MS);
    plain.setConnectionTimeout(TIMEOUT_MS);
    if (!plain.connect(target.host.c_str(), target.port)) return Error::Network;
    conn = &plain;
  }
  LOG_DBG("DAV", "PUT %s (%u bytes)", url.c_str(), static_cast<unsigned>(size));

  Error result = Error::Ok;
  bool sendFailed = !writeAll(*conn, reinterpret_cast<const uint8_t*>(head.data()), head.size());
  uint64_t sent = 0;
  while (!sendFailed && result == Error::Ok && sent < size) {
    if (transfer.cancelled()) {
      result = Error::Aborted;
      break;
    }
    const uint64_t left = size - sent;
    const size_t want = left < webdav::IO_BUFFER_BYTES ? static_cast<size_t>(left) : webdav::IO_BUFFER_BYTES;
    const int n = file.read(buffer.get(), want);
    if (n <= 0) {
      result = Error::File;  // the file got shorter, or the card failed
      break;
    }
    if (!writeAll(*conn, buffer.get(), static_cast<size_t>(n))) {
      sendFailed = true;
      break;
    }
    sent += static_cast<uint64_t>(n);
    if (transfer.progress) transfer.progress(static_cast<size_t>(sent), static_cast<size_t>(size));
  }

  if (result == Error::Ok) {
    // A server that refuses the upload (wrong password, no room) may close before the whole body
    // is in; its answer says why.
    const int status = readStatus(*conn, sendFailed ? REFUSAL_WAIT_MS : TIMEOUT_MS, transfer);
    LOG_DBG("DAV", "PUT status %d", status);
    if (transfer.cancelled()) {
      result = Error::Aborted;
    } else if (status == 0 || (sendFailed && status >= 200 && status < 300)) {
      result = Error::Network;
    } else {
      result = fromStatus(status);
    }
  }
  // Stopping short of Content-Length leaves the request unfinished, so the server has no whole
  // file to keep.
  conn->stop();
  return result;
}

Error WebDavClient::makeDir(const std::string& dir, const std::string& name) {
  size_t errorBytes = 0;
  const Answer answer = request(
      "MKCOL", webdav::resourceUrl(base_, remotefiles::joinPath(dir, name), true), {}, nullptr,
      [&](int, const uint8_t*, const size_t len) {
        errorBytes += len;
        return errorBytes <= MAX_ERROR_BODY_BYTES;
      },
      Transfer{});
  if (answer.early != Error::Ok) return answer.early;
  LOG_DBG("DAV", "MKCOL status %d", answer.status);
  return fromStatus(answer.status);  // 405: there is something by that name already
}

}  // namespace

std::unique_ptr<Client> makeWebDavClient(const RemoteServer& server, Error& error) {
  webdav::Url base;
  if (!webdav::parseUrl(server.url, base)) {
    error = Error::BadAddress;
    return nullptr;
  }
  auto client = makeUniqueNoThrow<WebDavClient>(std::move(base), server.username, server.password, server.verifyCert);
  error = client ? Error::Ok : Error::LowMemory;
  return client;
}

}  // namespace remotefs

#else  // SIMULATOR

#include "RemoteFsClient.h"

namespace remotefs {
std::unique_ptr<Client> makeWebDavClient(const RemoteServer&, Error& error) {
  error = Error::Network;
  return nullptr;
}
}  // namespace remotefs

#endif  // SIMULATOR
