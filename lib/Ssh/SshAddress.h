#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// The address of an SSH / SFTP server as the user types it, and small SFTP rules. No platform code.
namespace sshaddr {

struct Address {
  std::string host;  // a name or an IP address, never empty after parse()
  uint16_t port = 22;
  std::string path;  // SFTP start folder: absolute, or empty for the folder the server opens (home)
};

// "host", "host:2222", "host/srv/books", "host:2222/srv/books", "[2001:db8::1]:22/path", each with
// an optional "ssh://" or "sftp://" in front. A user name in the address ("me@host") is refused:
// the user name has its own field. False for anything else.
bool parse(std::string_view text, Address& out);

// "host" or "host:port" for the "Connected to ..." line.
std::string display(const Address& address);

// A file mode from SFTP attributes: is it a folder / a plain file?
constexpr uint32_t MODE_TYPE_MASK = 0170000;
constexpr uint32_t MODE_DIR = 0040000;
constexpr uint32_t MODE_FILE = 0100000;
inline bool isDirMode(const uint32_t mode) { return (mode & MODE_TYPE_MASK) == MODE_DIR; }
inline bool isFileMode(const uint32_t mode) { return (mode & MODE_TYPE_MASK) == MODE_FILE; }

// The path on the server for `dir` (as the browser knows it: "/" is the start folder) under the
// start folder `base` (absolute). False when the result would climb above `base`.
bool remotePath(std::string_view base, std::string_view dir, std::string& out);

}  // namespace sshaddr
