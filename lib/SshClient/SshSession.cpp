// Compiled only where the build links wolfSSH ([vaultink_ssh] in platformio.ini).
#if defined(VAULTINK_SSH) && VAULTINK_SSH

#include "SshSession.h"

// clang-format off
// The wolfSSH options have to be seen before any wolfSSL or wolfSSH header.
#include <vaultink_wolfssh.h>
// clang-format on
#include <SshKey.h>
#include <SshKnownHosts.h>
#include <wolfssl/wolfcrypt/aes.h>
#include <wolfssl/wolfcrypt/ed25519.h>
#include <wolfssl/wolfcrypt/hash.h>
#include <wolfssl/wolfcrypt/random.h>

#include <cstring>
#include <memory>
#include <new>

namespace sshclient {

namespace {

// Host keys asked for, in this order. Ed25519 and ECDSA P-256 are cheap to check; RSA is last
// because its check needs the big-number temporaries (tens of KB of heap for a moment).
constexpr char HOST_KEY_ALGOS[] = "ssh-ed25519,ecdsa-sha2-nistp256,rsa-sha2-256";

WOLFSSH* sshOf(void* p) { return static_cast<WOLFSSH*>(p); }
WOLFSSH_CTX* ctxOf(void* p) { return static_cast<WOLFSSH_CTX*>(p); }

int ioRecv(WOLFSSH*, void* buf, word32 sz, void* ctx) {
  auto* io = static_cast<Session::Io*>(ctx);
  if (!io || !io->socket) return WS_CBIO_ERR_GENERAL;
  const uint32_t start = io->socket->nowMs();
  for (;;) {
    const int n = io->socket->read(static_cast<uint8_t*>(buf), sz);
    if (n > 0) return n;
    if (n < 0) {
      io->closed = true;
      return WS_CBIO_ERR_CONN_CLOSE;
    }
    if (!io->blocking) return WS_CBIO_ERR_WANT_READ;
    if (io->cancel && *io->cancel) {
      io->aborted = true;
      return WS_CBIO_ERR_GENERAL;
    }
    if (static_cast<uint32_t>(io->socket->nowMs() - start) >= io->timeoutMs) {
      io->timedOut = true;
      return WS_CBIO_ERR_TIMEOUT;
    }
    io->socket->idle();
  }
}

// Sending always waits (up to the timeout): what is sent is small next to the socket's buffer,
// and a half-sent packet would break the stream.
int ioSend(WOLFSSH*, void* buf, word32 sz, void* ctx) {
  auto* io = static_cast<Session::Io*>(ctx);
  if (!io || !io->socket) return WS_CBIO_ERR_GENERAL;
  const uint32_t start = io->socket->nowMs();
  for (;;) {
    const int n = io->socket->write(static_cast<const uint8_t*>(buf), sz);
    if (n > 0) return n;
    if (n < 0) {
      io->closed = true;
      return WS_CBIO_ERR_CONN_CLOSE;
    }
    if (io->cancel && *io->cancel) {
      io->aborted = true;
      return WS_CBIO_ERR_GENERAL;
    }
    if (static_cast<uint32_t>(io->socket->nowMs() - start) >= io->timeoutMs) {
      io->timedOut = true;
      return WS_CBIO_ERR_TIMEOUT;
    }
    io->socket->idle();
  }
}

// The one place a server's key is accepted: only the pinned key passes. Anything else stops the
// key exchange; the caller reads why from the verdict.
int hostKeyCheck(const byte* publicKey, word32 publicKeySz, void* ctx) {
  auto* check = static_cast<Session::KeyCheck*>(ctx);
  if (!check || !check->login || !check->seen || !publicKey || publicKeySz == 0) return -1;
  check->seen->blob.assign(publicKey, publicKey + publicKeySz);
  check->seen->type = sshkh::keyType(publicKey, publicKeySz);
  check->seen->fingerprint = fingerprintOf(publicKey, publicKeySz);
  if (check->seen->type.empty() || check->seen->fingerprint.empty()) {
    check->verdict = Error::Protocol;
    return -1;
  }
  switch (sshkh::check(check->login->knownHosts, check->login->host, check->login->port, publicKey, publicKeySz)) {
    case sshkh::Verdict::Match:
      check->verdict = Error::Ok;
      return 0;
    case sshkh::Verdict::Mismatch:
      check->verdict = Error::HostKeyChanged;
      return -1;
    case sshkh::Verdict::Unknown:
    default:
      check->verdict = Error::HostKeyUnknown;
      return -1;
  }
}

// wolfSSH asks for the password first, then for a key. Each is offered once: a second request
// means the server said no, and answering again would only burn the server's tries.
int userAuth(byte authType, WS_UserAuthData* authData, void* ctx) {
  auto* auth = static_cast<Session::AuthState*>(ctx);
  if (!auth || !auth->login || !authData) return WOLFSSH_USERAUTH_FAILURE;
  if (authType == WOLFSSH_USERAUTH_PASSWORD) {
    if (auth->login->useKey || auth->login->password.empty() || auth->passwordAsked++ > 0)
      return WOLFSSH_USERAUTH_FAILURE;
    authData->sf.password.password = reinterpret_cast<const byte*>(auth->login->password.data());
    authData->sf.password.passwordSz = static_cast<word32>(auth->login->password.size());
    return WOLFSSH_USERAUTH_SUCCESS;
  }
  if (authType == WOLFSSH_USERAUTH_PUBLICKEY) {
    // wolfSSH asks twice for one sign-in with a key: once to offer the key, once to sign.
    if (!auth->login->useKey || auth->keyAsked++ > 1) return WOLFSSH_USERAUTH_FAILURE;
    WS_UserAuthData_PublicKey& pk = authData->sf.publicKey;
    pk.publicKeyType = reinterpret_cast<const byte*>(sshkey::KEY_TYPE);
    pk.publicKeyTypeSz = sizeof(sshkey::KEY_TYPE) - 1;
    pk.publicKey = auth->publicBlob.data();
    pk.publicKeySz = static_cast<word32>(auth->publicBlob.size());
    pk.privateKey = auth->privateDer;
    pk.privateKeySz = sizeof(auth->privateDer);
    return WOLFSSH_USERAUTH_SUCCESS;
  }
  return WOLFSSH_USERAUTH_FAILURE;
}

void splitOffset(const uint64_t offset, word32 out[2]) {
  out[0] = static_cast<word32>(offset & 0xFFFFFFFFu);
  out[1] = static_cast<word32>(offset >> 32);
}

bool initLibrary() {
  static bool done = false;
  if (!done) done = wolfSSH_Init() == WS_SUCCESS;
  return done;
}

}  // namespace

Session::Session(Socket& socket) { io_.socket = &socket; }

Session::~Session() { freeAll(); }

void Session::freeAll() {
  if (ssh_) wolfSSH_free(sshOf(ssh_));
  if (ctx_) wolfSSH_CTX_free(ctxOf(ctx_));
  ssh_ = nullptr;
  ctx_ = nullptr;
  open_ = false;
  sftp_ = false;
  fileHandleLen_ = 0;
  sshkey::wipe(auth_.privateDer, sizeof(auth_.privateDer));
  auth_.login = nullptr;
  keyCheck_.login = nullptr;
}

Error Session::fail(const int libraryResult) {
  lastError_ = ssh_ ? wolfSSH_get_error(sshOf(ssh_)) : libraryResult;
  if (lastError_ == 0) lastError_ = libraryResult;
  if (io_.aborted) return Error::Aborted;
  if (keyCheck_.verdict != Error::Ok) return keyCheck_.verdict;
  if (io_.timedOut || io_.closed) return Error::Network;
  // The host key was the pinned one and a password or key was offered: the server said no to it.
  // (wolfSSH reports that with different numbers: WS_USER_AUTH_E, WS_INVALID_ALGO_ID, WS_ERROR.)
  if (hostKey_.seen() && (auth_.passwordAsked > 0 || auth_.keyAsked > 0) && lastError_ != WS_MEMORY_E)
    return Error::Auth;
  switch (lastError_) {
    case WS_MEMORY_E:
      return Error::LowMemory;
    case WS_MATCH_KEX_ALGO_E:
    case WS_MATCH_KEY_ALGO_E:
    case WS_MATCH_ENC_ALGO_E:
    case WS_MATCH_MAC_ALGO_E:
      return Error::NoCommonCipher;
    case WS_USER_AUTH_E:
    case WS_INVALID_USERNAME:
    case WS_CHANGE_AUTH_E:
    case WS_BAD_USAGE:  // no sign-in method left that the callback will answer
      return Error::Auth;
    case WS_SOCKET_ERROR_E:
    case WS_EOF:
    case WS_DISCONNECT:
    case WS_CHANNEL_CLOSED:
      return Error::Network;
    case WS_CHANOPEN_FAILED:
    case WS_CHANNEL_NOT_CONF:
      return Error::Refused;
    default:
      return Error::Protocol;
  }
}

Error Session::connect(Login& login, const bool sftp, const uint16_t cols, const uint16_t rows) {
  freeAll();
  hostKey_ = HostKey{};
  lastError_ = 0;
  io_.cancel = login.cancel;
  io_.timeoutMs = login.timeoutMs;
  io_.blocking = true;
  io_.aborted = io_.timedOut = io_.closed = false;
  keyCheck_ = KeyCheck{&login, &hostKey_, Error::Ok};
  auth_.login = &login;
  auth_.passwordAsked = auth_.keyAsked = 0;
  auth_.publicBlob.clear();
  if (login.useKey) {
    auth_.publicBlob = sshkey::publicBlob(login.keyPublic);
    sshkey::privateDer(login.keySeed, auth_.privateDer);
  }

  Error result = Error::Ok;
  int ret = WS_SUCCESS;
  if (!initLibrary()) {
    result = Error::LowMemory;
  } else {
    WOLFSSH_CTX* ctx = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_CLIENT, nullptr);
    ctx_ = ctx;
    if (!ctx) {
      result = Error::LowMemory;
    } else {
      wolfSSH_SetIORecv(ctx, ioRecv);
      wolfSSH_SetIOSend(ctx, ioSend);
      wolfSSH_SetUserAuth(ctx, userAuth);
      wolfSSH_CTX_SetPublicKeyCheck(ctx, hostKeyCheck);
      ret = wolfSSH_CTX_SetAlgoListKey(ctx, HOST_KEY_ALGOS);
      WOLFSSH* ssh = ret == WS_SUCCESS ? wolfSSH_new(ctx) : nullptr;
      ssh_ = ssh;
      if (!ssh) {
        result = ret == WS_SUCCESS ? Error::LowMemory : Error::Protocol;
      } else {
        wolfSSH_SetIOReadCtx(ssh, &io_);
        wolfSSH_SetIOWriteCtx(ssh, &io_);
        wolfSSH_SetUserAuthCtx(ssh, &auth_);
        wolfSSH_SetPublicKeyCheckCtx(ssh, &keyCheck_);
        ret = wolfSSH_SetUsername(ssh, login.user.c_str());
        if (ret == WS_SUCCESS && !sftp) ret = wolfSSH_SetChannelType(ssh, WOLFSSH_SESSION_TERMINAL, nullptr, 0);
        if (ret == WS_SUCCESS) ret = sftp ? wolfSSH_SFTP_connect(ssh) : wolfSSH_connect(ssh);
        if (ret != WS_SUCCESS) result = fail(ret);
      }
    }
  }

  // The secrets are not needed past the sign-in, whichever way it went.
  sshkey::wipe(login.password.data(), login.password.size());
  login.password.clear();
  sshkey::wipe(login.keySeed, sizeof(login.keySeed));
  sshkey::wipe(auth_.privateDer, sizeof(auth_.privateDer));
  auth_.login = nullptr;
  keyCheck_.login = nullptr;
  io_.cancel = nullptr;

  if (result != Error::Ok) {
    const int keep = lastError_;
    freeAll();
    lastError_ = keep;
    return result;
  }
  open_ = true;
  sftp_ = sftp;
  if (!sftp && cols > 0 && rows > 0) resizeShell(cols, rows);
  return Error::Ok;
}

Error Session::openShell(Login& login, const uint16_t cols, const uint16_t rows) {
  return connect(login, false, cols, rows);
}

Error Session::openSftp(Login& login) { return connect(login, true, 0, 0); }

int Session::readShell(uint8_t* buf, const size_t len) {
  if (!connected() || sftp_) return -1;
  io_.blocking = false;
  int n = wolfSSH_stream_read(sshOf(ssh_), buf, static_cast<word32>(len));
  int error = n > 0 ? 0 : wolfSSH_get_error(sshOf(ssh_));
  if (n <= 0 && (n == WS_EXTDATA || error == WS_EXTDATA)) {
    // What the remote program wrote to stderr: shown like the rest.
    n = wolfSSH_extended_data_read(sshOf(ssh_), buf, static_cast<word32>(len));
    error = n > 0 ? 0 : wolfSSH_get_error(sshOf(ssh_));
  }
  io_.blocking = true;
  if (n > 0) return n;
  if (io_.closed || io_.timedOut) {
    lastError_ = error;
    open_ = false;
    return -1;
  }
  switch (error) {
    case WS_WANT_READ:
    case WS_WANT_WRITE:
    case WS_REKEYING:
    case WS_CHAN_RXD:
    case WS_WINDOW_FULL:
    case WS_EXTDATA:
    case WS_SUCCESS:
      return 0;
    default:
      lastError_ = error;
      open_ = false;
      return -1;
  }
}

Error Session::writeShell(const uint8_t* data, size_t len) {
  if (!connected() || sftp_) return Error::NotConnected;
  io_.blocking = true;
  io_.aborted = io_.timedOut = false;
  int stalls = 0;
  while (len > 0) {
    const int n = wolfSSH_stream_send(sshOf(ssh_), const_cast<byte*>(data), static_cast<word32>(len));
    if (n > 0) {
      data += n;
      len -= static_cast<size_t>(n);
      stalls = 0;
      continue;
    }
    const int error = wolfSSH_get_error(sshOf(ssh_));
    // The server has to read first (its window is full) or new keys are being agreed: the caller's
    // next readShell() moves that along. A few short waits, then give up on these bytes.
    if ((error == WS_WINDOW_FULL || error == WS_REKEYING || error == WS_WANT_WRITE) && ++stalls < 50) {
      io_.socket->idle();
      continue;
    }
    open_ = false;
    return fail(n);
  }
  return Error::Ok;
}

Error Session::resizeShell(const uint16_t cols, const uint16_t rows) {
  if (!connected() || sftp_) return Error::NotConnected;
  const int ret = wolfSSH_ChangeTerminalSize(sshOf(ssh_), cols, rows, 0, 0);
  if (ret == WS_SUCCESS) return Error::Ok;
  lastError_ = ret;
  return Error::Refused;
}

// wolfSSH folds the server's status answers (no such file, failure, ...) into a few return codes
// and does not always set the session error, so the result is read from both. When the connection
// itself is fine, a failed request is an answer: "not there" for a request that only reads,
// "refused" for one that changes something. Permission denied is always "refused".
Error Session::sftpFail(const int libraryResult, const bool readRequest) {
  const int sessionError = ssh_ ? wolfSSH_get_error(sshOf(ssh_)) : 0;
  lastError_ = libraryResult < 0 ? libraryResult : sessionError;
  if (io_.aborted) {
    open_ = false;
    return Error::Aborted;
  }
  if (io_.timedOut || io_.closed) {
    open_ = false;
    return Error::Network;
  }
  for (const int code : {libraryResult, sessionError}) {
    switch (code) {
      case WS_PERMISSIONS:
        return Error::Refused;
      case WS_SFTP_FILE_DNE:
        return Error::NotFound;
      case WS_MEMORY_E:
        return Error::LowMemory;
      case WS_SOCKET_ERROR_E:
      case WS_EOF:
      case WS_DISCONNECT:
      case WS_CHANNEL_CLOSED:
        open_ = false;
        return Error::Network;
      default:
        break;
    }
  }
  return readRequest ? Error::NotFound : Error::Refused;
}

Error Session::sftpHome(std::string& out) {
  if (!connected() || !sftp_) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  char dot[] = ".";
  WS_SFTPNAME* name = wolfSSH_SFTP_RealPath(sshOf(ssh_), dot);
  if (!name) return sftpFail(WS_ERROR, true);
  out.assign(name->fName ? name->fName : "", name->fName ? name->fSz : 0);
  wolfSSH_SFTPNAME_list_free(name);
  if (out.empty() || out[0] != '/') return Error::Protocol;
  return Error::Ok;
}

namespace {
void fillEntry(const WS_SFTP_FILEATRB& atr, DirEntry& entry) {
  if (atr.flags & WOLFSSH_FILEATRB_SIZE) entry.size = (static_cast<uint64_t>(atr.sz[1]) << 32) | atr.sz[0];
  if (atr.flags & WOLFSSH_FILEATRB_TIME) entry.mtime = atr.mtime;
  if (atr.flags & WOLFSSH_FILEATRB_PERM) entry.mode = atr.per;
}
}  // namespace

Error Session::sftpList(const std::string& path, const Visit visit, void* ctx) {
  if (!connected() || !sftp_) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  byte handle[WOLFSSH_MAX_HANDLE];
  word32 handleLen = sizeof(handle);
  int ret = VaultInk_SFTP_OpenDir(sshOf(ssh_), path.c_str(), handle, &handleLen);
  if (ret != WS_SUCCESS) return sftpFail(ret, true);

  Error result = Error::Ok;
  bool more = true;
  while (more) {
    WS_SFTPNAME* names = VaultInk_SFTP_ReadDir(sshOf(ssh_), handle, handleLen);
    if (!names) {
      // The end of the folder is a status answer too; only a broken connection is an error here.
      if (io_.aborted || io_.timedOut || io_.closed) result = sftpFail(WS_ERROR, true);
      break;
    }
    for (const WS_SFTPNAME* n = names; n && more; n = n->next) {
      if (!n->fName || n->fSz == 0) continue;
      DirEntry entry;
      entry.name.assign(n->fName, n->fSz);
      if (entry.name == "." || entry.name == "..") continue;
      fillEntry(n->atrb, entry);
      more = visit(ctx, entry);
    }
    wolfSSH_SFTPNAME_list_free(names);
  }
  if (connected()) {
    ret = wolfSSH_SFTP_Close(sshOf(ssh_), handle, handleLen);
    if (ret != WS_SUCCESS && result == Error::Ok) result = sftpFail(ret, true);
  }
  return result;
}

Error Session::sftpStat(const std::string& path, DirEntry& out) {
  if (!connected() || !sftp_) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  WS_SFTP_FILEATRB atr;
  memset(&atr, 0, sizeof(atr));
  std::string copy = path;
  const int ret = wolfSSH_SFTP_STAT(sshOf(ssh_), copy.data(), &atr);
  if (ret != WS_SUCCESS) return sftpFail(ret, true);
  fillEntry(atr, out);
  return Error::Ok;
}

Error Session::sftpOpenRead(const std::string& path) {
  if (!connected() || !sftp_) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  std::string copy = path;
  word32 len = sizeof(fileHandle_);
  fileHandleLen_ = 0;
  const int ret = wolfSSH_SFTP_Open(sshOf(ssh_), copy.data(), WOLFSSH_FXF_READ, nullptr, fileHandle_, &len);
  if (ret != WS_SUCCESS) return sftpFail(ret, true);
  fileHandleLen_ = len;
  return Error::Ok;
}

Error Session::sftpOpenWrite(const std::string& path) {
  if (!connected() || !sftp_) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  std::string copy = path;
  word32 len = sizeof(fileHandle_);
  fileHandleLen_ = 0;
  const int ret = wolfSSH_SFTP_Open(sshOf(ssh_), copy.data(), WOLFSSH_FXF_WRITE | WOLFSSH_FXF_CREAT | WOLFSSH_FXF_TRUNC,
                                    nullptr, fileHandle_, &len);
  if (ret != WS_SUCCESS) return sftpFail(ret, false);
  fileHandleLen_ = len;
  return Error::Ok;
}

Error Session::sftpRead(const uint64_t offset, uint8_t* buf, const size_t len, size_t& got) {
  got = 0;
  if (!connected() || !sftp_ || fileHandleLen_ == 0) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  word32 ofst[2];
  splitOffset(offset, ofst);
  const word32 want = static_cast<word32>(len > SFTP_CHUNK ? SFTP_CHUNK : len);
  const int n = wolfSSH_SFTP_SendReadPacket(sshOf(ssh_), fileHandle_, fileHandleLen_, ofst, buf, want);
  if (n > 0) {
    got = static_cast<size_t>(n);
    return Error::Ok;
  }
  // The end of the file comes as a status answer (EOF), which is not a failure of the connection.
  if (!io_.aborted && !io_.timedOut && !io_.closed) {
    const int error = wolfSSH_get_error(sshOf(ssh_));
    if (n == 0 || error == WS_EOF || error == WS_SUCCESS || error == WS_SFTP_STATUS_NOT_OK) return Error::Ok;
  }
  return sftpFail(n, true);
}

Error Session::sftpWrite(uint64_t offset, const uint8_t* data, size_t len) {
  if (!connected() || !sftp_ || fileHandleLen_ == 0) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  while (len > 0) {
    word32 ofst[2];
    splitOffset(offset, ofst);
    const word32 want = static_cast<word32>(len > SFTP_CHUNK ? SFTP_CHUNK : len);
    const int n =
        wolfSSH_SFTP_SendWritePacket(sshOf(ssh_), fileHandle_, fileHandleLen_, ofst, const_cast<byte*>(data), want);
    if (n <= 0) return sftpFail(n, false);
    data += n;
    offset += static_cast<uint64_t>(n);
    len -= static_cast<size_t>(n);
  }
  return Error::Ok;
}

Error Session::sftpCloseFile() {
  if (fileHandleLen_ == 0) return Error::Ok;
  const uint32_t len = fileHandleLen_;
  fileHandleLen_ = 0;
  if (!connected() || !sftp_) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  const int ret = wolfSSH_SFTP_Close(sshOf(ssh_), fileHandle_, len);
  if (ret != WS_SUCCESS) return sftpFail(ret, false);
  return Error::Ok;
}

Error Session::sftpMakeDir(const std::string& path) {
  if (!connected() || !sftp_) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  WS_SFTP_FILEATRB atr;
  memset(&atr, 0, sizeof(atr));
  atr.flags = WOLFSSH_FILEATRB_PERM;
  atr.per = 0755;
  std::string copy = path;
  const int ret = wolfSSH_SFTP_MKDIR(sshOf(ssh_), copy.data(), &atr);
  if (ret != WS_SUCCESS) return sftpFail(ret, false);
  return Error::Ok;
}

Error Session::sftpRename(const std::string& from, const std::string& to) {
  if (!connected() || !sftp_) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  const int ret = wolfSSH_SFTP_Rename(sshOf(ssh_), from.c_str(), to.c_str());
  if (ret != WS_SUCCESS) return sftpFail(ret, false);
  return Error::Ok;
}

Error Session::sftpRemove(const std::string& path) {
  if (!connected() || !sftp_) return Error::NotConnected;
  io_.aborted = io_.timedOut = false;
  std::string copy = path;
  const int ret = wolfSSH_SFTP_Remove(sshOf(ssh_), copy.data());
  if (ret != WS_SUCCESS) return sftpFail(ret, false);
  return Error::Ok;
}

void Session::close() {
  if (ssh_ && open_ && !io_.closed && !io_.timedOut && !io_.aborted) {
    // A short goodbye: the server is not waited for longer than this.
    io_.cancel = nullptr;
    io_.timeoutMs = 1500;
    io_.blocking = true;
    wolfSSH_shutdown(sshOf(ssh_));
  }
  freeAll();
}

// ---- the device key and the fingerprint ----

std::string fingerprintOf(const uint8_t* blob, const size_t len) {
  uint8_t digest[WC_SHA256_DIGEST_SIZE];
  if (!blob || len == 0 || wc_Sha256Hash(blob, static_cast<word32>(len), digest) != 0) return {};
  return sshkh::fingerprint(digest);
}

namespace {
struct Rng {
  WC_RNG rng;
  bool ready = wc_InitRng(&rng) == 0;
  ~Rng() {
    if (ready) wc_FreeRng(&rng);
  }
};

struct EdKey {
  ed25519_key key;
  bool ready = wc_ed25519_init(&key) == 0;
  ~EdKey() {
    if (ready) wc_ed25519_free(&key);
  }
};

// wolfSSL's Aes carries the GCM tables (KBs): heap, not the task stack.
struct AesGcm {
  std::unique_ptr<Aes> aes{new (std::nothrow) Aes};
  bool ready = aes && wc_AesInit(aes.get(), nullptr, INVALID_DEVID) == 0;
  ~AesGcm() {
    if (ready) wc_AesFree(aes.get());
  }
};

// SHA-256("ssh-key:" || secret): its own label, so this key is no other use's key (the book-key
// wrap of lib/Epub/BookKey.cpp uses "book-key:").
bool sealKey(const uint8_t secret[32], uint8_t out[32]) {
  static constexpr char LABEL[] = "ssh-key:";
  uint8_t input[sizeof(LABEL) - 1 + 32];
  memcpy(input, LABEL, sizeof(LABEL) - 1);
  memcpy(input + sizeof(LABEL) - 1, secret, 32);
  const bool ok = wc_Sha256Hash(input, sizeof(input), out) == 0;
  sshkey::wipe(input, sizeof(input));
  return ok;
}
}  // namespace

bool generateKey(uint8_t seed[32], uint8_t publicKey[32]) {
  Rng rng;
  EdKey key;
  if (!rng.ready || !key.ready) return false;
  if (wc_ed25519_make_key(&rng.rng, ED25519_KEY_SIZE, &key.key) != 0) return false;
  word32 seedLen = 32;
  word32 publicLen = 32;
  return wc_ed25519_export_private_only(&key.key, seed, &seedLen) == 0 && seedLen == 32 &&
         wc_ed25519_export_public(&key.key, publicKey, &publicLen) == 0 && publicLen == 32;
}

bool publicFromSeed(const uint8_t seed[32], uint8_t publicKey[32]) {
  EdKey key;
  if (!key.ready) return false;
  return wc_ed25519_import_private_only(seed, 32, &key.key) == 0 &&
         wc_ed25519_make_public(&key.key, publicKey, 32) == 0;
}

bool sealKeyFile(const uint8_t secret[32], const uint8_t seed[32], const uint8_t publicKey[32], uint8_t* file) {
  uint8_t kek[32];
  uint8_t plain[sshkey::FILE_SEALED_BYTES];
  uint8_t* iv = nullptr;
  uint8_t* sealed = nullptr;
  uint8_t* tag = nullptr;
  sshkey::layoutFile(file, iv, sealed, tag);
  memcpy(plain, seed, 32);
  memcpy(plain + 32, publicKey, 32);
  Rng rng;
  AesGcm gcm;
  const bool ok = rng.ready && gcm.ready && sealKey(secret, kek) &&
                  wc_RNG_GenerateBlock(&rng.rng, iv, sshkey::FILE_IV_BYTES) == 0 &&
                  wc_AesGcmSetKey(gcm.aes.get(), kek, sizeof(kek)) == 0 &&
                  wc_AesGcmEncrypt(gcm.aes.get(), sealed, plain, sizeof(plain), iv, sshkey::FILE_IV_BYTES, tag,
                                   sshkey::FILE_TAG_BYTES, file, sshkey::FILE_HEADER_BYTES) == 0;
  sshkey::wipe(kek, sizeof(kek));
  sshkey::wipe(plain, sizeof(plain));
  if (!ok) sshkey::wipe(file, sshkey::FILE_BYTES);
  return ok;
}

bool openKeyFile(const uint8_t secret[32], const uint8_t* file, const size_t len, uint8_t seed[32],
                 uint8_t publicKey[32]) {
  sshkey::FileParts parts;
  if (!sshkey::parseFile(file, len, parts)) return false;
  uint8_t kek[32];
  uint8_t plain[sshkey::FILE_SEALED_BYTES];
  AesGcm gcm;
  bool ok = gcm.ready && sealKey(secret, kek) && wc_AesGcmSetKey(gcm.aes.get(), kek, sizeof(kek)) == 0 &&
            wc_AesGcmDecrypt(gcm.aes.get(), plain, parts.sealed, sizeof(plain), parts.iv, sshkey::FILE_IV_BYTES,
                             parts.tag, sshkey::FILE_TAG_BYTES, parts.header, sshkey::FILE_HEADER_BYTES) == 0;
  if (ok) {
    // The two halves must belong together, or signing would use a key the server never saw.
    uint8_t derived[32];
    ok = publicFromSeed(plain, derived) && memcmp(derived, plain + 32, 32) == 0;
  }
  if (ok) {
    memcpy(seed, plain, 32);
    memcpy(publicKey, plain + 32, 32);
  }
  sshkey::wipe(kek, sizeof(kek));
  sshkey::wipe(plain, sizeof(plain));
  return ok;
}

}  // namespace sshclient

#endif  // VAULTINK_SSH
