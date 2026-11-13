#pragma once

#include <SshKnownHosts.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// One SSH connection through the vendored wolfSSH: the terminal's shell or the SFTP file calls.
// No Arduino or ESP code in here: the bytes go through a Socket the caller supplies (WiFiClient on
// the device, a POSIX socket in the host test), so this file is built and run on the computer
// against a real sshd. Compiled only in builds with VAULTINK_SSH=1.
//
// Not thread safe: one task uses a Session. Every call but readShell() blocks until it is done,
// fails, times out or is cancelled.
namespace sshclient {

class Socket {
 public:
  virtual ~Socket() = default;
  // > 0: bytes read. 0: nothing there right now. < 0: closed or failed.
  virtual int read(uint8_t* buf, size_t len) = 0;
  // > 0: bytes taken. 0: cannot take any right now. < 0: closed or failed.
  virtual int write(const uint8_t* buf, size_t len) = 0;
  virtual uint32_t nowMs() = 0;
  // Called between tries while waiting for the other side (a short sleep).
  virtual void idle() = 0;
};

enum class Error : uint8_t {
  Ok,
  LowMemory,
  Network,         // no answer, a timeout, the connection dropped
  NoCommonCipher,  // the server and this build share no key exchange, host key type or cipher
  HostKeyUnknown,  // first connection: nothing is pinned for this host. hostKey() has the key to show
  HostKeyChanged,  // the pinned key is another one: hard stop
  Auth,            // wrong user name, password or key, or the sign-in method is not offered
  Refused,         // the server said no (no permission, read-only, a name it does not take)
  NotFound,
  Protocol,  // an answer that is not SSH / SFTP
  Aborted,   // stopped by the user
  NotConnected,
};

// The server's public key as presented in the key exchange.
struct HostKey {
  std::vector<uint8_t> blob;  // SSH wire form
  std::string type;           // "ssh-ed25519"
  std::string fingerprint;    // "SHA256:..."
  bool seen() const { return !blob.empty(); }
};

struct Login {
  std::string host;  // as typed: the name the host key is pinned under
  uint16_t port = 22;
  std::string user;
  // One of the two. The password is wiped by the Session once the sign-in is over.
  std::string password;
  bool useKey = false;
  uint8_t keySeed[32]{};  // the device key, when useKey
  uint8_t keyPublic[32]{};
  // The pinned keys (the known_hosts text, already verified by the caller: sshkh::verify). The
  // Session never changes the file: on HostKeyUnknown the caller asks the user and adds
  // hostKey().blob itself. Taken over by the Session (left empty).
  std::string knownHosts;
  // Checked while waiting for the server, during the key exchange and the sign-in too.
  const bool* cancel = nullptr;
  // The longest wait for one answer...
  uint32_t timeoutMs = 20000;
  // ...and for the key exchange and the sign-in as a whole, however the server spaces its bytes.
  uint32_t deadlineMs = 30000;
};

struct DirEntry {
  std::string name;
  uint64_t size = 0;
  int64_t mtime = 0;
  uint32_t mode = 0;  // 0 when the server sent no permissions
};

class Session {
 public:
  explicit Session(Socket& socket);
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Key exchange, host key check, sign-in, then a shell on a pty of cols x rows.
  Error openShell(Login& login, uint16_t cols, uint16_t rows);
  // The same, then the SFTP subsystem.
  Error openSftp(Login& login);
  bool connected() const { return ssh_ != nullptr && open_; }
  const HostKey& hostKey() const { return hostKey_; }
  // The last wolfSSH error number, for the log.
  int lastLibraryError() const { return lastError_; }
  // The session ended because a later key exchange showed another host key than the first one.
  bool hostKeyChangedMidSession() const { return keyCheck_.signedIn && keyCheck_.verdict == Error::HostKeyChanged; }
  // Asks for new session keys now (servers and wolfSSH do it by themselves after a time or a
  // byte count). The exchange runs inside the next reads. For the live test of the rekey rule.
  Error rekey();

  // --- shell ---
  // Never waits: > 0 bytes of output, 0 nothing right now, < 0 the session is over.
  int readShell(uint8_t* buf, size_t len);
  // Waits at most shellWriteTimeoutMs for the server to take the bytes; past that the session is
  // closed (connected() turns false) and Network comes back.
  Error writeShell(const uint8_t* data, size_t len);
  void setShellWriteTimeout(uint32_t ms) { shellWriteTimeoutMs_ = ms; }
  Error resizeShell(uint16_t cols, uint16_t rows);

  // --- SFTP ---
  // The folder the server opens for this user (its answer to realpath "."), absolute.
  Error sftpHome(std::string& out);
  // Calls `visit` for every entry but "." and ".."; `visit` returns false to stop early (a cap).
  using Visit = bool (*)(void* ctx, const DirEntry& entry);
  Error sftpList(const std::string& path, Visit visit, void* ctx);
  Error sftpStat(const std::string& path, DirEntry& out);
  // One file at a time.
  Error sftpOpenRead(const std::string& path);
  Error sftpOpenWrite(const std::string& path);  // created or cut to nothing
  // `got` is 0 at the end of the file.
  Error sftpRead(uint64_t offset, uint8_t* buf, size_t len, size_t& got);
  Error sftpWrite(uint64_t offset, const uint8_t* data, size_t len);
  Error sftpCloseFile();
  Error sftpMakeDir(const std::string& path);
  Error sftpRename(const std::string& from, const std::string& to);
  Error sftpRemove(const std::string& path);

  // For the next calls (a transfer's cancel flag); nullptr for none.
  void setCancel(const bool* cancel) { io_.cancel = cancel; }

  // Says goodbye when the connection is still good, and frees everything.
  void close();

  // What a file transfer should ask for per call.
  static constexpr size_t SFTP_CHUNK = 8192;

  struct Io {
    Socket* socket = nullptr;
    const bool* cancel = nullptr;
    uint32_t timeoutMs = 20000;
    // The whole-attempt deadline of connect(): 0 when none is running.
    uint32_t deadlineStart = 0;
    uint32_t deadlineMs = 0;
    bool blocking = true;
    bool aborted = false;
    bool timedOut = false;
    bool closed = false;
  };
  struct KeyCheck {
    sshkh::KeyGate* gate = nullptr;
    HostKey* seen = nullptr;
    Error verdict = Error::Ok;
    bool signedIn = false;  // the sign-in is over: only the key that passed then is accepted
  };
  struct AuthState {
    Login* login = nullptr;
    std::vector<uint8_t> publicBlob;
    uint8_t privateDer[48]{};
    int passwordAsked = 0;
    int keyAsked = 0;
  };

 private:
  Error connect(Login& login, bool sftp, uint16_t cols, uint16_t rows);
  Error fail(int libraryResult);
  Error sftpFail(int libraryResult, bool readRequest);
  void freeAll();

  Io io_;
  sshkh::KeyGate gate_;
  KeyCheck keyCheck_;
  uint32_t shellWriteTimeoutMs_ = 3000;
  AuthState auth_;
  HostKey hostKey_;
  void* ctx_ = nullptr;  // WOLFSSH_CTX*
  void* ssh_ = nullptr;  // WOLFSSH*
  bool open_ = false;
  bool sftp_ = false;
  int lastError_ = 0;
  uint8_t fileHandle_[256]{};
  uint32_t fileHandleLen_ = 0;
};

// --- the device key and the fingerprint (wolfCrypt) ---

// The MAC of the known_hosts file (sshkh::Mac): HMAC-SHA256 under SHA-256("ssh-known-hosts:" ||
// secret), `secret` being the device secret. Its own label, so it is not the key of the key file.
bool knownHostsMac(const uint8_t secret[32], const std::string& canonicalText, uint8_t out[32]);
// "SHA256:..." of a key blob. Empty on failure.
std::string fingerprintOf(const uint8_t* blob, size_t len);
// A new Ed25519 key from wolfCrypt's random generator.
bool generateKey(uint8_t seed[32], uint8_t publicKey[32]);
// The public key that belongs to a seed (used to check a key file after it is opened).
bool publicFromSeed(const uint8_t seed[32], uint8_t publicKey[32]);
// The key file of lib/Ssh/SshKey.h: sealed with AES-256-GCM under SHA-256("ssh-key:" || secret),
// `secret` being the device secret (lib/DeviceSecret). `file` is sshkey::FILE_BYTES long.
bool sealKeyFile(const uint8_t secret[32], const uint8_t seed[32], const uint8_t publicKey[32], uint8_t* file);
// False when the file is not a key file, was sealed on another device, or was changed.
bool openKeyFile(const uint8_t secret[32], const uint8_t* file, size_t len, uint8_t seed[32], uint8_t publicKey[32]);

}  // namespace sshclient
