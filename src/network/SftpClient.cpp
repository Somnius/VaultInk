// SFTP as one more protocol of the remote file browser (docs/ssh.md). Only in a build that links
// wolfSSH (VAULTINK_SSH=1, the X4 Pro); elsewhere, and in the simulator, the server cannot be used.
#if defined(VAULTINK_SSH) && VAULTINK_SSH && !defined(SIMULATOR)

// clang-format off
// HalStorage.h brings in SdFat, whose macros have to be seen before the network headers.
#include <HalStorage.h>
#include "RemoteFsClient.h"
// clang-format on

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <SshAddress.h>
#include <SshKey.h>
#include <SshSession.h>

#include <utility>

#include "RemoteServerStore.h"
#include "SshDevice.h"
#include "SshWifiSocket.h"
#include "WifiPowerSaveGuard.h"

namespace remotefs {

namespace {

constexpr uint32_t TIMEOUT_MS = 20000;
// Symbolic links in a listing are looked up one by one to learn what they point at; a folder full
// of links must not turn into hundreds of round trips.
constexpr int MAX_LINK_LOOKUPS = 32;

Error map(const sshclient::Error error) {
  switch (error) {
    case sshclient::Error::Ok:
      return Error::Ok;
    case sshclient::Error::LowMemory:
      return Error::LowMemory;
    case sshclient::Error::NoCommonCipher:
      return Error::NoCipher;
    case sshclient::Error::HostKeyUnknown:
      return Error::HostKeyUnknown;
    case sshclient::Error::HostKeyChanged:
      return Error::HostKeyChanged;
    case sshclient::Error::Auth:
      return Error::Auth;
    case sshclient::Error::Refused:
      return Error::Refused;
    case sshclient::Error::NotFound:
      return Error::NotFound;
    case sshclient::Error::Protocol:
      return Error::BadAnswer;
    case sshclient::Error::Aborted:
      return Error::Aborted;
    case sshclient::Error::Network:
    case sshclient::Error::NotConnected:
    default:
      return Error::Network;
  }
}

class SftpClient final : public Client {
 public:
  SftpClient(sshaddr::Address address, std::string user, std::string password, const bool keyAuth)
      : address_(std::move(address)),
        user_(std::move(user)),
        password_(std::move(password)),
        keyAuth_(keyAuth),
        session_(socket_) {}
  ~SftpClient() override {
    drop();
    sshkey::wipe(password_.data(), password_.size());
  }

  Error list(const std::string& dir, remotefiles::Listing& out, const Transfer& transfer) override;
  Error download(const std::string& dir, const std::string& name, const std::string& localPath, uint64_t expectedSize,
                 const Transfer& transfer) override;
  Error upload(const std::string& localPath, const std::string& dir, const std::string& name,
               const Transfer& transfer) override;
  Error makeDir(const std::string& dir, const std::string& name) override;
  void close() override;
  bool hostKey(std::string& type, std::string& fingerprint) const override;
  bool trustHostKey() override;

 private:
  Error ensureSession(const bool* cancel);
  void drop();
  // The path on the server of `dir` (and `name` in it), under the start folder.
  bool remote(const std::string& dir, const std::string& name, std::string& out) const;

  // Runs `op` on an open session; a connection the server closed while the list sat on the screen
  // is opened again once.
  template <typename Op>
  Error run(const Transfer& transfer, Op&& op) {
    WifiPowerSaveGuard psGuard;
    Error result = Error::Ok;
    for (int attempt = 0; attempt < 2; ++attempt) {
      const bool reused = session_.connected();
      result = ensureSession(transfer.cancel);
      if (result != Error::Ok) break;
      session_.setCancel(transfer.cancel);
      const sshclient::Error e = op();
      session_.setCancel(nullptr);
      result = map(e);
      // A session that is no longer connected (a drop, a cancel in the middle of a request, an
      // answer that made no sense) is never used again.
      if (!session_.connected()) drop();
      if (e != sshclient::Error::Network || !reused || transfer.cancelled()) break;
    }
    return result;
  }

  sshaddr::Address address_;
  std::string user_;
  std::string password_;  // typed for this browsing session; wiped when the client goes
  bool keyAuth_;
  SshWifiSocket socket_;
  sshclient::Session session_;
  std::string base_;  // the start folder on the server, absolute; empty until the first connection
  // The key the server showed when it was refused (unknown or changed), for the question.
  sshclient::HostKey refusedKey_;
};

void SftpClient::drop() {
  session_.close();
  socket_.stop();
}

Error SftpClient::ensureSession(const bool* cancel) {
  if (session_.connected()) return Error::Ok;
  if (!sshdevice::heapAllowsConnection()) return Error::LowMemory;

  sshclient::Login login;
  login.host = address_.host;
  login.port = address_.port;
  login.user = user_;
  login.cancel = cancel;
  login.timeoutMs = TIMEOUT_MS;
  if (keyAuth_) {
    login.useKey = true;
    if (!sshdevice::loadKey(login.keySeed, login.keyPublic)) return Error::Auth;
  } else {
    login.password = password_;
  }
  login.knownHosts = sshdevice::knownHosts();

  LOG_INF("SFTP", "Connecting to %s", sshaddr::display(address_).c_str());
  if (!socket_.connect(address_.host, address_.port, TIMEOUT_MS)) {
    sshkey::wipe(login.keySeed, sizeof(login.keySeed));
    sshkey::wipe(login.password.data(), login.password.size());
    return Error::Network;
  }
  const sshclient::Error e = session_.openSftp(login);  // wipes the secrets in `login`
  if (e != sshclient::Error::Ok) {
    LOG_ERR("SFTP", "Connection failed: %d (wolfSSH %d)", static_cast<int>(e), session_.lastLibraryError());
    if (e == sshclient::Error::HostKeyUnknown || e == sshclient::Error::HostKeyChanged)
      refusedKey_ = session_.hostKey();
    drop();
    return map(e);
  }
  if (base_.empty()) {
    if (!address_.path.empty()) {
      base_ = address_.path;
    } else {
      const sshclient::Error home = session_.sftpHome(base_);
      if (home != sshclient::Error::Ok) {
        drop();
        return map(home);
      }
    }
  }
  return Error::Ok;
}

bool SftpClient::hostKey(std::string& type, std::string& fingerprint) const {
  if (!refusedKey_.seen()) return false;
  type = refusedKey_.type;
  fingerprint = refusedKey_.fingerprint;
  return true;
}

bool SftpClient::trustHostKey() {
  if (!refusedKey_.seen()) return false;
  // trustHost() pins only where nothing is pinned: a changed key cannot be accepted through here.
  const bool ok = sshdevice::trustHost(address_.host, address_.port, refusedKey_.blob);
  refusedKey_ = sshclient::HostKey{};
  return ok;
}

bool SftpClient::remote(const std::string& dir, const std::string& name, std::string& out) const {
  std::string folder;
  if (base_.empty() || !sshaddr::remotePath(base_, dir, folder)) return false;
  if (name.empty()) {
    out = folder;
    return true;
  }
  if (!remotefiles::safeName(name)) return false;
  out = remotefiles::joinPath(folder, name);
  return true;
}

Error SftpClient::list(const std::string& dir, remotefiles::Listing& out, const Transfer& transfer) {
  struct Collect {
    remotefiles::Listing* listing;
    std::vector<std::string> links;
  } collect{&out, {}};
  const Error result = run(transfer, [&]() {
    out = remotefiles::Listing{};
    collect.links.clear();
    std::string path;
    if (!remote(dir, "", path)) return sshclient::Error::Refused;
    const sshclient::Error e = session_.sftpList(
        path,
        [](void* ctx, const sshclient::DirEntry& entry) {
          auto* c = static_cast<Collect*>(ctx);
          remotefiles::Entry item;
          item.name = entry.name;
          item.size = entry.size;
          item.mtime = entry.mtime;
          if (sshaddr::isDirMode(entry.mode)) {
            item.isDir = true;
            item.size = 0;
          } else if (!sshaddr::isFileMode(entry.mode) && entry.mode != 0) {
            // A link is looked at afterwards; devices, sockets and pipes are not shown.
            if ((entry.mode & sshaddr::MODE_TYPE_MASK) == 0120000 && remotefiles::safeName(entry.name) &&
                c->links.size() < MAX_LINK_LOOKUPS)
              c->links.push_back(entry.name);
            return true;
          }
          c->listing->add(std::move(item));
          return !c->listing->full();
        },
        &collect);
    if (e != sshclient::Error::Ok) return e;
    for (const std::string& link : collect.links) {
      if (out.full()) break;
      std::string target;
      sshclient::DirEntry st;
      if (!remote(dir, link, target) || session_.sftpStat(target, st) != sshclient::Error::Ok) {
        if (!session_.connected()) return sshclient::Error::Network;
        continue;  // a link to nothing
      }
      remotefiles::Entry item;
      item.name = link;
      item.mtime = st.mtime;
      if (sshaddr::isDirMode(st.mode)) {
        item.isDir = true;
      } else if (sshaddr::isFileMode(st.mode)) {
        item.size = st.size;
      } else {
        continue;
      }
      out.add(std::move(item));
    }
    return sshclient::Error::Ok;
  });
  if (result == Error::Ok) remotefiles::sortEntries(out.entries);
  return result;
}

Error SftpClient::download(const std::string& dir, const std::string& name, const std::string& localPath,
                           const uint64_t expectedSize, const Transfer& transfer) {
  // Staged in <dest>.part: a failed or cancelled download never replaces a file that is there.
  const std::string partPath = localPath + ".part";
  bool fileError = false;
  const Error result = run(transfer, [&]() {
    std::string path;
    if (!remote(dir, name, path)) return sshclient::Error::Refused;
    const sshclient::Error opened = session_.sftpOpenRead(path);
    if (opened != sshclient::Error::Ok) return opened;

    Storage.remove(partPath.c_str());
    HalFile file;
    if (!Storage.openFileForWrite("SFTP", partPath, file)) {
      fileError = true;
      session_.sftpCloseFile();
      return sshclient::Error::Ok;
    }
    auto buffer = makeUniqueNoThrow<uint8_t[]>(sshclient::Session::SFTP_CHUNK);
    sshclient::Error e = buffer ? sshclient::Error::Ok : sshclient::Error::LowMemory;
    uint64_t received = 0;
    while (e == sshclient::Error::Ok) {
      size_t got = 0;
      e = session_.sftpRead(received, buffer.get(), sshclient::Session::SFTP_CHUNK, got);
      if (e != sshclient::Error::Ok || got == 0) break;
      if (file.write(buffer.get(), got) != got) {
        fileError = true;
        break;
      }
      received += got;
      if (transfer.progress) transfer.progress(static_cast<size_t>(received), static_cast<size_t>(expectedSize));
      if (transfer.cancelled()) e = sshclient::Error::Aborted;
    }
    // Close before any remove() on the same path.
    if (file.isOpen()) file.close();
    if (session_.connected()) {
      const sshclient::Error closed = session_.sftpCloseFile();
      if (e == sshclient::Error::Ok && closed != sshclient::Error::Ok && !session_.connected()) e = closed;
    }
    // Shorter than the listing said: the connection ended early without an error from the server.
    if (e == sshclient::Error::Ok && !fileError && expectedSize > 0 && received < expectedSize)
      e = sshclient::Error::Network;
    return e;
  });
  if (fileError) {
    Storage.remove(partPath.c_str());
    return Error::File;
  }
  if (result != Error::Ok) {
    Storage.remove(partPath.c_str());
    return result;
  }
  if (!Storage.replaceFile(partPath.c_str(), localPath.c_str())) {
    Storage.remove(partPath.c_str());
    return Error::File;
  }
  return Error::Ok;
}

Error SftpClient::upload(const std::string& localPath, const std::string& dir, const std::string& name,
                         const Transfer& transfer) {
  bool fileError = false;
  const Error result = run(transfer, [&]() {
    std::string path;
    if (!remote(dir, name, path)) return sshclient::Error::Refused;
    HalFile file;
    if (!Storage.openFileForRead("SFTP", localPath, file) || file.isDirectory()) {
      if (file.isOpen()) file.close();
      fileError = true;
      return sshclient::Error::Ok;
    }
    const uint64_t size = file.fileSize64();
    auto buffer = makeUniqueNoThrow<uint8_t[]>(sshclient::Session::SFTP_CHUNK);
    if (!buffer) {
      file.close();
      return sshclient::Error::LowMemory;
    }
    sshclient::Error e = session_.sftpOpenWrite(path);
    uint64_t sent = 0;
    while (e == sshclient::Error::Ok) {
      const int n = file.read(buffer.get(), sshclient::Session::SFTP_CHUNK);
      if (n < 0) {
        fileError = true;
        break;
      }
      if (n == 0) break;
      e = session_.sftpWrite(sent, buffer.get(), static_cast<size_t>(n));
      if (e != sshclient::Error::Ok) break;
      sent += static_cast<uint64_t>(n);
      if (transfer.progress) transfer.progress(static_cast<size_t>(sent), static_cast<size_t>(size));
      if (transfer.cancelled()) e = sshclient::Error::Aborted;
    }
    file.close();
    if (session_.connected()) {
      const sshclient::Error closed = session_.sftpCloseFile();
      if (e == sshclient::Error::Ok && closed != sshclient::Error::Ok) e = closed;
    }
    return e;
  });
  if (fileError) return Error::File;
  return result;
}

Error SftpClient::makeDir(const std::string& dir, const std::string& name) {
  return run(Transfer{}, [&]() {
    std::string path;
    if (!remote(dir, name, path)) return sshclient::Error::Refused;
    return session_.sftpMakeDir(path);
  });
}

void SftpClient::close() { drop(); }

}  // namespace

std::unique_ptr<Client> makeSftpClient(const RemoteServer& server, Error& error) {
  sshaddr::Address address;
  if (!sshaddr::parse(server.url, address) || server.username.empty()) {
    error = Error::BadAddress;
    return nullptr;
  }
  if (!server.keyAuth && server.password.empty()) {
    error = Error::Auth;
    return nullptr;
  }
  auto client = makeUniqueNoThrow<SftpClient>(std::move(address), server.username,
                                              server.keyAuth ? std::string() : server.password, server.keyAuth);
  error = client ? Error::Ok : Error::LowMemory;
  return client;
}

}  // namespace remotefs

#else  // no SSH in this build

#include "RemoteFsClient.h"

namespace remotefs {
std::unique_ptr<Client> makeSftpClient(const RemoteServer&, Error& error) {
  error = Error::Unavailable;
  return nullptr;
}
}  // namespace remotefs

#endif
