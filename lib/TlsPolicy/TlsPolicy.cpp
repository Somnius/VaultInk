#include "TlsPolicy.h"

#include <algorithm>

namespace tlspolicy {

namespace {

char lower(const char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool startsWithNoCase(const std::string_view text, const std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); i++) {
    if (lower(text[i]) != prefix[i]) return false;
  }
  return true;
}

// `host` is `domain` itself or a name under it ("en.wikipedia.org" under "wikipedia.org").
bool isOrUnder(const std::string_view host, const std::string_view domain) {
  if (host == domain) return true;
  return host.size() > domain.size() && host.substr(host.size() - domain.size()) == domain &&
         host[host.size() - domain.size() - 1] == '.';
}

}  // namespace

bool isHttps(const std::string_view url) { return startsWithNoCase(url, "https://"); }

std::string hostOf(const std::string_view url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string_view::npos) return {};
  std::string_view rest = url.substr(schemeEnd + 3);
  rest = rest.substr(0, rest.find_first_of("/?#"));
  // Anything before the last '@' is user info, which may itself hold a host-looking name.
  if (const size_t at = rest.rfind('@'); at != std::string_view::npos) rest.remove_prefix(at + 1);
  if (!rest.empty() && rest.front() == '[') {
    const size_t close = rest.find(']');
    if (close == std::string_view::npos) return {};
    rest = rest.substr(1, close - 1);
  } else {
    rest = rest.substr(0, rest.find(':'));
  }
  while (!rest.empty() && rest.back() == '.') rest.remove_suffix(1);  // "github.com." is github.com
  std::string host(rest);
  std::transform(host.begin(), host.end(), host.begin(), lower);
  return host;
}

bool isPublicHost(const std::string_view host) {
  return isOrUnder(host, "github.com") || isOrUnder(host, "githubusercontent.com") || isOrUnder(host, "wikipedia.org");
}

bool alwaysChecked(const std::string_view host) {
  // readwise.io is not in isPublicHost: which root its certificate chains to has not been seen from a
  // device, so it is checked against every built-in root, not only the public group.
  return isOrUnder(host, "wikipedia.org") || isOrUnder(host, "readwise.io") || (VERIFY_GITHUB && isPublicHost(host));
}

Mode choose(const std::string_view url, const bool userVerify, const bool caFileExists) {
  if (!isHttps(url)) return Mode::Plain;
  if (alwaysChecked(hostOf(url))) return Mode::Verify;
  if (!userVerify) return Mode::Insecure;
  return caFileExists ? Mode::CustomCa : Mode::Verify;
}

Decision decide(const std::string_view url, const bool userVerify, const Clock& clock, const CaFile& caFile,
                std::string& caPem) {
  Decision d;
  // Only ask the card when the answer can matter.
  const bool ownServer = isHttps(url) && userVerify && !alwaysChecked(hostOf(url));
  d.mode = choose(url, userVerify, ownServer && caFile.exists && caFile.exists());
  if (!d.checked()) return d;
  if (d.mode == Mode::CustomCa && caPem.empty() && (!caFile.read || !caFile.read(caPem) || caPem.empty())) {
    caPem.clear();
    d.error = Error::CaUnreadable;
    return d;
  }
  if (!clock.now || clock.now() == 0) {
    if (clock.sync) clock.sync();
    if (!clock.now || clock.now() == 0) d.error = Error::NoClock;
  }
  return d;
}

const char* errorText(const Error error) {
  switch (error) {
    case Error::NoClock:
      return "The date could not be set, so the server's certificate cannot be checked.";
    case Error::CaUnreadable:
      return "ca.pem could not be read, so the server's certificate cannot be checked.";
    case Error::None:
    default:
      return "";
  }
}

}  // namespace tlspolicy
