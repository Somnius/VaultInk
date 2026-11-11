#include "WebDav.h"

#include <XmlParserUtils.h>
#include <expat.h>

#include <cstdlib>
#include <cstring>

namespace webdav {

const char PROPFIND_BODY[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<D:propfind xmlns:D=\"DAV:\"><D:prop>"
    "<D:resourcetype/><D:getcontentlength/><D:getlastmodified/>"
    "</D:prop></D:propfind>";

namespace {

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool isUnreserved(const char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
         c == '_' || c == '~';
}

char lowerAscii(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool hasLineBreak(const std::string_view s) { return s.find_first_of("\r\n") != std::string_view::npos; }

// "/a/b" from a decoded path; "" for the root. False when it climbs above the root.
bool cleanPath(const std::string_view decoded, std::string& out) {
  if (!remotefiles::normalizePath(decoded, out)) return false;
  if (out == "/") out.clear();
  return true;
}

bool parseDigits(const std::string_view s, uint64_t& out) {
  if (s.empty() || s.size() > 19) return false;
  uint64_t v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
  }
  out = v;
  return true;
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.remove_suffix(1);
  }
  return s;
}

// Reads an unsigned number of 1..maxDigits digits at `pos`, moving it on.
bool takeNumber(const std::string_view s, size_t& pos, const size_t maxDigits, unsigned& out) {
  size_t n = 0;
  unsigned v = 0;
  while (pos < s.size() && n < maxDigits && s[pos] >= '0' && s[pos] <= '9') {
    v = v * 10 + static_cast<unsigned>(s[pos] - '0');
    ++pos;
    ++n;
  }
  if (n == 0) return false;
  out = v;
  return true;
}

bool takeChar(const std::string_view s, size_t& pos, const char c) {
  if (pos >= s.size() || s[pos] != c) return false;
  ++pos;
  return true;
}

}  // namespace

std::string Url::hostHeader() const {
  const uint16_t defaultPort = https ? 443 : 80;
  if (port == 0 || port == defaultPort) return host;
  return host + ":" + std::to_string(port);
}

std::string Url::origin() const { return std::string(https ? "https://" : "http://") + hostHeader(); }

bool percentDecode(const std::string_view in, std::string& out) {
  out.clear();
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '%') {
      out += in[i];
      continue;
    }
    if (i + 2 >= in.size()) return false;
    const int hi = hexValue(in[i + 1]);
    const int lo = hexValue(in[i + 2]);
    if (hi < 0 || lo < 0) return false;
    const char c = static_cast<char>((hi << 4) | lo);
    if (c == '\0') return false;
    out += c;
    i += 2;
  }
  return true;
}

std::string encodePath(const std::string_view decodedPath) {
  static const char HEX[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(decodedPath.size() + 16);
  for (const char c : decodedPath) {
    if (c == '/' || isUnreserved(c)) {
      out += c;
    } else {
      const auto u = static_cast<unsigned char>(c);
      out += '%';
      out += HEX[u >> 4];
      out += HEX[u & 0x0F];
    }
  }
  return out;
}

bool parseUrl(const std::string_view url, Url& out) {
  const std::string_view trimmed = trim(url);
  const size_t schemeEnd = trimmed.find("://");
  if (schemeEnd == std::string_view::npos) return false;
  std::string scheme(trimmed.substr(0, schemeEnd));
  for (char& c : scheme) c = lowerAscii(c);
  if (scheme != "http" && scheme != "https") return false;

  const size_t hostStart = schemeEnd + 3;
  size_t hostEnd = trimmed.find_first_of("/?#", hostStart);
  if (hostEnd == std::string_view::npos) hostEnd = trimmed.size();
  const std::string_view authority = trimmed.substr(hostStart, hostEnd - hostStart);
  if (authority.empty() || authority.find_first_of("@[]") != std::string_view::npos) return false;

  Url parsed;
  parsed.https = scheme == "https";
  std::string_view host = authority;
  const size_t colon = authority.find(':');
  if (colon != std::string_view::npos) {
    host = authority.substr(0, colon);
    uint64_t port = 0;
    if (!parseDigits(authority.substr(colon + 1), port) || port == 0 || port > 65535) return false;
    parsed.port = static_cast<uint16_t>(port);
  } else {
    parsed.port = parsed.https ? 443 : 80;
  }
  if (host.empty()) return false;
  for (const char c : host) {
    const char l = lowerAscii(c);
    if (!((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '.' || l == '-' || l == '_')) return false;
    parsed.host += l;
  }

  std::string_view path = trimmed.substr(hostEnd);
  const size_t pathEnd = path.find_first_of("?#");
  if (pathEnd != std::string_view::npos) path = path.substr(0, pathEnd);
  std::string decoded;
  std::string clean;
  if (!percentDecode(path, decoded) || !cleanPath(decoded, clean)) return false;
  parsed.path = encodePath(clean);
  out = std::move(parsed);
  return true;
}

bool hrefPath(const std::string_view href, std::string& decodedPath, bool& endsWithSlash) {
  std::string_view path = trim(href);
  const size_t schemeEnd = path.find("://");
  if (schemeEnd != std::string_view::npos && path.find('/') > schemeEnd) {
    const size_t slash = path.find('/', schemeEnd + 3);
    if (slash == std::string_view::npos) return false;
    path = path.substr(slash);
  }
  const size_t end = path.find_first_of("?#");
  if (end != std::string_view::npos) path = path.substr(0, end);
  if (path.empty() || path.front() != '/') return false;
  std::string decoded;
  if (!percentDecode(path, decoded)) return false;
  endsWithSlash = decoded.size() > 1 && decoded.back() == '/';
  return cleanPath(decoded, decodedPath);
}

std::string decodedRequestPath(const Url& base, const std::string_view remotePath) {
  std::string basePath;
  if (!percentDecode(base.path, basePath)) return {};
  basePath += '/';
  basePath.append(remotePath);
  std::string clean;
  if (!cleanPath(basePath, clean)) return {};
  return clean;
}

std::string resourceUrl(const Url& base, const std::string_view remotePath, const bool collection) {
  std::string clean;
  if (!cleanPath(remotePath, clean)) clean.clear();
  std::string url = base.origin() + base.path + encodePath(clean);
  if (collection || (base.path.empty() && clean.empty())) url += '/';
  return url;
}

bool redirectAllowed(const Url& from, const Url& to) {
  if (from.host != to.host) return false;
  if (from.https && !to.https) return false;
  // The same scheme has to stay on the same port; http to https may change it (80 to 443).
  if (from.https == to.https && from.port != to.port) return false;
  return true;
}

std::string base64(const std::string_view in) {
  static const char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    const uint32_t v =
        (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8) | static_cast<uint8_t>(in[i + 2]);
    out += TABLE[(v >> 18) & 63];
    out += TABLE[(v >> 12) & 63];
    out += TABLE[(v >> 6) & 63];
    out += TABLE[v & 63];
  }
  if (i + 1 == in.size()) {
    const uint32_t v = static_cast<uint8_t>(in[i]) << 16;
    out += TABLE[(v >> 18) & 63];
    out += TABLE[(v >> 12) & 63];
    out += "==";
  } else if (i + 2 == in.size()) {
    const uint32_t v = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8);
    out += TABLE[(v >> 18) & 63];
    out += TABLE[(v >> 12) & 63];
    out += TABLE[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

std::string basicAuthorization(const std::string_view user, const std::string_view password) {
  if (user.empty()) return {};
  std::string creds(user);
  creds += ':';
  creds.append(password);
  return "Basic " + base64(creds);
}

std::string requestHead(const std::string_view method, const Url& target, const std::vector<Header>& headers,
                        const int64_t contentLength, const std::string_view userAgent) {
  if (method.empty() || target.host.empty() || hasLineBreak(userAgent)) return {};
  for (const char c : method) {
    if (c < 'A' || c > 'Z') return {};
  }
  for (const char c : target.path) {
    if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) return {};
  }
  std::string head(method);
  head += ' ';
  head += target.path.empty() ? "/" : target.path;
  head += " HTTP/1.1\r\nHost: ";
  head += target.hostHeader();
  head += "\r\nUser-Agent: ";
  head.append(userAgent);
  head += "\r\nConnection: close\r\n";
  for (const auto& h : headers) {
    if (h.first.empty() || hasLineBreak(h.first) || hasLineBreak(h.second)) return {};
    if (h.first.find_first_of(": \t") != std::string::npos) return {};
    head += h.first;
    head += ": ";
    head += h.second;
    head += "\r\n";
  }
  if (contentLength >= 0) {
    head += "Content-Length: ";
    head += std::to_string(contentLength);
    head += "\r\n";
  }
  head += "\r\n";
  return head;
}

int statusOf(const std::string_view statusLine) {
  const std::string_view line = trim(statusLine);
  if (line.size() < 12 || line.substr(0, 5) != "HTTP/") return 0;
  const size_t space = line.find(' ');
  if (space == std::string_view::npos || space + 3 > line.size()) return 0;
  int status = 0;
  for (size_t i = space + 1; i < space + 4; ++i) {
    if (i >= line.size() || line[i] < '0' || line[i] > '9') return 0;
    status = status * 10 + (line[i] - '0');
  }
  if (space + 4 < line.size() && line[space + 4] != ' ') return 0;
  return status;
}

int64_t parseHttpDate(const std::string_view text) {
  static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const std::string_view s = trim(text);
  size_t pos = s.find(',');
  if (pos == std::string_view::npos) return 0;
  ++pos;
  while (pos < s.size() && s[pos] == ' ') ++pos;
  unsigned day = 0;
  if (!takeNumber(s, pos, 2, day) || !takeChar(s, pos, ' ')) return 0;
  if (pos + 3 > s.size()) return 0;
  unsigned month = 0;
  for (unsigned m = 0; m < 12; ++m) {
    if (s.compare(pos, 3, MONTHS + m * 3, 3) == 0) month = m + 1;
  }
  if (month == 0) return 0;
  pos += 3;
  unsigned year = 0;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  if (!takeChar(s, pos, ' ') || !takeNumber(s, pos, 4, year) || !takeChar(s, pos, ' ')) return 0;
  if (!takeNumber(s, pos, 2, hour) || !takeChar(s, pos, ':') || !takeNumber(s, pos, 2, minute)) return 0;
  if (!takeChar(s, pos, ':') || !takeNumber(s, pos, 2, second)) return 0;
  if (s.substr(pos) != " GMT") return 0;
  return remotefiles::epochUtc(year, month, day, hour, minute, second);
}

// ---- MultistatusParser ----

MultistatusParser::MultistatusParser(const std::string_view requestPath) : requestPath_(requestPath) {
  const XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) {
    error_ = Error::NoMemory;
    return;
  }
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);
  parser_ = parser;
}

MultistatusParser::~MultistatusParser() {
  auto parser = static_cast<XML_Parser>(parser_);
  destroyXmlParser(parser);
  parser_ = nullptr;
}

void MultistatusParser::onStart(void* self, const char* name, const char** /*atts*/) {
  static_cast<MultistatusParser*>(self)->start(name);
}

void MultistatusParser::onEnd(void* self, const char* name) { static_cast<MultistatusParser*>(self)->end(name); }

void MultistatusParser::onText(void* self, const char* text, const int len) {
  auto* p = static_cast<MultistatusParser*>(self);
  if (p->capture_ == Capture::None || len <= 0 || p->textOverflow_) return;
  if (p->text_.size() + static_cast<size_t>(len) > MAX_HREF_BYTES) {
    p->textOverflow_ = true;
    p->text_.clear();
    return;
  }
  p->text_.append(text, static_cast<size_t>(len));
}

namespace {
constexpr int MAX_XML_DEPTH = 32;
}  // namespace

void MultistatusParser::start(const char* name) {
  ++depth_;
  if (depth_ > MAX_XML_DEPTH) {  // a multistatus is a few levels deep; endless nesting only costs heap
    error_ = Error::Malformed;
    XML_StopParser(static_cast<XML_Parser>(parser_), XML_FALSE);
    return;
  }
  const char* local = xmlLocalName(name);
  Capture capture = Capture::None;
  if (depth_ == 1) {
    sawRoot_ = std::strcmp(local, "multistatus") == 0;
    if (!sawRoot_) {
      error_ = Error::Malformed;
      XML_StopParser(static_cast<XML_Parser>(parser_), XML_FALSE);
    }
    return;
  }
  if (!responseDepth_) {
    if (depth_ == 2 && std::strcmp(local, "response") == 0) {
      responseDepth_ = depth_;
      ++responses_;
      href_.clear();
      haveHref_ = false;
      size_ = 0;
      mtime_ = 0;
      isDir_ = false;
      sawType_ = false;
    }
    return;
  }
  if (depth_ == responseDepth_ + 1) {
    if (std::strcmp(local, "href") == 0 && !haveHref_) {
      capture = Capture::Href;
    } else if (std::strcmp(local, "propstat") == 0) {
      propstatDepth_ = depth_;
      statSize_ = 0;
      statMtime_ = 0;
      statStatus_ = 0;
      statIsDir_ = false;
      statSawType_ = false;
    }
  } else if (propstatDepth_ && depth_ == propstatDepth_ + 1) {
    if (std::strcmp(local, "prop") == 0) {
      propDepth_ = depth_;
    } else if (std::strcmp(local, "status") == 0) {
      capture = Capture::Status;
    }
  } else if (propDepth_ && depth_ == propDepth_ + 1) {
    if (std::strcmp(local, "getcontentlength") == 0) {
      capture = Capture::Length;
    } else if (std::strcmp(local, "getlastmodified") == 0) {
      capture = Capture::Modified;
    } else if (std::strcmp(local, "resourcetype") == 0) {
      typeDepth_ = depth_;
      statSawType_ = true;
    }
  } else if (typeDepth_ && depth_ == typeDepth_ + 1 && std::strcmp(local, "collection") == 0) {
    statIsDir_ = true;
  }
  if (capture != Capture::None) {
    capture_ = capture;
    text_.clear();
    textOverflow_ = false;
  }
}

void MultistatusParser::end(const char* /*name*/) {
  if (capture_ == Capture::Href && depth_ == responseDepth_ + 1) {
    haveHref_ = true;
    href_ = textOverflow_ ? std::string() : text_;
    capture_ = Capture::None;
  } else if (capture_ == Capture::Status && depth_ == propstatDepth_ + 1) {
    statStatus_ = textOverflow_ ? -1 : statusOf(text_);
    if (statStatus_ == 0) statStatus_ = -1;  // a status that cannot be read is not a 200
    capture_ = Capture::None;
  } else if (capture_ == Capture::Length && depth_ == propDepth_ + 1) {
    uint64_t size = 0;
    if (!textOverflow_ && parseDigits(trim(text_), size)) statSize_ = size;
    capture_ = Capture::None;
  } else if (capture_ == Capture::Modified && depth_ == propDepth_ + 1) {
    if (!textOverflow_) statMtime_ = parseHttpDate(text_);
    capture_ = Capture::None;
  }
  if (typeDepth_ && depth_ == typeDepth_) typeDepth_ = 0;
  if (propDepth_ && depth_ == propDepth_) propDepth_ = 0;
  if (propstatDepth_ && depth_ == propstatDepth_) {
    endPropstat();
    propstatDepth_ = 0;
  }
  if (responseDepth_ && depth_ == responseDepth_) {
    endResponse();
    responseDepth_ = 0;
  }
  if (depth_ == 1) rootClosed_ = true;
  --depth_;
}

void MultistatusParser::endPropstat() {
  // Properties under a propstat that is not a 200 (Apache lists the ones a resource lacks under
  // 404) are not this resource's values.
  if (statStatus_ != 0 && (statStatus_ < 200 || statStatus_ >= 300)) return;
  if (statSize_) size_ = statSize_;
  if (statMtime_) mtime_ = statMtime_;
  if (statSawType_) {
    sawType_ = true;
    isDir_ = isDir_ || statIsDir_;
  }
}

void MultistatusParser::endResponse() {
  std::string path;
  bool slash = false;
  if (href_.empty() || !hrefPath(href_, path, slash)) {
    ++listing_.skipped;
    return;
  }
  if (path == requestPath_) {
    sawSelf_ = true;
    return;
  }
  // A direct child only: "<request>/<one segment>".
  if (path.size() <= requestPath_.size() + 1 || path.compare(0, requestPath_.size(), requestPath_) != 0 ||
      path[requestPath_.size()] != '/') {
    return;
  }
  const std::string_view name = std::string_view(path).substr(requestPath_.size() + 1);
  if (name.find('/') != std::string_view::npos) return;

  remotefiles::Entry entry;
  entry.name = std::string(name);
  // A server that sends no resourcetype at all still marks folders with a trailing '/'.
  entry.isDir = sawType_ ? isDir_ : slash;
  entry.size = entry.isDir ? 0 : size_;
  entry.mtime = mtime_;
  listing_.add(std::move(entry));
  if (listing_.truncated) XML_StopParser(static_cast<XML_Parser>(parser_), XML_FALSE);
}

bool MultistatusParser::feed(const char* data, const size_t len) {
  if (error_ != Error::None || listing_.truncated || !parser_) return false;
  if (len == 0) return true;
  if (taken_ + len > MAX_MULTISTATUS_BYTES) {
    listing_.truncated = true;
    return false;
  }
  taken_ += len;
  const auto parser = static_cast<XML_Parser>(parser_);
  if (XML_Parse(parser, data, static_cast<int>(len), 0) == XML_STATUS_ERROR) {
    if (listing_.truncated) return false;  // stopped at a cap, by us
    if (error_ == Error::None) {
      error_ = XML_GetErrorCode(parser) == XML_ERROR_NO_MEMORY ? Error::NoMemory : Error::Malformed;
    }
    return false;
  }
  return true;
}

bool MultistatusParser::finish() {
  if (error_ != Error::None || !parser_) return false;
  if (listing_.truncated) return true;
  const auto parser = static_cast<XML_Parser>(parser_);
  if (XML_Parse(parser, nullptr, 0, 1) == XML_STATUS_ERROR) {
    error_ = XML_GetErrorCode(parser) == XML_ERROR_NO_MEMORY ? Error::NoMemory : Error::Malformed;
    return false;
  }
  if (!sawRoot_ || !rootClosed_) {
    error_ = Error::Malformed;
    return false;
  }
  return true;
}

}  // namespace webdav
