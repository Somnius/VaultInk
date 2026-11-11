// The simulator has host network shims in place of SecureNet and WiFiClient (no bare TLS client, no
// raw sockets with this API), so there this file only says that the server cannot be reached.
#ifndef SIMULATOR

// clang-format off
// HalStorage.h brings in SdFat, whose macros have to be seen before the network headers.
#include <HalStorage.h>
#include "RemoteFsClient.h"
// clang-format on

#include <Arduino.h>
#include <Ftp.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFiClient.h>

#include <utility>

#include "RemoteServerStore.h"
#include "WifiPowerSaveGuard.h"

// The network side of lib/Ftp: plain FTP over two WiFiClient connections (control and passive
// data). Nothing here is encrypted; the screens say so before every connection.
namespace remotefs {

namespace {

constexpr uint32_t TIMEOUT_MS = 20000;

class WifiChannel final : public ftp::Channel {
 public:
  bool connect(const std::string& host, const uint16_t port) {
    client.stop();
    return client.connect(host.c_str(), port, static_cast<int32_t>(TIMEOUT_MS)) != 0;
  }

  bool write(const uint8_t* data, size_t len) override {
    while (len > 0) {
      if (cancel && *cancel) return false;
      const size_t n = client.write(data, len);
      if (n == 0) return false;
      data += n;
      len -= n;
    }
    return true;
  }

  int read(uint8_t* buf, const size_t len) override {
    const unsigned long start = millis();
    for (;;) {
      if (cancel && *cancel) return -1;
      if (client.available() > 0) {
        const int n = client.read(buf, len);
        if (n > 0) return n;
      } else if (!client.connected()) {
        return 0;
      }
      if (millis() - start >= TIMEOUT_MS) return -1;
      delay(2);
    }
  }

  void close() override { client.stop(); }

  WiFiClient client;
  const bool* cancel = nullptr;
};

class WifiTransport final : public ftp::Transport {
 public:
  ftp::Channel& control() override { return ctrl; }
  ftp::Channel* openData(const uint16_t port) override {
    // Always to the host we are signed in to, whatever address a PASV answer names.
    return data.connect(host, port) ? &data : nullptr;
  }
  bool aborted() override { return ctrl.cancel && *ctrl.cancel; }
  void setCancel(const bool* cancel) {
    ctrl.cancel = cancel;
    data.cancel = cancel;
  }

  WifiChannel ctrl;
  WifiChannel data;
  std::string host;
};

class FtpClient final : public Client {
 public:
  FtpClient(ftp::Url url, std::string user, std::string password)
      : url_(std::move(url)), user_(std::move(user)), password_(std::move(password)) {
    transport_.host = url_.host;
  }
  ~FtpClient() override { drop(); }

  Error list(const std::string& dir, remotefiles::Listing& out, const Transfer& transfer) override;
  Error download(const std::string& dir, const std::string& name, const std::string& localPath, uint64_t expectedSize,
                 const Transfer& transfer) override;
  Error upload(const std::string& localPath, const std::string& dir, const std::string& name,
               const Transfer& transfer) override;
  Error makeDir(const std::string& dir, const std::string& name) override;
  void close() override;

 private:
  // The path on the server of `dir`, which is relative to the start folder of the address.
  std::string remote(const std::string& dir) const {
    std::string out;
    if (!remotefiles::normalizePath(url_.path + "/" + dir, out)) out = url_.path;
    return out;
  }
  Error ensureSession();
  void drop();
  Error map(ftp::Error error, bool missingIsNotFound) const;

  // Runs `op` on a signed-in session. A control connection the server closed while the list sat
  // on the screen is opened again once.
  template <typename Op>
  Error run(const Transfer& transfer, const bool missingIsNotFound, Op&& op) {
    WifiPowerSaveGuard psGuard;
    transport_.setCancel(transfer.cancel);
    Error result = Error::Ok;
    for (int attempt = 0; attempt < 2; ++attempt) {
      const bool reused = static_cast<bool>(session_);
      result = ensureSession();
      if (result != Error::Ok) break;
      const ftp::Error e = op(*session_);
      result = map(e, missingIsNotFound);
      if (e == ftp::Error::Aborted) {
        // A cancelled transfer leaves the server's last reply unread, which would shift every later answer by one:
        // the session is dropped, never reused.
        drop();
        break;
      }
      if (e != ftp::Error::Network && e != ftp::Error::Protocol) break;
      drop();
      if (!reused || e != ftp::Error::Network || transfer.cancelled()) break;
    }
    transport_.setCancel(nullptr);
    return result;
  }

  ftp::Url url_;
  std::string user_;
  std::string password_;
  WifiTransport transport_;
  std::unique_ptr<ftp::Session> session_;
};

Error FtpClient::ensureSession() {
  if (session_) return Error::Ok;
  LOG_DBG("FTP", "Connecting to %s:%u (not encrypted)", url_.host.c_str(), static_cast<unsigned>(url_.port));
  if (!transport_.ctrl.connect(url_.host, url_.port)) return Error::Network;
  session_ = makeUniqueNoThrow<ftp::Session>(transport_);
  if (!session_) {
    drop();
    return Error::LowMemory;
  }
  const ftp::Error e = session_->login(user_, password_);
  if (e != ftp::Error::Ok) {
    LOG_ERR("FTP", "Sign-in failed: %d (reply %d)", static_cast<int>(e), session_->lastReply().code);
    const Error mapped = map(e, false);
    drop();
    return mapped;
  }
  return Error::Ok;
}

void FtpClient::drop() {
  transport_.data.close();
  transport_.ctrl.close();
  session_.reset();
}

Error FtpClient::map(const ftp::Error error, const bool missingIsNotFound) const {
  switch (error) {
    case ftp::Error::Ok:
      return Error::Ok;
    case ftp::Error::Network:
      return Error::Network;
    case ftp::Error::Login:
      return Error::Auth;
    case ftp::Error::Refused: {
      const int code = session_ ? session_->lastReply().code : 0;
      LOG_ERR("FTP", "Refused: reply %d", code);
      if (code == 530) return Error::Auth;
      if (code == 550 && missingIsNotFound) return Error::NotFound;
      return Error::Refused;
    }
    case ftp::Error::Protocol:
      return Error::BadAnswer;
    case ftp::Error::Local:
      return Error::File;
    case ftp::Error::Aborted:
      return Error::Aborted;
    case ftp::Error::NoMemory:
      return Error::LowMemory;
    case ftp::Error::BadName:
    default:
      return Error::Refused;
  }
}

Error FtpClient::list(const std::string& dir, remotefiles::Listing& out, const Transfer& transfer) {
  const std::string path = remote(dir);
  const Error result = run(transfer, true, [&](ftp::Session& session) {
    out = remotefiles::Listing{};
    return session.list(path, out);
  });
  if (result == Error::Ok) remotefiles::sortEntries(out.entries);
  return result;
}

Error FtpClient::download(const std::string& dir, const std::string& name, const std::string& localPath,
                          const uint64_t expectedSize, const Transfer& transfer) {
  const std::string path = remote(dir);
  // Staged in <dest>.part: a failed or cancelled download never replaces a file that is there.
  const std::string partPath = localPath + ".part";
  bool fileError = false;
  const Error result = run(transfer, true, [&](ftp::Session& session) {
    Storage.remove(partPath.c_str());
    HalFile file;
    if (!Storage.openFileForWrite("FTP", partPath, file)) {
      fileError = true;
      return ftp::Error::Local;
    }
    size_t received = 0;
    const ftp::Error e = session.retrieve(path, name, [&](const uint8_t* data, const size_t len) {
      if (file.write(data, len) != len) return false;
      received += len;
      if (transfer.progress) transfer.progress(received, static_cast<size_t>(expectedSize));
      return true;
    });
    // Close before any remove() on the same path.
    if (file.isOpen()) file.close();
    return e;
  });
  if (result != Error::Ok) {
    if (!fileError) Storage.remove(partPath.c_str());
    return result;
  }
  if (!Storage.replaceFile(partPath.c_str(), localPath.c_str())) {
    Storage.remove(partPath.c_str());
    return Error::File;
  }
  return Error::Ok;
}

Error FtpClient::upload(const std::string& localPath, const std::string& dir, const std::string& name,
                        const Transfer& transfer) {
  const std::string path = remote(dir);
  return run(transfer, false, [&](ftp::Session& session) {
    HalFile file;
    if (!Storage.openFileForRead("FTP", localPath, file)) return ftp::Error::Local;
    if (file.isDirectory()) {
      file.close();
      return ftp::Error::Local;
    }
    const uint64_t size = file.fileSize64();
    uint64_t sent = 0;
    const ftp::Error e = session.store(path, name, [&](uint8_t* buf, const size_t len) {
      const int n = file.read(buf, len);
      if (n < 0) return -1;
      sent += static_cast<uint64_t>(n);
      if (transfer.progress) transfer.progress(static_cast<size_t>(sent), static_cast<size_t>(size));
      return n;
    });
    file.close();
    return e;
  });
}

Error FtpClient::makeDir(const std::string& dir, const std::string& name) {
  const std::string path = remote(dir);
  return run(Transfer{}, false, [&](ftp::Session& session) { return session.makeDir(path, name); });
}

void FtpClient::close() {
  if (session_) {
    transport_.setCancel(nullptr);
    session_->quit();
  }
  drop();
}

}  // namespace

std::unique_ptr<Client> makeFtpClient(const RemoteServer& server, Error& error) {
  ftp::Url url;
  if (!ftp::parseUrl(server.url, url)) {
    error = Error::BadAddress;
    return nullptr;
  }
  auto client = makeUniqueNoThrow<FtpClient>(std::move(url), server.username, server.password);
  error = client ? Error::Ok : Error::LowMemory;
  return client;
}

}  // namespace remotefs

#else  // SIMULATOR

#include "RemoteFsClient.h"

namespace remotefs {
std::unique_ptr<Client> makeFtpClient(const RemoteServer&, Error& error) {
  error = Error::Network;
  return nullptr;
}
}  // namespace remotefs

#endif  // SIMULATOR
