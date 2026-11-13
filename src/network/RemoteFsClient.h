#pragma once

#include <RemoteFiles.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct RemoteServer;

// One interface over the WebDAV, the FTP and the SFTP client, so the browse, download and upload screens are
// written once. Wi-Fi must be up already. Every call blocks until it is done, fails or is cancelled.
//
// Paths are absolute, decoded UTF-8, with "/" standing for the folder the server's address points
// at; names are single path segments that passed remotefiles::safeName().
namespace remotefs {

enum class Error : uint8_t {
  Ok,
  BadAddress,    // the server's address cannot be used
  PlainRefused,  // not encrypted, and the user did not say yes to that on the screen
  LowMemory,     // not enough free heap for the connection
  NoClock,       // the date could not be set, so the certificate cannot be checked
  Certificate,   // ca.pem could not be read
  Network,       // no answer: DNS, connect, the certificate check, a timeout, a dropped connection
  Auth,          // wrong user name or password, or no permission
  NotFound,      // no such folder or file
  Refused,       // the server said no (read-only, disk full, a name it does not take)
  Redirected,    // the server sent us to another server, or from https to http
  BadAnswer,     // an answer that is not WebDAV / FTP
  File,          // the file on the card could not be read or written
  Aborted,       // stopped by the user
  // SSH (SFTP) only:
  HostKeyUnknown,      // first connection to this server: its key is not pinned yet. Ask, then trustHostKey()
  HostKeyChanged,      // the server's key is not the pinned one: hard stop, never accepted from here
  NoCipher,            // the server and this firmware share no key exchange, host key type or cipher
  Unavailable,         // SSH is not in this build
  HostKeysUnreadable,  // the file of pinned keys is there but cannot be read: hard stop
  HostKeysChanged,     // the file of pinned keys was changed outside the device: hard stop
};

struct Transfer {
  // Called as bytes move; `total` is 0 when unknown. Also where the screen reads the buttons.
  std::function<void(size_t done, size_t total)> progress;
  const bool* cancel = nullptr;
  bool cancelled() const { return cancel && *cancel; }
};

class Client {
 public:
  virtual ~Client() = default;
  // The folder to show first.
  virtual std::string startDir() const { return "/"; }
  // The entries of `dir`, folders first. `out.truncated` when the list was cut at a cap.
  virtual Error list(const std::string& dir, remotefiles::Listing& out, const Transfer& transfer) = 0;
  // Into "<localPath>.part", moved over `localPath` only when all of it arrived.
  virtual Error download(const std::string& dir, const std::string& name, const std::string& localPath,
                         uint64_t expectedSize, const Transfer& transfer) = 0;
  virtual Error upload(const std::string& localPath, const std::string& dir, const std::string& name,
                       const Transfer& transfer) = 0;
  virtual Error makeDir(const std::string& dir, const std::string& name) = 0;
  // Says goodbye and closes every connection.
  virtual void close() {}
  // SSH only. After HostKeyUnknown or HostKeyChanged: the key the server showed.
  virtual bool hostKey(std::string& type, std::string& fingerprint) const {
    (void)type;
    (void)fingerprint;
    return false;
  }
  // SSH only. After HostKeyUnknown, when the user said yes to the fingerprint: pin that key. The
  // next call connects again and must then see the same key.
  virtual bool trustHostKey() { return false; }
};

// `plainConfirmed`: the user answered yes on the "not encrypted" screen for this connection. A
// server that is not encrypted (FTP, or WebDAV over http://) gives PlainRefused without it.
// nullptr with `error` set when the server cannot be used.
std::unique_ptr<Client> makeClient(const RemoteServer& server, bool plainConfirmed, Error& error);

// Used by makeClient().
std::unique_ptr<Client> makeWebDavClient(const RemoteServer& server, Error& error);
std::unique_ptr<Client> makeFtpClient(const RemoteServer& server, Error& error);
// `server.password` is the password typed for this connection (SSH passwords are never saved), or
// `server.keyAuth` for the device's key.
std::unique_ptr<Client> makeSftpClient(const RemoteServer& server, Error& error);

}  // namespace remotefs
