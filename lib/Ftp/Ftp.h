#pragma once

#include <RemoteFiles.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// A small FTP client without any platform code: addresses, commands, reply and listing parsers,
// and the conversation itself (Session) over two abstract byte channels, so it runs in host tests
// against in-memory channels. The network side is src/network/FtpClient.
//
// Plain FTP only: the user name, the password and the files cross the network unencrypted. The
// screens say so before every connection. No FTPS, no active mode.
namespace ftp {

// One line of the control connection or of a listing; a longer control line ends the session, a
// longer listing line is skipped.
inline constexpr size_t MAX_LINE_BYTES = 1024;
// A reply may have this many lines (a long welcome banner).
inline constexpr size_t MAX_REPLY_LINES = 64;
// Files move through one buffer of this size.
inline constexpr size_t IO_BUFFER_BYTES = 4096;

struct Url {
  std::string host;
  uint16_t port = 21;
  std::string path = "/";  // the start folder, absolute
};

// "ftp://host[:port][/path]" or just "host[:port][/path]". False for another scheme (ftps, sftp,
// http), user info in the address, an IPv6 literal, or a bad port.
bool parseUrl(std::string_view url, Url& out);

// "VERB arg\r\n". Empty when the argument holds a CR, LF or NUL: a file name cannot add a command.
std::string command(std::string_view verb, std::string_view arg = {});

struct Reply {
  int code = 0;      // 0: none
  std::string text;  // the last line, without the code
  bool positive() const { return code >= 200 && code < 300; }
  bool preliminary() const { return code >= 100 && code < 200; }
  bool intermediate() const { return code >= 300 && code < 400; }
};

// Fed the control connection's lines (without CR LF) until a reply is whole.
class ReplyParser {
 public:
  enum class State : uint8_t { More, Done, Bad };
  State feedLine(std::string_view line);
  const Reply& reply() const { return reply_; }
  void reset();

 private:
  Reply reply_;
  size_t lines_ = 0;
  int open_ = 0;  // the code of a multi-line reply that has not ended
};

// "227 Entering Passive Mode (192,168,1,2,19,137)" -> 5001. The address in the reply is ignored
// on purpose: the data connection always goes to the host the control connection is on.
bool parsePasv(std::string_view text, uint16_t& port);
// "229 Entering Extended Passive Mode (|||5001|)" -> 5001.
bool parseEpsv(std::string_view text, uint16_t& port);

// One line of a LIST answer, Unix ("drwxr-xr-x 2 u g 4096 Jan 12 10:00 name") or DOS
// ("01-12-26  10:00AM  <DIR>  name"). False for "total 12", "." / ".." and anything else that is
// not an entry. A symbolic link is listed as a file under its own name.
bool parseListLine(std::string_view line, remotefiles::Entry& out);
// One line of an MLSD answer ("type=file;size=12;modify=20260112100000; name"). False for the
// folder itself, its parent and types other than file and dir.
bool parseMlsdLine(std::string_view line, remotefiles::Entry& out);

// One end of a TCP connection.
class Channel {
 public:
  virtual ~Channel() = default;
  virtual bool write(const uint8_t* data, size_t len) = 0;
  // Blocks until some bytes are there: > 0 bytes read, 0 the other side closed, < 0 a timeout or
  // an error.
  virtual int read(uint8_t* buf, size_t len) = 0;
  virtual void close() = 0;
};

class Transport {
 public:
  virtual ~Transport() = default;
  virtual Channel& control() = 0;
  // A data connection to `port` on the host of the control connection; nullptr when it fails.
  // One at a time; the session closes it.
  virtual Channel* openData(uint16_t port) = 0;
  // The user asked to stop.
  virtual bool aborted() { return false; }
};

enum class Error : uint8_t {
  Ok,
  Network,   // the connection failed, closed or timed out
  Login,     // the server refused the user name or password
  Refused,   // the server answered no to a command (no such folder, no permission, disk full)
  Protocol,  // an answer that is not FTP, or a line over the limit
  Local,     // the file on the card could not be read or written
  Aborted,
  BadName,  // a name with a line break in it
  NoMemory,
};

class Session {
 public:
  // > 0 bytes taken; false stops the transfer (Error::Local).
  using Sink = std::function<bool(const uint8_t* data, size_t len)>;
  // Fills `buf`: > 0 bytes, 0 the end, < 0 a read error.
  using Source = std::function<int(uint8_t* buf, size_t len)>;

  explicit Session(Transport& transport) : transport_(transport) {}

  // Reads the greeting and signs in; an empty user signs in as "anonymous". Then TYPE I.
  Error login(std::string_view user, std::string_view password);
  // The entries of `dir` (absolute). MLSD when the server has it, else LIST.
  Error list(std::string_view dir, remotefiles::Listing& out);
  Error retrieve(std::string_view dir, std::string_view name, const Sink& sink);
  // Sent as "<name>.part" and renamed when the server has taken all of it, so a broken upload
  // never replaces a good file; the .part is deleted when the upload fails.
  Error store(std::string_view dir, std::string_view name, const Source& source);
  Error makeDir(std::string_view dir, std::string_view name);
  // Says goodbye without waiting for the answer (a server that is gone must not hold up leaving
  // the screen); the caller closes the control channel.
  void quit();

  const Reply& lastReply() const { return reply_; }

 private:
  Error send(std::string_view verb, std::string_view arg = {});
  Error readReply();
  // Sends a command and reads its reply.
  Error exchange(std::string_view verb, std::string_view arg = {});
  bool readControlLine(std::string& line, Error& error);
  Error changeDir(std::string_view dir);
  Error openPassive(Channel*& data);
  Error finishTransfer(Channel* data, Error transferError, bool ignoreFinalCode);

  Transport& transport_;
  Reply reply_;
  uint8_t ctrlBuf_[256] = {};
  size_t ctrlLen_ = 0;
  size_t ctrlPos_ = 0;
  bool epsvUnsupported_ = false;
  bool mlsdUnsupported_ = false;
};

}  // namespace ftp
