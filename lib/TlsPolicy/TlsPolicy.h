#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// Which certificate check an HTTPS request gets. The rules, in one place:
//
//  * A public host the firmware itself talks to (GitHub for updates and fonts, Wikipedia) is
//    always checked against the built-in public roots. There is no switch for it and a ca.pem on
//    the card does not replace those roots. Readwise (highlight exports) is always checked too,
//    against every built-in root.
//  * A server the user set up (OPDS, KOReader sync, a plugin's server) is checked only when the
//    user asked for it: with /.crosspoint/ca.pem when that file is there, else with every built-in
//    root. A ca.pem that cannot be read stops the request; it never falls back to the built-in
//    roots. Without the user's say-so the connection is encrypted but not checked, as before.
//  * A checked connection needs the real date. A clock near 1970 is synced first, and the request
//    stops when that fails.
//
// No certificate pinning: SecureNet (read-only SDK) keeps the wolfSSL session private and has no
// verify callback, so the peer certificate cannot be read from here.
//
// This file has no platform code; TlsPolicyDevice.h applies a decision to a SecureHttpClient.
namespace tlspolicy {

// GitHub (the update check, the firmware and font downloads) is NOT always checked in 0.74.0: nobody has seen a
// real handshake with its P-384 chains on a device yet, and a failure there would stop Wi-Fi updates for everyone.
// The code for it is all here: turn this on after a device has done an update check, a font download and a
// Wikipedia lookup (ROADMAP, Release B gate). Wikipedia is new, so it is always checked.
inline constexpr bool VERIFY_GITHUB = false;

enum class Mode : uint8_t {
  Plain,     // not https: nothing to check
  Insecure,  // https, encrypted, certificate not checked (the user's own server, check not asked for)
  Verify,    // checked against the built-in roots
  CustomCa,  // checked against the user's ca.pem only
};

enum class Error : uint8_t {
  None,
  NoClock,       // the date is unknown and could not be set, so certificate dates cannot be checked
  CaUnreadable,  // ca.pem is there but empty or unreadable
};

// Lower-case host of a URL, without user info, port or IPv6 brackets. Empty when there is none.
std::string hostOf(std::string_view url);
bool isHttps(std::string_view url);
// A host of GitHub (github.com, api.github.com, *.githubusercontent.com) or Wikipedia.
bool isPublicHost(std::string_view host);
// The hosts that are always checked: Wikipedia, Readwise (readwise.io), and GitHub once VERIFY_GITHUB is on.
bool alwaysChecked(std::string_view host);

// The mode for `url`. `userVerify`: the user turned the check on for this server. `caFileExists`:
// the shared ca.pem is on the card. Both are ignored for a public host.
Mode choose(std::string_view url, bool userVerify, bool caFileExists);

struct Clock {
  std::function<int64_t()> now;  // epoch seconds, 0 while the date is not trustworthy
  std::function<bool()> sync;    // try to set it (blocking); the result is read back through now()
};

struct CaFile {
  std::function<bool()> exists;
  std::function<bool(std::string& pem)> read;
};

struct Decision {
  Mode mode = Mode::Plain;
  Error error = Error::None;
  bool ok() const { return error == Error::None; }
  bool checked() const { return mode == Mode::Verify || mode == Mode::CustomCa; }
};

// Everything needed before the handshake: picks the mode, loads ca.pem into `caPem` when the mode
// is CustomCa (kept when already loaded), and makes sure the clock is set for a checked mode. On
// an error the request must not be sent.
Decision decide(std::string_view url, bool userVerify, const Clock& clock, const CaFile& caFile, std::string& caPem);

// One plain sentence for the log or the screen.
const char* errorText(Error error);

}  // namespace tlspolicy
