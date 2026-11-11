#pragma once

#include <RemoteFiles.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The platform-free parts of the WebDAV client: addresses, the redirect rule, request heads, and
// the reader for a PROPFIND answer. The network side is src/network/WebDavClient.
namespace webdav {

// Files move through one buffer of this size.
inline constexpr size_t IO_BUFFER_BYTES = 4096;
// No more than this much of a PROPFIND answer is read; past it the listing is marked as cut.
inline constexpr size_t MAX_MULTISTATUS_BYTES = 512 * 1024;
// One href longer than this is skipped.
inline constexpr size_t MAX_HREF_BYTES = 2048;
inline constexpr int MAX_REDIRECTS = 3;

struct Url {
  bool https = false;
  std::string host;  // lower case
  uint16_t port = 0;
  std::string path;  // percent-encoded, starts with '/', no trailing '/' ("" for the root), no query

  // "https://host[:port]" (the port only when it is not the scheme's own).
  std::string origin() const;
  std::string hostHeader() const;
};

// An http:// or https:// address. False for anything else, for user info in the address
// ("user:pass@host": credentials belong in their own fields), for an IPv6 literal, a bad port or
// a bad percent escape. A query or fragment is dropped.
bool parseUrl(std::string_view url, Url& out);

// False on a broken escape or an escaped NUL.
bool percentDecode(std::string_view in, std::string& out);
// Encodes each segment of a decoded path, keeping '/'.
std::string encodePath(std::string_view decodedPath);

// The decoded path of an href from a multistatus ("/dav/a%20b/", or a whole URL). False when it
// has no path or a broken escape. A trailing '/' is reported in `endsWithSlash` and removed.
bool hrefPath(std::string_view href, std::string& decodedPath, bool& endsWithSlash);

// The address of `remotePath` (decoded, absolute, "/" is the server's base folder) under `base`.
// A folder gets a trailing '/', as servers expect for PROPFIND and MKCOL.
std::string resourceUrl(const Url& base, std::string_view remotePath, bool collection);
// The decoded path a PROPFIND on that address is about, without a trailing '/' ("" for "/").
std::string decodedRequestPath(const Url& base, std::string_view remotePath);

// A redirect is followed only to the same host, and never from https to http. (The password goes
// along, so it must not reach another server or lose its encryption.)
bool redirectAllowed(const Url& from, const Url& to);

// "Basic dXNlcjpwYXNz"; empty when there is no user.
std::string basicAuthorization(std::string_view user, std::string_view password);
std::string base64(std::string_view in);

using Header = std::pair<std::string, std::string>;
// "METHOD /path HTTP/1.1" with Host, User-Agent, Connection: close, the given headers and, when
// `contentLength` is not negative, Content-Length. Empty when the method or a header holds a CR,
// LF or (in a name) a ':' or space: nothing a server or a file name gave us can add a header.
std::string requestHead(std::string_view method, const Url& target, const std::vector<Header>& headers,
                        int64_t contentLength, std::string_view userAgent);

// 201 from "HTTP/1.1 201 Created"; 0 when the line is not a status line.
int statusOf(std::string_view statusLine);

// "Mon, 12 Jan 2026 10:00:00 GMT" as epoch seconds; 0 when it is not an RFC 1123 date.
int64_t parseHttpDate(std::string_view text);

// The body of the PROPFIND request: only the three properties the list shows.
extern const char PROPFIND_BODY[];

// Reads a 207 Multi-Status answer as it arrives and keeps, for each direct child of the folder
// asked about: the name, the size, the folder flag and the change time. The folder itself and
// anything deeper are passed over. Element prefixes do not matter ("D:", "d:", none).
class MultistatusParser {
 public:
  enum class Error : uint8_t { None, NoMemory, Malformed };

  // `requestPath`: decodedRequestPath() of the folder that was asked for.
  explicit MultistatusParser(std::string_view requestPath);
  ~MultistatusParser();
  MultistatusParser(const MultistatusParser&) = delete;
  MultistatusParser& operator=(const MultistatusParser&) = delete;

  // False: stop reading (an error, or a cap was hit and the listing is marked as cut).
  bool feed(const char* data, size_t len);
  // After the last byte. True when the answer was a whole multistatus, or was cut at a cap.
  bool finish();

  Error error() const { return error_; }
  remotefiles::Listing& listing() { return listing_; }
  // The answer named the folder itself (most servers do; not required here).
  bool sawSelf() const { return sawSelf_; }
  size_t responses() const { return responses_; }

 private:
  enum class Capture : uint8_t { None, Href, Length, Modified, Status };

  static void onStart(void* self, const char* name, const char** atts);
  static void onEnd(void* self, const char* name);
  static void onText(void* self, const char* text, int len);
  void start(const char* name);
  void end(const char* name);
  void endResponse();
  void endPropstat();

  void* parser_ = nullptr;  // XML_Parser
  remotefiles::Listing listing_;
  std::string requestPath_;
  std::string text_;
  std::string href_;
  size_t taken_ = 0;
  size_t responses_ = 0;
  uint64_t size_ = 0;
  uint64_t statSize_ = 0;
  int64_t mtime_ = 0;
  int64_t statMtime_ = 0;
  int depth_ = 0;
  int responseDepth_ = 0;  // 0: not inside a response
  int propstatDepth_ = 0;
  int propDepth_ = 0;
  int typeDepth_ = 0;  // inside resourcetype
  int statStatus_ = 0;
  Error error_ = Error::None;
  Capture capture_ = Capture::None;
  bool textOverflow_ = false;
  bool haveHref_ = false;
  bool isDir_ = false;
  bool sawType_ = false;
  bool statIsDir_ = false;
  bool statSawType_ = false;
  bool sawSelf_ = false;
  bool sawRoot_ = false;
  bool rootClosed_ = false;
};

}  // namespace webdav
