#include "DevLinkFetch.h"

#ifdef VAULTINK_DEVLINK_FULL

#include <cstring>

namespace devlink {
namespace {

bool isDigit(const char c) { return c >= '0' && c <= '9'; }
bool isBlank(const char c) { return c == ' ' || c == '\t'; }
char lower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// A decimal number with no leading zero (a lone "0" is fine), `maxDigits` at most. Moves `at`
// past it. False when there is no digit, a leading zero, or too many digits.
bool readNumber(const char*& at, const char* end, const size_t maxDigits, uint32_t& value) {
  const char* start = at;
  value = 0;
  while (at < end && isDigit(*at)) {
    if (static_cast<size_t>(at - start) >= maxDigits) return false;
    value = value * 10 + static_cast<uint32_t>(*at - '0');
    ++at;
  }
  const size_t digits = static_cast<size_t>(at - start);
  return digits > 0 && (digits == 1 || *start != '0');
}

bool isPathChar(const char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '.' || c == '_' || c == '-' ||
         c == '~' || c == '/';
}

int hexValue(const char c) {
  if (isDigit(c)) return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// True when `line` starts with `name` (lower case), whatever the case of the line.
bool startsWithNoCase(const char* line, const size_t length, const char* name) {
  const size_t nameLength = strlen(name);
  if (length < nameLength) return false;
  for (size_t i = 0; i < nameLength; ++i) {
    if (lower(line[i]) != name[i]) return false;
  }
  return true;
}

}  // namespace

bool isPrivateIpv4(const uint8_t ip[4]) {
  if (ip[0] == 10) return true;
  if (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) return true;
  return ip[0] == 192 && ip[1] == 168;
}

UrlCheck checkFetchUrl(const char* url, const size_t length, FetchUrl& out) {
  static constexpr char SCHEME[] = "http://";
  constexpr size_t schemeLength = sizeof(SCHEME) - 1;
  if (!url || length == 0 || length > FETCH_MAX_URL) return UrlCheck::TooLong;
  if (length < schemeLength || memcmp(url, SCHEME, schemeLength) != 0) return UrlCheck::NotHttp;

  const char* at = url + schemeLength;
  const char* end = url + length;
  FetchUrl parsed;
  for (size_t i = 0; i < 4; ++i) {
    uint32_t part = 0;
    if (!readNumber(at, end, 3, part) || part > 255) return UrlCheck::BadHost;
    parsed.ip[i] = static_cast<uint8_t>(part);
    if (i < 3) {
      if (at >= end || *at != '.') return UrlCheck::BadHost;
      ++at;
    }
  }
  // After the address only a port or the path may follow: a fifth number, a name, '@' or '[' is
  // not an address.
  if (at < end && *at != ':' && *at != '/') return UrlCheck::BadHost;
  if (at < end && *at == ':') {
    ++at;
    uint32_t port = 0;
    if (!readNumber(at, end, 5, port) || port == 0 || port > 65535) return UrlCheck::BadPort;
    if (at < end && *at != '/') return UrlCheck::BadPort;
    parsed.port = static_cast<uint16_t>(port);
  }
  if (!isPrivateIpv4(parsed.ip)) return UrlCheck::NotPrivate;

  const size_t pathLength = static_cast<size_t>(end - at);
  if (pathLength < 2 || end[-1] == '/') return UrlCheck::BadPath;
  for (const char* c = at; c < end; ++c) {
    if (!isPathChar(*c)) return UrlCheck::BadPath;
    if (c + 1 < end && c[0] == '.' && c[1] == '.') return UrlCheck::BadPath;
    if (c + 1 < end && c[0] == '/' && c[1] == '/') return UrlCheck::BadPath;
  }
  parsed.path = at;
  parsed.pathLength = pathLength;
  out = parsed;
  return UrlCheck::Ok;
}

const char* urlCheckText(const UrlCheck check) {
  switch (check) {
    case UrlCheck::Ok:
      return "ok";
    case UrlCheck::TooLong:
      return "the URL is empty or over 160 bytes";
    case UrlCheck::NotHttp:
      return "only http:// (lower case) is taken: no https, no other scheme";
    case UrlCheck::BadHost:
      return "the host must be an IPv4 address in numbers: no name, no user@, no IPv6";
    case UrlCheck::NotPrivate:
      return "the address must be in 10.0.0.0/8, 172.16.0.0/12 or 192.168.0.0/16";
    case UrlCheck::BadPort:
      return "the port must be 1 to 65535";
    case UrlCheck::BadPath:
      return "the URL path may hold letters, digits and . _ - ~ / only, with no '..' and no '//'";
  }
  return "refused";
}

bool parseSha256Hex(const char* text, const size_t length, uint8_t out[SHA256_BYTES]) {
  if (!text || length != SHA256_HEX) return false;
  for (size_t i = 0; i < SHA256_BYTES; ++i) {
    const int high = hexValue(text[i * 2]);
    const int low = hexValue(text[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    out[i] = static_cast<uint8_t>(high * 16 + low);
  }
  return true;
}

void sha256Hex(const uint8_t digest[SHA256_BYTES], char out[SHA256_HEX + 1]) {
  static constexpr char DIGITS[] = "0123456789abcdef";
  for (size_t i = 0; i < SHA256_BYTES; ++i) {
    out[i * 2] = DIGITS[digest[i] >> 4];
    out[i * 2 + 1] = DIGITS[digest[i] & 0x0f];
  }
  out[SHA256_HEX] = '\0';
}

bool parseFetchArgs(const char* args, const size_t length, FetchArgs& out) {
  if (!args) return false;
  const char* at = args;
  const char* end = args + length;
  while (at < end && isBlank(*at)) ++at;
  while (end > at && isBlank(end[-1])) --end;

  const char* urlEnd = at;
  while (urlEnd < end && !isBlank(*urlEnd)) ++urlEnd;
  const char* hashStart = end;
  while (hashStart > urlEnd && !isBlank(hashStart[-1])) --hashStart;
  if (urlEnd == at || hashStart <= urlEnd) return false;

  const char* pathStart = urlEnd;
  while (pathStart < hashStart && isBlank(*pathStart)) ++pathStart;
  const char* pathEnd = hashStart;
  while (pathEnd > pathStart && isBlank(pathEnd[-1])) --pathEnd;
  if (pathEnd == pathStart) return false;

  FetchArgs parsed;
  if (!parseSha256Hex(hashStart, static_cast<size_t>(end - hashStart), parsed.sha256)) return false;
  parsed.url = at;
  parsed.urlLength = static_cast<size_t>(urlEnd - at);
  parsed.path = pathStart;
  parsed.pathLength = static_cast<size_t>(pathEnd - pathStart);
  out = parsed;
  return true;
}

bool parseFlashArgs(const char* args, const size_t length, FlashArgs& out) {
  if (!args) return false;
  const char* at = args;
  const char* end = args + length;
  while (at < end && isBlank(*at)) ++at;
  while (end > at && isBlank(end[-1])) --end;
  if (at == end) return false;

  FlashArgs parsed;
  parsed.path = at;
  parsed.pathLength = static_cast<size_t>(end - at);
  const char* last = end;
  while (last > at && !isBlank(last[-1])) --last;
  if (last > at && parseSha256Hex(last, static_cast<size_t>(end - last), parsed.sha256)) {
    const char* pathEnd = last;
    while (pathEnd > at && isBlank(pathEnd[-1])) --pathEnd;
    parsed.pathLength = static_cast<size_t>(pathEnd - at);
    parsed.hasSha256 = true;
  }
  out = parsed;
  return true;
}

FetchWait fetchWaitStep(const uint32_t nowMs, const uint32_t startMs, const uint32_t lastByteMs, const bool cancel,
                        const bool available, const bool connected) {
  if (cancel) return FetchWait::Cancelled;
  if (nowMs - startMs > FETCH_DEADLINE_MS) return FetchWait::Deadline;
  if (nowMs - lastByteMs > FETCH_STALL_MS) return FetchWait::Stalled;
  if (available) return FetchWait::Ready;
  return connected ? FetchWait::Wait : FetchWait::Closed;
}

const char* fetchWaitText(const FetchWait step) {
  switch (step) {
    case FetchWait::Ready:
    case FetchWait::Wait:
      return "ok";
    case FetchWait::Cancelled:
      return "cancelled: another command arrived";
    case FetchWait::Deadline:
      return "timeout: the transfer took over 180 s";
    case FetchWait::Stalled:
      return "timeout: nothing arrived from the server for 10 s";
    case FetchWait::Closed:
      return "the connection closed before the end of the file";
  }
  return "stopped";
}

void HttpHead::endLine() {
  if (!statusRead) {
    // "HTTP/1.x NNN ..."
    statusRead = true;
    static constexpr char PREFIX[] = "HTTP/1.";
    constexpr size_t prefixLength = sizeof(PREFIX) - 1;
    const bool shaped = !lineTooLong && used >= prefixLength + 5 && memcmp(line, PREFIX, prefixLength) == 0 &&
                        isDigit(line[prefixLength]) && line[prefixLength + 1] == ' ' &&
                        isDigit(line[prefixLength + 2]) && isDigit(line[prefixLength + 3]) &&
                        isDigit(line[prefixLength + 4]) && (used == prefixLength + 5 || line[prefixLength + 5] == ' ');
    if (!shaped) {
      state = State::Bad;
      return;
    }
    statusCode =
        (line[prefixLength + 2] - '0') * 100 + (line[prefixLength + 3] - '0') * 10 + (line[prefixLength + 4] - '0');
    return;
  }
  if (used == 0 && !lineTooLong) {
    state = State::Done;
    return;
  }
  // A header line that did not fit still has its name in the buffer: enough to know it.
  if (startsWithNoCase(line, used, "transfer-encoding:")) {
    transferEncoding = true;
    return;
  }
  static constexpr char LENGTH[] = "content-length:";
  if (!startsWithNoCase(line, used, LENGTH)) return;
  if (lineTooLong) {
    state = State::Bad;
    return;
  }
  const char* at = line + sizeof(LENGTH) - 1;
  const char* end = line + used;
  while (at < end && isBlank(*at)) ++at;
  while (end > at && isBlank(end[-1])) --end;
  uint64_t value = 0;
  if (at == end || end - at > 10) {
    state = State::Bad;
    return;
  }
  for (; at < end; ++at) {
    if (!isDigit(*at)) {
      state = State::Bad;
      return;
    }
    value = value * 10 + static_cast<uint64_t>(*at - '0');
  }
  if (value > UINT32_MAX || (lengthSeen && value != length)) {
    state = State::Bad;  // two lengths that differ
    return;
  }
  length = static_cast<uint32_t>(value);
  lengthSeen = true;
}

HttpHead::State HttpHead::feed(const char c) {
  if (state != State::More) return state;
  if (++total > HEAD_BYTES_MAX) {
    state = State::Bad;
    return state;
  }
  if (c == '\r') return state;
  if (c == '\n') {
    endLine();
    used = 0;
    lineTooLong = false;
    return state;
  }
  if (used < HEAD_LINE_MAX) {
    line[used++] = c;
  } else {
    lineTooLong = true;
  }
  return state;
}

HeadCheck checkHead(const HttpHead& head, const uint32_t maxBytes) {
  if (head.status() != 200) return HeadCheck::NotOk200;
  if (head.encoded()) return HeadCheck::Encoded;
  if (!head.hasLength()) return HeadCheck::NoLength;
  if (head.contentLength() == 0) return HeadCheck::Empty;
  if (head.contentLength() > maxBytes) return HeadCheck::TooLarge;
  return HeadCheck::Ok;
}

const char* headCheckText(const HeadCheck check) {
  switch (check) {
    case HeadCheck::Ok:
      return "ok";
    case HeadCheck::NotOk200:
      return "the server did not answer 200 (a redirect is not followed)";
    case HeadCheck::Encoded:
      return "the answer has a Transfer-Encoding";
    case HeadCheck::NoLength:
      return "the answer has no Content-Length";
    case HeadCheck::Empty:
      return "the file is empty";
    case HeadCheck::TooLarge:
      return "the file is larger than the app slot";
  }
  return "refused";
}

}  // namespace devlink

#endif  // VAULTINK_DEVLINK_FULL
