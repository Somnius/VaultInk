// The SSH client of the firmware (lib/SshClient on the vendored wolfSSH and the wolfCrypt sources
// PlatformIO downloaded) built for the computer and run against a real OpenSSH server on
// 127.0.0.1. Not part of the CMake test tree: it needs sshd and the .pio folder. Run it with
// test/ssh_live/run.sh, which starts a throwaway sshd as the current user.
//
//   ssh_live_test <port> <user> <work dir> <known host fingerprint file> [host key type]
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "SshKey.h"
#include "SshKnownHosts.h"
#include "SshSession.h"
#include "SshTerminal.h"

using sshclient::Error;

namespace {

int failures = 0;
#define CHECK(cond)                                            \
  do {                                                         \
    if (!(cond)) {                                             \
      ++failures;                                              \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                          \
  } while (0)

class PosixSocket final : public sshclient::Socket {
 public:
  ~PosixSocket() override { closeNow(); }
  bool connectTo(const uint16_t port) {
    closeNow();
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
    return true;
  }
  void closeNow() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
  }
  int read(uint8_t* buf, const size_t len) override {
    const ssize_t n = recv(fd_, buf, len, 0);
    if (n > 0) return static_cast<int>(n);
    if (n == 0) return -1;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
  }
  int write(const uint8_t* buf, const size_t len) override {
    const ssize_t n = send(fd_, buf, len, MSG_NOSIGNAL);
    if (n > 0) return static_cast<int>(n);
    return (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) ? 0 : -1;
  }
  uint32_t nowMs() override {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
  }
  void idle() override {
    pollfd p{fd_, POLLIN, 0};
    poll(&p, 1, 5);
  }

 private:
  int fd_ = -1;
};

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

struct Setup {
  uint16_t port;
  std::string user;
  std::string dir;
  uint8_t seed[32];
  uint8_t pub[32];
  std::string knownHosts;
};

sshclient::Login keyLogin(const Setup& s) {
  sshclient::Login login;
  login.host = "127.0.0.1";
  login.port = s.port;
  login.user = s.user;
  login.useKey = true;
  memcpy(login.keySeed, s.seed, 32);
  memcpy(login.keyPublic, s.pub, 32);
  login.knownHosts = s.knownHosts;
  login.timeoutMs = 10000;
  return login;
}

void* termAlloc(size_t n) { return malloc(n); }
void termFree(void* p) { free(p); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 5) {
    fprintf(stderr, "usage: %s <port> <user> <work dir> <fingerprint file> [host key type]\n", argv[0]);
    return 2;
  }
  Setup s{};
  s.port = static_cast<uint16_t>(atoi(argv[1]));
  s.user = argv[2];
  s.dir = argv[3];
  const std::string wantFingerprint = readFile(argv[4]);

  printf("1. device key: generate, seal, open\n");
  CHECK(sshclient::generateKey(s.seed, s.pub));
  uint8_t derived[32];
  CHECK(sshclient::publicFromSeed(s.seed, derived) && memcmp(derived, s.pub, 32) == 0);
  uint8_t secret[32];
  memset(secret, 0x5A, sizeof(secret));
  uint8_t file[sshkey::FILE_BYTES];
  CHECK(sshclient::sealKeyFile(secret, s.seed, s.pub, file));
  uint8_t seed2[32];
  uint8_t pub2[32];
  CHECK(sshclient::openKeyFile(secret, file, sizeof(file), seed2, pub2));
  CHECK(memcmp(seed2, s.seed, 32) == 0 && memcmp(pub2, s.pub, 32) == 0);
  CHECK(memmem(file, sizeof(file), s.seed, 32) == nullptr);  // the seed is not in the file as it is
  uint8_t other[32];
  memset(other, 0x11, sizeof(other));
  CHECK(!sshclient::openKeyFile(other, file, sizeof(file), seed2, pub2));  // another device
  for (const size_t at : {size_t{0}, size_t{4}, size_t{6}, size_t{20}, size_t{90}}) {
    uint8_t bad[sshkey::FILE_BYTES];
    memcpy(bad, file, sizeof(bad));
    bad[at] ^= 1;
    CHECK(!sshclient::openKeyFile(secret, bad, sizeof(bad), seed2, pub2));  // any changed byte
  }
  CHECK(!sshclient::openKeyFile(secret, file, sizeof(file) - 1, seed2, pub2));
  // The line for authorized_keys: the test's sshd is started with exactly this line.
  {
    std::ofstream out(s.dir + "/authorized_keys");
    out << sshkey::authorizedKeysLine(s.pub, "vaultink-test") << "\n";
  }
  chmod((s.dir + "/authorized_keys").c_str(), 0600);

  PosixSocket sock;

  if (argc > 5) {
    // Another kind of host key (run.sh starts sshd again with an ECDSA key, then an RSA key): the
    // key is shown with the fingerprint OpenSSH prints, and once pinned the connection is made.
    const std::string wantType = argv[5];
    printf("host key type %s\n", wantType.c_str());
    CHECK(sock.connectTo(s.port));
    sshclient::Session session(sock);
    sshclient::Login login = keyLogin(s);
    CHECK(session.openSftp(login) == Error::HostKeyUnknown);
    printf("   server key %s %s\n", session.hostKey().type.c_str(), session.hostKey().fingerprint.c_str());
    CHECK(session.hostKey().type == wantType);
    CHECK(wantFingerprint.find(session.hostKey().fingerprint) != std::string::npos);
    CHECK(sshkh::add(s.knownHosts, "127.0.0.1", s.port, session.hostKey().blob.data(), session.hostKey().blob.size()));
    CHECK(sock.connectTo(s.port));
    login = keyLogin(s);
    const Error e = session.openSftp(login);
    printf("   openSftp -> %d (library %d)\n", static_cast<int>(e), session.lastLibraryError());
    CHECK(e == Error::Ok);
    std::string home;
    CHECK(e == Error::Ok && session.sftpHome(home) == Error::Ok);
    session.close();
    printf(failures == 0 ? "ALL OK\n" : "%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
  }

  printf("2. first connection: host key unknown, nothing sent after the key exchange\n");
  CHECK(sock.connectTo(s.port));
  sshclient::Session session(sock);
  sshclient::Login login = keyLogin(s);
  Error e = session.openSftp(login);
  CHECK(e == Error::HostKeyUnknown);
  CHECK(session.hostKey().seen());
  CHECK(session.hostKey().type == "ssh-ed25519");
  printf("   server key %s %s\n", session.hostKey().type.c_str(), session.hostKey().fingerprint.c_str());
  printf("   ssh-keygen  %s\n", wantFingerprint.c_str());
  CHECK(wantFingerprint.find(session.hostKey().fingerprint) != std::string::npos);
  CHECK(!session.connected());
  CHECK(login.password.empty());
  uint8_t zero[32] = {};
  CHECK(memcmp(login.keySeed, zero, 32) == 0);  // wiped after the attempt
  CHECK(sshkh::add(s.knownHosts, "127.0.0.1", s.port, session.hostKey().blob.data(), session.hostKey().blob.size()));

  printf("3. changed host key: hard stop\n");
  {
    Setup changed = s;
    changed.knownHosts.clear();
    uint8_t fake[32];
    memset(fake, 7, sizeof(fake));
    const std::vector<uint8_t> blob = sshkey::publicBlob(fake);
    CHECK(sshkh::add(changed.knownHosts, "127.0.0.1", s.port, blob.data(), blob.size()));
    CHECK(sock.connectTo(s.port));
    login = keyLogin(changed);
    CHECK(session.openSftp(login) == Error::HostKeyChanged);
    CHECK(!session.connected());
  }

  printf("4. wrong key and wrong password: refused, each offered once\n");
  {
    Setup wrong = s;
    CHECK(sshclient::generateKey(wrong.seed, wrong.pub));
    CHECK(sock.connectTo(s.port));
    login = keyLogin(wrong);
    e = session.openSftp(login);
    printf("   wrong key -> %d (library %d)\n", static_cast<int>(e), session.lastLibraryError());
    CHECK(e == Error::Auth);
    CHECK(sock.connectTo(s.port));
    login = keyLogin(s);
    login.useKey = false;
    login.password = "not-the-password";
    e = session.openSftp(login);
    printf("   wrong password -> %d (library %d)\n", static_cast<int>(e), session.lastLibraryError());
    CHECK(e == Error::Auth);
    CHECK(login.password.empty());
  }

  printf("5. SFTP with the device key: home, list, mkdir, upload, download, rename\n");
  CHECK(sock.connectTo(s.port));
  login = keyLogin(s);
  e = session.openSftp(login);
  printf("   openSftp -> %d (library %d)\n", static_cast<int>(e), session.lastLibraryError());
  CHECK(e == Error::Ok);
  if (e == Error::Ok) {
    std::string home;
    CHECK(session.sftpHome(home) == Error::Ok);
    printf("   home %s\n", home.c_str());
    CHECK(!home.empty() && home[0] == '/');
    const std::string base = s.dir + "/remote";
    e = session.sftpMakeDir(base);
    printf("   mkdir -> %d (library %d)\n", static_cast<int>(e), session.lastLibraryError());
    CHECK(e == Error::Ok);
    CHECK(session.sftpMakeDir(base) == Error::Refused);  // it is there already
    CHECK(session.sftpMakeDir(base + "/\xCE\xB2\xCE\xB9\xCE\xB2\xCE\xBB\xCE\xAF\xCE\xB1") == Error::Ok);

    // Upload: sizes around the chunk size, and one of several chunks.
    const size_t sizes[] = {0, 1, 8191, 8192, 8193, 100000};
    for (const size_t size : sizes) {
      std::vector<uint8_t> data(size);
      for (size_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>((i * 31 + size) & 0xFF);
      const std::string part = base + "/f" + std::to_string(size) + ".bin.part";
      const std::string path = base + "/f" + std::to_string(size) + ".bin";
      CHECK(session.sftpOpenWrite(part) == Error::Ok);
      size_t sent = 0;
      while (sent < size) {
        const size_t n = std::min(size - sent, sshclient::Session::SFTP_CHUNK);
        CHECK(session.sftpWrite(sent, data.data() + sent, n) == Error::Ok);
        sent += n;
      }
      CHECK(session.sftpCloseFile() == Error::Ok);
      CHECK(session.sftpRename(part, path) == Error::Ok);
      CHECK(readFile(path) == std::string(data.begin(), data.end()));  // what the server's disk holds

      sshclient::DirEntry st;
      CHECK(session.sftpStat(path, st) == Error::Ok);
      CHECK(st.size == size);
      CHECK((st.mode & 0170000) == 0100000);

      std::vector<uint8_t> back;
      CHECK(session.sftpOpenRead(path) == Error::Ok);
      uint8_t buf[sshclient::Session::SFTP_CHUNK];
      for (;;) {
        size_t got = 0;
        const Error re = session.sftpRead(back.size(), buf, sizeof(buf), got);
        CHECK(re == Error::Ok);
        if (re != Error::Ok || got == 0) break;
        back.insert(back.end(), buf, buf + got);
      }
      CHECK(session.sftpCloseFile() == Error::Ok);
      CHECK(back == data);
    }

    struct Seen {
      std::vector<sshclient::DirEntry> entries;
      size_t cap;
    } seen{{}, 1000};
    const auto visit = [](void* ctx, const sshclient::DirEntry& entry) {
      auto* list = static_cast<Seen*>(ctx);
      list->entries.push_back(entry);
      return list->entries.size() < list->cap;
    };
    CHECK(session.sftpList(base, visit, &seen) == Error::Ok);
    CHECK(seen.entries.size() == 7);  // six files and the Greek folder; no "." or ".."
    bool sawDir = false;
    bool sawBig = false;
    for (const auto& entry : seen.entries) {
      if (entry.name == "\xCE\xB2\xCE\xB9\xCE\xB2\xCE\xBB\xCE\xAF\xCE\xB1") sawDir = (entry.mode & 0170000) == 0040000;
      if (entry.name == "f100000.bin") sawBig = entry.size == 100000 && entry.mtime > 1600000000;
      CHECK(entry.name != "." && entry.name != "..");
    }
    CHECK(sawDir);
    CHECK(sawBig);

    // A folder with more entries than one answer holds, and stopping at a cap.
    const std::string many = base + "/many";
    CHECK(session.sftpMakeDir(many) == Error::Ok);
    for (int i = 0; i < 700; ++i) {
      std::ofstream out(many + "/file-with-a-fairly-long-name-" + std::to_string(i) + ".txt");
      out << i;
    }
    seen = Seen{{}, 100000};
    CHECK(session.sftpList(many, visit, &seen) == Error::Ok);
    CHECK(seen.entries.size() == 700);
    seen = Seen{{}, 50};
    CHECK(session.sftpList(many, visit, &seen) == Error::Ok);
    CHECK(seen.entries.size() == 50);
    // The session is still good after a listing that stopped early.
    seen = Seen{{}, 100000};
    CHECK(session.sftpList(base, visit, &seen) == Error::Ok);
    CHECK(seen.entries.size() == 8);

    // Errors that are answers, not broken connections.
    seen = Seen{{}, 10};
    CHECK(session.sftpList(base + "/missing", visit, &seen) == Error::NotFound);
    CHECK(session.sftpOpenRead(base + "/missing.bin") == Error::NotFound);
    sshclient::DirEntry st;
    CHECK(session.sftpStat(base + "/missing.bin", st) == Error::NotFound);
    CHECK(session.connected());
    CHECK(session.sftpRemove(base + "/f1.bin") == Error::Ok);
    CHECK(access((base + "/f1.bin").c_str(), F_OK) != 0);

    // Cancel in the middle of a download: Aborted, and the session is not used again.
    bool cancel = false;
    session.setCancel(&cancel);
    CHECK(session.sftpOpenRead(base + "/f100000.bin") == Error::Ok);
    uint8_t buf[sshclient::Session::SFTP_CHUNK];
    size_t got = 0;
    CHECK(session.sftpRead(0, buf, sizeof(buf), got) == Error::Ok && got > 0);
    cancel = true;
    // A cancel is seen only while waiting for the server, so the read may still succeed once.
    Error ce = session.sftpRead(got, buf, sizeof(buf), got);
    printf("   read after cancel -> %d\n", static_cast<int>(ce));
    session.setCancel(nullptr);
    session.close();
    CHECK(!session.connected());
  }

  printf("6. shell with the device key through the terminal emulator\n");
  CHECK(sock.connectTo(s.port));
  login = keyLogin(s);
  e = session.openShell(login, 80, 24);
  printf("   openShell -> %d (library %d)\n", static_cast<int>(e), session.lastLibraryError());
  CHECK(e == Error::Ok);
  if (e == Error::Ok) {
    sshterm::Terminal term;
    CHECK(term.begin(80, 24, 100, sshterm::Memory{termAlloc, termFree}));
    const auto pump = [&](const int ms) {
      const uint32_t start = sock.nowMs();
      uint8_t buf[2048];
      while (static_cast<uint32_t>(sock.nowMs() - start) < static_cast<uint32_t>(ms)) {
        const int n = session.readShell(buf, sizeof(buf));
        if (n < 0) return false;
        if (n > 0) {
          term.feed(buf, static_cast<size_t>(n));
          const std::string reply = term.takeReply();
          if (!reply.empty()) session.writeShell(reinterpret_cast<const uint8_t*>(reply.data()), reply.size());
        } else {
          sock.idle();
        }
      }
      return true;
    };
    const auto screenHas = [&](const std::string& needle) {
      for (uint16_t offset = 0; offset <= term.scrollbackLines(); ++offset)
        for (uint16_t r = 0; r < term.rows(); ++r)
          if (term.rowText(r, offset).find(needle) != std::string::npos) return true;
      return false;
    };
    CHECK(pump(1500));
    const std::string cmd = "PS1='$ '; echo marker-$((6*7)); stty size; printf '\\033[1mbold\\033[0m\\n'\r";
    CHECK(session.writeShell(reinterpret_cast<const uint8_t*>(cmd.data()), cmd.size()) == Error::Ok);
    CHECK(pump(1500));
    CHECK(screenHas("marker-42"));
    CHECK(screenHas("24 80"));  // the pty has the size that was asked for
    CHECK(screenHas("bold"));
    CHECK(session.resizeShell(60, 20) == Error::Ok);
    const std::string cmd2 = "stty size\r";
    CHECK(session.writeShell(reinterpret_cast<const uint8_t*>(cmd2.data()), cmd2.size()) == Error::Ok);
    CHECK(pump(1000));
    CHECK(screenHas("20 60"));
    // Ctrl-C stops a running program.
    const std::string sleepCmd = "sleep 30; echo not-reached\r";
    CHECK(session.writeShell(reinterpret_cast<const uint8_t*>(sleepCmd.data()), sleepCmd.size()) == Error::Ok);
    CHECK(pump(500));
    const uint8_t ctrlC = 0x03;
    CHECK(session.writeShell(&ctrlC, 1) == Error::Ok);
    const std::string after = "echo after-$((1+1))\r";
    CHECK(session.writeShell(reinterpret_cast<const uint8_t*>(after.data()), after.size()) == Error::Ok);
    CHECK(pump(1000));
    CHECK(screenHas("after-2"));
    CHECK(!screenHas("not-reached\n"));
    // A burst of output: 3000 lines arrive whole and in order.
    const std::string burst = "seq 1 3000\r";
    CHECK(session.writeShell(reinterpret_cast<const uint8_t*>(burst.data()), burst.size()) == Error::Ok);
    CHECK(pump(3000));
    CHECK(screenHas("3000"));
    CHECK(screenHas("2990"));
    for (uint16_t r = 0; r < term.rows(); ++r) printf("   |%s\n", term.rowText(r).c_str());
    // exit ends the session: readShell reports it.
    const std::string bye = "exit\r";
    CHECK(session.writeShell(reinterpret_cast<const uint8_t*>(bye.data()), bye.size()) == Error::Ok);
    CHECK(!pump(3000));
    session.close();
  }

  printf("7. a server that goes away: timeout, not a hang\n");
  {
    // A listening socket that never answers.
    const int dead = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    bind(dead, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    listen(dead, 1);
    socklen_t alen = sizeof(addr);
    getsockname(dead, reinterpret_cast<sockaddr*>(&addr), &alen);
    CHECK(sock.connectTo(ntohs(addr.sin_port)));
    login = keyLogin(s);
    login.timeoutMs = 700;
    const uint32_t start = sock.nowMs();
    CHECK(session.openSftp(login) == Error::Network);
    const uint32_t took = sock.nowMs() - start;
    printf("   gave up after %u ms\n", took);
    CHECK(took >= 600 && took < 5000);
    // And a cancel while waiting.
    CHECK(sock.connectTo(ntohs(addr.sin_port)));
    login = keyLogin(s);
    bool cancel = true;
    login.cancel = &cancel;
    CHECK(session.openSftp(login) == Error::Aborted);
    close(dead);
  }

  printf(failures == 0 ? "ALL OK\n" : "%d FAILED\n", failures);
  return failures == 0 ? 0 : 1;
}
