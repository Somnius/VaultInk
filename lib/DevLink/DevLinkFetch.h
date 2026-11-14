#pragma once

// The pure parts of CMD:FETCH <http url> <sd path> <sha256> (docs/devlink.md): the URL and address
// guard, the arguments, the SHA-256 text and the head of the HTTP answer. Platform-free and
// heap-free, tested in test/dev_link. Compiled only with VAULTINK_DEVLINK_FULL (x4pro-dev,
// x4pro-dev-verify): no other build has any of it.
#ifdef VAULTINK_DEVLINK_FULL

#include <cstddef>
#include <cstdint>

namespace devlink {

// ---- the URL guard ----

constexpr size_t FETCH_MAX_URL = 160;
// The most FETCH writes: the size of one app slot (partitions_vaultink.csv, 0x7F0000).
constexpr uint32_t FETCH_MAX_BYTES = 0x7F0000;
constexpr size_t SHA256_BYTES = 32;
constexpr size_t SHA256_HEX = 64;

enum class UrlCheck : uint8_t {
  Ok,
  TooLong,     // over FETCH_MAX_URL bytes, or empty
  NotHttp,     // anything but "http://" in lower case (https, ftp, no scheme)
  BadHost,     // not four plain decimal numbers: a host name, userinfo, IPv6, octal, hex, a blank
  NotPrivate,  // an IPv4 address outside 10/8, 172.16/12 and 192.168/16
  BadPort,     // not 1 to 65535 written without a leading zero
  BadPath,     // see checkFetchUrl
};

struct FetchUrl {
  uint8_t ip[4] = {};
  uint16_t port = 80;
  // The path as it is sent in the request. Points into the checked text, not terminated.
  const char* path = "";
  size_t pathLength = 0;
};

// True for 10.0.0.0/8, 172.16.0.0/12 and 192.168.0.0/16, and nothing else: not loopback (127/8),
// not link-local (169.254/16), not 0.0.0.0, not the carrier range 100.64/10.
bool isPrivateIpv4(const uint8_t ip[4]);

// "http://a.b.c.d[:port]/path", the whole text and nothing after it.
//   scheme  exactly "http://"
//   host    four decimal numbers 0 to 255 with no leading zero, a private address (isPrivateIpv4)
//   port    left out (80), or 1 to 65535 with no leading zero
//   path    starts with '/', does not end with one, letters, digits and . _ - ~ / only, no ".."
//           and no "//". So no query, no fragment, no percent escape, no blank, no backslash.
// `out` is filled only for Ok.
UrlCheck checkFetchUrl(const char* url, size_t length, FetchUrl& out);

// A few words for the reply's "reason".
const char* urlCheckText(UrlCheck check);

// ---- the arguments ----

// 64 hex digits, either case. False for anything else.
bool parseSha256Hex(const char* text, size_t length, uint8_t out[SHA256_BYTES]);
// 64 lower-case hex digits and a terminator.
void sha256Hex(const uint8_t digest[SHA256_BYTES], char out[SHA256_HEX + 1]);

struct FetchArgs {
  const char* url = "";
  size_t urlLength = 0;
  const char* path = "";
  size_t pathLength = 0;
  uint8_t sha256[SHA256_BYTES] = {};
};
// "<url> <sd path> <sha256>": the first word is the URL, the last one the hash, and the path is
// what is between them and may hold blanks. The URL and the path are not checked here.
bool parseFetchArgs(const char* args, size_t length, FetchArgs& out);

// "<sd path> [sha256]" of CMD:FLASH: a last word of 64 hex digits, with a path in front of it, is
// the hash the file must have; anything else is all path (which may hold blanks).
struct FlashArgs {
  const char* path = "";
  size_t pathLength = 0;
  bool hasSha256 = false;
  uint8_t sha256[SHA256_BYTES] = {};
};
bool parseFlashArgs(const char* args, size_t length, FlashArgs& out);

// ---- when a transfer stops ----

constexpr uint32_t FETCH_DEADLINE_MS = 180000;  // the whole transfer
constexpr uint32_t FETCH_STALL_MS = 10000;      // no byte read for this long

enum class FetchWait : uint8_t {
  Ready,      // there is something to read
  Wait,       // nothing yet: look again in a moment
  Cancelled,  // a byte came in on the serial port (a new command)
  Deadline,   // FETCH_DEADLINE_MS since the start
  Stalled,    // FETCH_STALL_MS since the last byte that was really read
  Closed,     // the server closed and nothing is left to read
};
// One look at the transfer. `lastByteMs` is when a byte was last READ, not when one was last said
// to be there: a socket that reports data and then gives none cannot keep the transfer alive. The
// cancel and the two time limits are looked at before the data, so a server that never stops
// sending cannot hold the reader either. The times are millis(); a wrapped clock still works.
FetchWait fetchWaitStep(uint32_t nowMs, uint32_t startMs, uint32_t lastByteMs, bool cancel, bool available,
                        bool connected);
const char* fetchWaitText(FetchWait step);

// ---- the head of the answer ----

// Reads the status line and the header lines of an HTTP answer, a byte at a time, and keeps the
// status and Content-Length. It holds one line of HEAD_LINE_MAX bytes: a longer header line is skipped,
// a longer status line is an error. The head ends at the first empty line.
class HttpHead {
 public:
  static constexpr size_t HEAD_LINE_MAX = 160;
  static constexpr size_t HEAD_BYTES_MAX = 4096;

  enum class State : uint8_t { More, Done, Bad };

  // Done: the byte ended the head, the body starts with the next one. Bad: this is not an HTTP
  // answer, or its head is over HEAD_BYTES_MAX bytes. After Done or Bad, feed() returns the same again.
  State feed(char c);

  int status() const { return statusCode; }
  bool hasLength() const { return lengthSeen; }
  uint32_t contentLength() const { return length; }
  // A Transfer-Encoding header was there (the body is then not a plain run of bytes).
  bool encoded() const { return transferEncoding; }

 private:
  void endLine();

  char line[HEAD_LINE_MAX + 1] = {};
  size_t used = 0;
  size_t total = 0;
  uint32_t length = 0;
  int statusCode = 0;
  State state = State::More;
  bool lineTooLong = false;
  bool statusRead = false;
  bool lengthSeen = false;
  bool transferEncoding = false;
};

enum class HeadCheck : uint8_t { Ok, NotOk200, Encoded, NoLength, Empty, TooLarge };
// What FETCH takes: status 200, no Transfer-Encoding, a Content-Length from 1 to maxBytes.
// Redirects (3xx) are NotOk200: they are never followed.
HeadCheck checkHead(const HttpHead& head, uint32_t maxBytes);
const char* headCheckText(HeadCheck check);

}  // namespace devlink

#endif  // VAULTINK_DEVLINK_FULL
