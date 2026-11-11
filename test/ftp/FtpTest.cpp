#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "Ftp.h"

namespace {

// An in-memory end of a connection: `in` is what the client will read, `out` what it wrote.
struct FakeChannel : ftp::Channel {
  std::string in;
  std::string out;
  size_t maxRead = 7;  // small reads, so lines arrive in pieces
  size_t failWriteAfter = SIZE_MAX;
  bool timeoutWhenEmpty = false;
  bool closed = false;

  bool write(const uint8_t* data, size_t len) override {
    if (closed || out.size() + len > failWriteAfter) return false;
    out.append(reinterpret_cast<const char*>(data), len);
    onWrite();
    return true;
  }
  int read(uint8_t* buf, size_t len) override {
    if (closed) return -1;
    if (in.empty()) return timeoutWhenEmpty ? -1 : 0;
    const size_t n = std::min({len, maxRead, in.size()});
    std::memcpy(buf, in.data(), n);
    in.erase(0, n);
    return static_cast<int>(n);
  }
  void close() override { closed = true; }
  virtual void onWrite() {}
};

struct Step {
  std::string expect;  // the command line the client must send, without CR LF
  std::string reply;   // what the server answers (with CR LF)
};

// A scripted server: each command the client sends must be the next one in the script.
struct FakeServer : ftp::Transport {
  struct Control : FakeChannel {
    FakeServer* server = nullptr;
    size_t taken = 0;
    void onWrite() override {
      for (;;) {
        const size_t end = out.find("\r\n", taken);
        if (end == std::string::npos) return;
        const std::string line = out.substr(taken, end - taken);
        taken = end + 2;
        server->onCommand(line);
      }
    }
  };

  Control ctrl;
  std::deque<Step> script;
  std::deque<std::string> dataToSend;  // one per data connection, in order
  std::vector<FakeChannel> dataChannels;
  std::vector<uint16_t> dataPorts;
  std::vector<std::string> unexpected;
  bool refuseData = false;
  bool abortFlag = false;
  size_t dataFailWriteAfter = SIZE_MAX;

  explicit FakeServer(const std::string& greeting = "220 ready\r\n") {
    ctrl.server = this;
    ctrl.timeoutWhenEmpty = true;
    ctrl.in = greeting;
    dataChannels.reserve(8);
  }

  void onCommand(const std::string& line) {
    if (script.empty()) {
      unexpected.push_back(line);
      return;
    }
    if (script.front().expect != line) unexpected.push_back(line + " (wanted " + script.front().expect + ")");
    ctrl.in += script.front().reply;
    script.pop_front();
  }

  ftp::Channel& control() override { return ctrl; }
  ftp::Channel* openData(uint16_t port) override {
    dataPorts.push_back(port);
    if (refuseData) return nullptr;
    dataChannels.emplace_back();
    FakeChannel& ch = dataChannels.back();
    ch.maxRead = 5;
    ch.failWriteAfter = dataFailWriteAfter;
    if (!dataToSend.empty()) {
      ch.in = dataToSend.front();
      dataToSend.pop_front();
    }
    return &ch;
  }
  bool aborted() override { return abortFlag; }

  void expectLogin(const std::string& user = "lef", const std::string& pass = "secret") {
    script.push_back({"USER " + user, "331 password please\r\n"});
    script.push_back({"PASS " + pass, "230 welcome\r\n"});
    script.push_back({"TYPE I", "200 binary\r\n"});
  }
  void expectDone() {
    EXPECT_TRUE(script.empty()) << "commands not sent, next: " << (script.empty() ? "" : script.front().expect);
    EXPECT_TRUE(unexpected.empty()) << unexpected.front();
  }
};

ftp::Session::Sink collect(std::string& into) {
  return [&into](const uint8_t* data, size_t len) {
    into.append(reinterpret_cast<const char*>(data), len);
    return true;
  };
}

ftp::Session::Source from(const std::string& text, size_t& pos) {
  return [&text, &pos](uint8_t* buf, size_t len) {
    const size_t n = std::min(len, text.size() - pos);
    std::memcpy(buf, text.data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  };
}

}  // namespace

// ---- addresses and commands ----

TEST(FtpUrl, Parses) {
  ftp::Url u;
  ASSERT_TRUE(ftp::parseUrl("ftp://NAS.local", u));
  EXPECT_EQ(u.host, "nas.local");
  EXPECT_EQ(u.port, 21);
  EXPECT_EQ(u.path, "/");
  ASSERT_TRUE(ftp::parseUrl(" 192.168.1.5:2121/books/sci fi/ ", u));
  EXPECT_EQ(u.host, "192.168.1.5");
  EXPECT_EQ(u.port, 2121);
  EXPECT_EQ(u.path, "/books/sci fi");
  ASSERT_TRUE(ftp::parseUrl("FTP://host/a/../b", u));
  EXPECT_EQ(u.path, "/b");
}

TEST(FtpUrl, Refuses) {
  ftp::Url u;
  EXPECT_FALSE(ftp::parseUrl("", u));
  EXPECT_FALSE(ftp::parseUrl("ftps://host", u));
  EXPECT_FALSE(ftp::parseUrl("sftp://host", u));
  EXPECT_FALSE(ftp::parseUrl("http://host", u));
  EXPECT_FALSE(ftp::parseUrl("ftp://user:pw@host", u));
  EXPECT_FALSE(ftp::parseUrl("ftp://[::1]/", u));
  EXPECT_FALSE(ftp::parseUrl("host:0", u));
  EXPECT_FALSE(ftp::parseUrl("host:99999", u));
  EXPECT_FALSE(ftp::parseUrl("ho st", u));
  EXPECT_FALSE(ftp::parseUrl("host/../..", u));
  EXPECT_FALSE(ftp::parseUrl("host/a\r\nDELE x", u));
}

TEST(FtpCommand, BuildsAndRefusesLineBreaks) {
  EXPECT_EQ(ftp::command("QUIT"), "QUIT\r\n");
  EXPECT_EQ(ftp::command("RETR", "my book.epub"), "RETR my book.epub\r\n");
  EXPECT_EQ(ftp::command("RETR", "a\r\nDELE b"), "");
  EXPECT_EQ(ftp::command("RETR", "a\nb"), "");
  EXPECT_EQ(ftp::command("RETR", std::string("a\0b", 3)), "");
  EXPECT_EQ(ftp::command("RETR x\r\nDELE", "y"), "");
  EXPECT_EQ(ftp::command("", "y"), "");
}

// ---- replies ----

TEST(FtpReply, SingleAndMultiLine) {
  ftp::ReplyParser p;
  EXPECT_EQ(p.feedLine("230 Logged in"), ftp::ReplyParser::State::Done);
  EXPECT_EQ(p.reply().code, 230);
  EXPECT_EQ(p.reply().text, "Logged in");
  EXPECT_TRUE(p.reply().positive());

  p.reset();
  EXPECT_EQ(p.feedLine("220-Welcome"), ftp::ReplyParser::State::More);
  EXPECT_EQ(p.feedLine(" 230 this is not the end"), ftp::ReplyParser::State::More);
  EXPECT_EQ(p.feedLine("230 nor is this: another code"), ftp::ReplyParser::State::More);
  EXPECT_EQ(p.feedLine("220-still going"), ftp::ReplyParser::State::More);
  EXPECT_EQ(p.feedLine(""), ftp::ReplyParser::State::More);
  EXPECT_EQ(p.feedLine("220 ready"), ftp::ReplyParser::State::Done);
  EXPECT_EQ(p.reply().code, 220);
  EXPECT_EQ(p.reply().text, "ready");

  p.reset();
  EXPECT_EQ(p.feedLine("226"), ftp::ReplyParser::State::Done);
  EXPECT_EQ(p.reply().code, 226);
  EXPECT_EQ(p.reply().text, "");
}

TEST(FtpReply, RefusesWhatIsNotFtp) {
  ftp::ReplyParser p;
  EXPECT_EQ(p.feedLine("HTTP/1.1 400 Bad Request"), ftp::ReplyParser::State::Bad);
  p.reset();
  EXPECT_EQ(p.feedLine("SSH-2.0-OpenSSH_9.6"), ftp::ReplyParser::State::Bad);
  p.reset();
  EXPECT_EQ(p.feedLine("2200 x"), ftp::ReplyParser::State::Bad);
  p.reset();
  EXPECT_EQ(p.feedLine("099 x"), ftp::ReplyParser::State::Bad);
  p.reset();
  EXPECT_EQ(p.feedLine("220-banner"), ftp::ReplyParser::State::More);
  ftp::ReplyParser::State s = ftp::ReplyParser::State::More;
  for (size_t i = 0; i < ftp::MAX_REPLY_LINES + 2 && s == ftp::ReplyParser::State::More; ++i) s = p.feedLine("blah");
  EXPECT_EQ(s, ftp::ReplyParser::State::Bad);  // a banner that never ends
}

TEST(FtpReply, PasvAndEpsv) {
  uint16_t port = 0;
  EXPECT_TRUE(ftp::parsePasv("Entering Passive Mode (192,168,1,2,19,137).", port));
  EXPECT_EQ(port, 19 * 256 + 137);
  EXPECT_TRUE(ftp::parsePasv("227 Entering Passive Mode 10,0,0,1,4,1", port));
  EXPECT_EQ(port, 1025);
  EXPECT_TRUE(ftp::parsePasv("Passive mode v2 ok (10,0,0,1,255,255)", port));
  EXPECT_EQ(port, 65535);
  EXPECT_FALSE(ftp::parsePasv("Entering Passive Mode (192,168,1,2,19)", port));
  EXPECT_FALSE(ftp::parsePasv("Entering Passive Mode (192,168,1,2,300,1)", port));
  EXPECT_FALSE(ftp::parsePasv("Entering Passive Mode (1,2,3,4,0,0)", port));
  EXPECT_FALSE(ftp::parsePasv("no numbers", port));
  EXPECT_FALSE(ftp::parsePasv("", port));

  EXPECT_TRUE(ftp::parseEpsv("Entering Extended Passive Mode (|||5001|)", port));
  EXPECT_EQ(port, 5001);
  EXPECT_TRUE(ftp::parseEpsv("ok (!!!65535!)", port));
  EXPECT_EQ(port, 65535);
  EXPECT_FALSE(ftp::parseEpsv("Entering Extended Passive Mode (|||0|)", port));
  EXPECT_FALSE(ftp::parseEpsv("Entering Extended Passive Mode (|||70000|)", port));
  EXPECT_FALSE(ftp::parseEpsv("Entering Extended Passive Mode (|1|10.0.0.1|5001|)", port));
  EXPECT_FALSE(ftp::parseEpsv("Entering Extended Passive Mode (|||5001", port));
  EXPECT_FALSE(ftp::parseEpsv("(|||)", port));
  EXPECT_FALSE(ftp::parseEpsv("", port));
}

// ---- listings ----

TEST(FtpList, UnixLines) {
  remotefiles::Entry e;
  ASSERT_TRUE(ftp::parseListLine("drwxr-xr-x    2 lef      users        4096 Jan 12 10:00 Sci Fi\r", e));
  EXPECT_EQ(e.name, "Sci Fi");
  EXPECT_TRUE(e.isDir);
  EXPECT_EQ(e.size, 0u);
  EXPECT_EQ(e.mtime, 0);  // no year on the line

  ASSERT_TRUE(ftp::parseListLine("-rw-r--r--   1 lef users 1048576 Feb 29  2020 Ανάγνωση.epub", e));
  EXPECT_EQ(e.name, "Ανάγνωση.epub");
  EXPECT_FALSE(e.isDir);
  EXPECT_EQ(e.size, 1048576u);
  EXPECT_EQ(e.mtime, 1582934400);

  // No group column.
  ASSERT_TRUE(ftp::parseListLine("-rw-r--r--   1 lef 77 Dec  1 09:05 notes.txt", e));
  EXPECT_EQ(e.name, "notes.txt");
  EXPECT_EQ(e.size, 77u);

  // A name that starts with spaces keeps all but the separator; a name with a month in it.
  ASSERT_TRUE(ftp::parseListLine("-rw-r--r-- 1 0 0 5 Mar  3 2021 Jan 12 10:00 diary.txt", e));
  EXPECT_EQ(e.name, "Jan 12 10:00 diary.txt");
  EXPECT_EQ(e.size, 5u);

  ASSERT_TRUE(ftp::parseListLine("lrwxrwxrwx 1 root root 11 Jan  1 2024 latest -> book.epub", e));
  EXPECT_EQ(e.name, "latest");
  EXPECT_FALSE(e.isDir);

  ASSERT_TRUE(ftp::parseListLine("-rw-r--r--+ 1 lef users 4294967296 Jan 12 10:00 big.iso", e));
  EXPECT_EQ(e.size, 4294967296ULL);
}

TEST(FtpList, DosLines) {
  remotefiles::Entry e;
  ASSERT_TRUE(ftp::parseListLine("01-12-26  10:00AM       <DIR>          My Books", e));
  EXPECT_EQ(e.name, "My Books");
  EXPECT_TRUE(e.isDir);
  EXPECT_EQ(e.mtime, 1768212000);

  ASSERT_TRUE(ftp::parseListLine("02-29-2020  12:34PM              1048576 book.epub\r\n", e));
  EXPECT_EQ(e.name, "book.epub");
  EXPECT_FALSE(e.isDir);
  EXPECT_EQ(e.size, 1048576u);
  EXPECT_EQ(e.mtime, 1582979640);

  ASSERT_TRUE(ftp::parseListLine("12-31-99  12:05AM                    7 old.txt", e));
  EXPECT_EQ(e.mtime, 946598700);  // 1999-12-31 00:05
}

TEST(FtpList, NotEntries) {
  remotefiles::Entry e;
  EXPECT_FALSE(ftp::parseListLine("total 12", e));
  EXPECT_FALSE(ftp::parseListLine("", e));
  EXPECT_FALSE(ftp::parseListLine("drwxr-xr-x 2 lef users 4096 Jan 12 10:00 .", e));
  EXPECT_FALSE(ftp::parseListLine("drwxr-xr-x 2 lef users 4096 Jan 12 10:00 ..", e));
  EXPECT_FALSE(ftp::parseListLine("crw-rw-rw- 1 root root 1, 3 Jan 12 10:00 null", e));
  EXPECT_FALSE(ftp::parseListLine("-rw-r--r-- 1 lef users", e));
  EXPECT_FALSE(ftp::parseListLine("-rw-r--r-- 1 lef users 12 Jan 12 10:00", e));
  EXPECT_FALSE(ftp::parseListLine("01-12-26  10:00AM       <DIR>", e));
  EXPECT_FALSE(ftp::parseListLine("01-12-26  10:00XM   12 a", e));
  EXPECT_FALSE(ftp::parseListLine("226 Transfer complete", e));
}

TEST(FtpList, MlsdLines) {
  remotefiles::Entry e;
  ASSERT_TRUE(ftp::parseMlsdLine("type=file;size=1048576;modify=20200229123456;perm=r; my book.epub\r", e));
  EXPECT_EQ(e.name, "my book.epub");
  EXPECT_FALSE(e.isDir);
  EXPECT_EQ(e.size, 1048576u);
  EXPECT_EQ(e.mtime, 1582979696);

  ASSERT_TRUE(ftp::parseMlsdLine("Type=DIR;Modify=20260112100000.123;sizd=4096; Sci Fi", e));
  EXPECT_TRUE(e.isDir);
  EXPECT_EQ(e.size, 0u);
  EXPECT_EQ(e.mtime, 1768212000);

  EXPECT_FALSE(ftp::parseMlsdLine("type=cdir;modify=20260112100000; .", e));
  EXPECT_FALSE(ftp::parseMlsdLine("type=pdir;modify=20260112100000; ..", e));
  EXPECT_FALSE(ftp::parseMlsdLine("type=OS.unix=slink:/x; link", e));
  EXPECT_FALSE(ftp::parseMlsdLine("size=12; untyped", e));
  EXPECT_FALSE(ftp::parseMlsdLine("type=file;size=12;", e));
  EXPECT_FALSE(ftp::parseMlsdLine("", e));
}

// ---- the conversation ----

TEST(FtpSession, LoginWithMultiLineGreeting) {
  FakeServer s("220-Welcome to the NAS\r\n220-Second line\r\n220 ready\r\n");
  s.expectLogin();
  ftp::Session ftp(s);
  EXPECT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  s.expectDone();
}

TEST(FtpSession, AnonymousLoginWithoutPassword) {
  FakeServer s;
  s.script.push_back({"USER anonymous", "230 hello\r\n"});
  s.script.push_back({"TYPE I", "200 ok\r\n"});
  ftp::Session ftp(s);
  EXPECT_EQ(ftp.login("", ""), ftp::Error::Ok);
  s.expectDone();
}

TEST(FtpSession, WrongPassword) {
  FakeServer s;
  s.script.push_back({"USER lef", "331 password\r\n"});
  s.script.push_back({"PASS wrong", "530 Login incorrect\r\n"});
  ftp::Session ftp(s);
  EXPECT_EQ(ftp.login("lef", "wrong"), ftp::Error::Login);
  EXPECT_EQ(ftp.lastReply().code, 530);
  s.expectDone();
}

TEST(FtpSession, NotAnFtpServer) {
  {
    FakeServer s("SSH-2.0-OpenSSH_9.6\r\n");
    ftp::Session ftp(s);
    EXPECT_EQ(ftp.login("lef", "x"), ftp::Error::Protocol);
    EXPECT_TRUE(s.ctrl.out.empty());  // the password was never sent
  }
  {
    FakeServer s("421 Too many users\r\n");
    ftp::Session ftp(s);
    EXPECT_EQ(ftp.login("lef", "x"), ftp::Error::Protocol);
    EXPECT_TRUE(s.ctrl.out.empty());
  }
  {
    FakeServer s("");  // silence, then a timeout
    ftp::Session ftp(s);
    EXPECT_EQ(ftp.login("lef", "x"), ftp::Error::Network);
  }
  {
    FakeServer s(std::string(ftp::MAX_LINE_BYTES + 50, 'x'));  // a line that never ends
    ftp::Session ftp(s);
    EXPECT_EQ(ftp.login("lef", "x"), ftp::Error::Protocol);
  }
}

TEST(FtpSession, ANameCannotAddACommand) {
  FakeServer s;
  s.expectLogin();
  ftp::Session ftp(s);
  ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  std::string got;
  EXPECT_EQ(ftp.retrieve("/", "a\r\nDELE b", collect(got)), ftp::Error::BadName);
  EXPECT_EQ(ftp.makeDir("/", "a\nb"), ftp::Error::BadName);
  size_t pos = 0;
  const std::string body = "x";
  EXPECT_EQ(ftp.store("/", "a\rb", from(body, pos)), ftp::Error::BadName);
  s.expectDone();
  EXPECT_EQ(s.ctrl.out.find("DELE"), std::string::npos);
}

TEST(FtpSession, ListWithMlsdOverEpsv) {
  FakeServer s;
  s.expectLogin();
  s.script.push_back({"CWD /books", "250 ok\r\n"});
  s.script.push_back({"EPSV", "229 Entering Extended Passive Mode (|||5001|)\r\n"});
  s.script.push_back({"MLSD", "150 here it comes\r\n226 done\r\n"});
  s.dataToSend.push_back(
      "type=cdir; .\r\ntype=pdir; ..\r\n"
      "type=dir;modify=20260112100000; Sci Fi\r\n"
      "type=file;size=42;modify=20200229123456; notes.txt\r\n"
      "type=file;size=7; bad:name.txt\r\n");
  ftp::Session ftp(s);
  ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  remotefiles::Listing l;
  EXPECT_EQ(ftp.list("/books", l), ftp::Error::Ok);
  s.expectDone();
  ASSERT_EQ(s.dataPorts.size(), 1u);
  EXPECT_EQ(s.dataPorts[0], 5001);
  ASSERT_EQ(l.entries.size(), 2u);
  EXPECT_EQ(l.entries[0].name, "Sci Fi");
  EXPECT_TRUE(l.entries[0].isDir);
  EXPECT_EQ(l.entries[1].name, "notes.txt");
  EXPECT_EQ(l.entries[1].size, 42u);
  EXPECT_EQ(l.skipped, 1u);
  EXPECT_TRUE(s.dataChannels[0].closed);
}

TEST(FtpSession, FallsBackToPasvAndListOnAnOldServer) {
  FakeServer s;
  s.expectLogin();
  s.script.push_back({"CWD /", "250 ok\r\n"});
  s.script.push_back({"EPSV", "500 unknown command\r\n"});
  s.script.push_back({"PASV", "227 Entering Passive Mode (10,0,0,9,19,137)\r\n"});
  s.script.push_back({"MLSD", "500 unknown command\r\n"});
  // EPSV is not asked again.
  s.script.push_back({"PASV", "227 Entering Passive Mode (10,0,0,9,19,138)\r\n"});
  s.script.push_back({"LIST", "150 ok\r\n226 done\r\n"});
  s.dataToSend.push_back("");  // the connection opened for the refused MLSD
  s.dataToSend.push_back(
      "total 8\r\n"
      "drwxr-xr-x 2 lef users 4096 Jan 12 10:00 .\r\n"
      "drwxr-xr-x 2 lef users 4096 Jan 12 10:00 comics\r\n"
      "-rw-r--r-- 1 lef users 99 Jan 12 10:00 last line without a break.txt");
  ftp::Session ftp(s);
  ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  remotefiles::Listing l;
  EXPECT_EQ(ftp.list("/", l), ftp::Error::Ok);
  s.expectDone();
  ASSERT_EQ(s.dataPorts.size(), 2u);
  EXPECT_EQ(s.dataPorts[1], 19 * 256 + 138);
  EXPECT_TRUE(s.dataChannels[0].closed);
  ASSERT_EQ(l.entries.size(), 2u);
  EXPECT_EQ(l.entries[0].name, "comics");
  EXPECT_EQ(l.entries[1].name, "last line without a break.txt");

  // The next listing goes straight to PASV + LIST.
  s.script.push_back({"CWD /comics", "250 ok\r\n"});
  s.script.push_back({"PASV", "227 Entering Passive Mode (10,0,0,9,19,139)\r\n"});
  s.script.push_back({"LIST", "150 ok\r\n226 done\r\n"});
  s.dataToSend.push_back("");
  remotefiles::Listing empty;
  EXPECT_EQ(ftp.list("/comics", empty), ftp::Error::Ok);
  EXPECT_TRUE(empty.entries.empty());
  s.expectDone();
}

TEST(FtpSession, ListErrors) {
  {
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /nope", "550 No such directory\r\n"});
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    remotefiles::Listing l;
    EXPECT_EQ(ftp.list("/nope", l), ftp::Error::Refused);
    EXPECT_EQ(ftp.lastReply().code, 550);
    s.expectDone();
  }
  {
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.refuseData = true;
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    remotefiles::Listing l;
    EXPECT_EQ(ftp.list("/", l), ftp::Error::Network);
    s.expectDone();
  }
  {
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "500 no\r\n"});
    s.script.push_back({"PASV", "227 nonsense\r\n"});
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    remotefiles::Listing l;
    EXPECT_EQ(ftp.list("/", l), ftp::Error::Protocol);
    EXPECT_TRUE(s.dataPorts.empty());
    s.expectDone();
  }
}

TEST(FtpSession, ListStopsAtTheCapAndSkipsOverlongLines) {
  FakeServer s;
  s.expectLogin();
  s.script.push_back({"CWD /", "250 ok\r\n"});
  s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
  s.script.push_back({"MLSD", "150 ok\r\n426 closed early\r\n"});
  std::string data = "type=file;size=1; " + std::string(ftp::MAX_LINE_BYTES + 100, 'x') + "\r\n";
  for (int i = 0; i < 700; ++i) data += "type=file;size=1; f" + std::to_string(i) + "\r\n";
  s.dataToSend.push_back(data);
  ftp::Session ftp(s);
  ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  remotefiles::Listing l;
  EXPECT_EQ(ftp.list("/", l), ftp::Error::Ok);  // cut, not failed: the 426 is expected
  EXPECT_TRUE(l.truncated);
  EXPECT_EQ(l.entries.size(), remotefiles::MAX_ENTRIES);
  EXPECT_EQ(l.entries[0].name, "f0");
  EXPECT_FALSE(s.dataChannels[0].in.empty());  // the rest was not read
  s.expectDone();
}

TEST(FtpSession, Retrieve) {
  FakeServer s;
  s.expectLogin();
  s.script.push_back({"CWD /books", "250 ok\r\n"});
  s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
  s.script.push_back({"RETR my book.epub", "150 Opening BINARY mode\r\n226 Transfer complete\r\n"});
  const std::string body = std::string("PK\x03\x04", 4) + std::string(10000, 'z') + std::string("\0\r\n\xFF", 4);
  s.dataToSend.push_back(body);
  ftp::Session ftp(s);
  ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  std::string got;
  EXPECT_EQ(ftp.retrieve("/books", "my book.epub", collect(got)), ftp::Error::Ok);
  EXPECT_EQ(got, body);
  s.expectDone();
}

TEST(FtpSession, RetrieveErrors) {
  {  // no such file
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.script.push_back({"RETR gone.txt", "550 No such file\r\n"});
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    std::string got;
    EXPECT_EQ(ftp.retrieve("/", "gone.txt", collect(got)), ftp::Error::Refused);
    EXPECT_TRUE(s.dataChannels[0].closed);
    s.expectDone();
  }
  {  // the server reports a failed transfer after the data
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.script.push_back({"RETR a.txt", "150 ok\r\n451 read error on the server\r\n"});
    s.dataToSend.push_back("half");
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    std::string got;
    EXPECT_EQ(ftp.retrieve("/", "a.txt", collect(got)), ftp::Error::Refused);
    s.expectDone();
  }
  {  // the card is full
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.script.push_back({"RETR a.txt", "150 ok\r\n426 aborted\r\n"});
    s.dataToSend.push_back(std::string(100, 'a'));
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    EXPECT_EQ(ftp.retrieve("/", "a.txt", [](const uint8_t*, size_t) { return false; }), ftp::Error::Local);
    EXPECT_TRUE(s.dataChannels[0].closed);
    s.expectDone();
  }
  {  // the user stops it
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.script.push_back({"RETR a.txt", "150 ok\r\n"});
    s.dataToSend.push_back(std::string(100, 'a'));
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    size_t taken = 0;
    EXPECT_EQ(ftp.retrieve("/", "a.txt",
                           [&](const uint8_t*, size_t len) {
                             taken += len;
                             s.abortFlag = true;
                             return true;
                           }),
              ftp::Error::Aborted);
    EXPECT_LT(taken, 100u);
    EXPECT_TRUE(s.dataChannels[0].closed);
  }
}

TEST(FtpSession, StoreGoesToAPartFileAndIsRenamed) {
  FakeServer s;
  s.expectLogin();
  s.script.push_back({"CWD /in box", "250 ok\r\n"});
  s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
  s.script.push_back({"STOR new book.epub.part", "150 ok\r\n226 stored\r\n"});
  s.script.push_back({"RNFR new book.epub.part", "350 ready\r\n"});
  s.script.push_back({"RNTO new book.epub", "250 renamed\r\n"});
  const std::string body = std::string(9000, 'q') + std::string("\0\x01", 2);
  size_t pos = 0;
  ftp::Session ftp(s);
  ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  EXPECT_EQ(ftp.store("/in box", "new book.epub", from(body, pos)), ftp::Error::Ok);
  s.expectDone();
  EXPECT_EQ(s.dataChannels[0].out, body);
  EXPECT_TRUE(s.dataChannels[0].closed);
}

TEST(FtpSession, StoreReplacesAFileTheServerWillNotRenameOver) {
  FakeServer s;
  s.expectLogin();
  s.script.push_back({"CWD /", "250 ok\r\n"});
  s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
  s.script.push_back({"STOR a.txt.part", "150 ok\r\n226 stored\r\n"});
  s.script.push_back({"RNFR a.txt.part", "350 ready\r\n"});
  s.script.push_back({"RNTO a.txt", "550 file exists\r\n"});
  s.script.push_back({"DELE a.txt", "250 deleted\r\n"});
  s.script.push_back({"RNFR a.txt.part", "350 ready\r\n"});
  s.script.push_back({"RNTO a.txt", "250 renamed\r\n"});
  const std::string body = "hello";
  size_t pos = 0;
  ftp::Session ftp(s);
  ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  EXPECT_EQ(ftp.store("/", "a.txt", from(body, pos)), ftp::Error::Ok);
  s.expectDone();
}

TEST(FtpSession, StoreErrorsLeaveNoPartFile) {
  {  // no permission
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.script.push_back({"STOR a.txt.part", "553 Permission denied\r\n"});
    const std::string body = "hello";
    size_t pos = 0;
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    EXPECT_EQ(ftp.store("/", "a.txt", from(body, pos)), ftp::Error::Refused);
    EXPECT_EQ(ftp.lastReply().code, 553);
    EXPECT_EQ(pos, 0u);  // nothing was read from the card
    s.expectDone();
  }
  {  // the server's disk fills up
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.script.push_back({"STOR a.txt.part", "150 ok\r\n552 Disk full\r\n"});
    s.script.push_back({"DELE a.txt.part", "250 deleted\r\n"});
    const std::string body = "hello";
    size_t pos = 0;
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    EXPECT_EQ(ftp.store("/", "a.txt", from(body, pos)), ftp::Error::Refused);
    EXPECT_EQ(ftp.lastReply().code, 552);  // the reason, not the clean-up's answer
    s.expectDone();
  }
  {  // the card cannot be read
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.script.push_back({"STOR a.txt.part", "150 ok\r\n226 stored\r\n"});
    s.script.push_back({"DELE a.txt.part", "250 deleted\r\n"});
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    EXPECT_EQ(ftp.store("/", "a.txt", [](uint8_t*, size_t) { return -1; }), ftp::Error::Local);
    s.expectDone();  // never renamed into place
  }
  {  // the data connection breaks
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.script.push_back({"STOR a.txt.part", "150 ok\r\n426 connection lost\r\n"});
    s.script.push_back({"DELE a.txt.part", "250 deleted\r\n"});
    s.dataFailWriteAfter = 4096;
    const std::string body(20000, 'b');
    size_t pos = 0;
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    EXPECT_EQ(ftp.store("/", "a.txt", from(body, pos)), ftp::Error::Network);
    EXPECT_EQ(s.ctrl.out.find("RNFR"), std::string::npos);
    s.expectDone();
  }
  {  // the rename is refused twice: the .part is removed and the old file's fate is reported
    FakeServer s;
    s.expectLogin();
    s.script.push_back({"CWD /", "250 ok\r\n"});
    s.script.push_back({"EPSV", "229 ok (|||5001|)\r\n"});
    s.script.push_back({"STOR a.txt.part", "150 ok\r\n226 stored\r\n"});
    s.script.push_back({"RNFR a.txt.part", "350 ready\r\n"});
    s.script.push_back({"RNTO a.txt", "550 no\r\n"});
    s.script.push_back({"DELE a.txt", "550 no permission\r\n"});
    s.script.push_back({"DELE a.txt.part", "250 deleted\r\n"});
    const std::string body = "hello";
    size_t pos = 0;
    ftp::Session ftp(s);
    ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
    EXPECT_EQ(ftp.store("/", "a.txt", from(body, pos)), ftp::Error::Refused);
    EXPECT_EQ(ftp.lastReply().code, 550);
    s.expectDone();
  }
}

TEST(FtpSession, MakeDirAndQuit) {
  FakeServer s;
  s.expectLogin();
  s.script.push_back({"CWD /books", "250 ok\r\n"});
  s.script.push_back({"MKD New Folder", "257 \"/books/New Folder\" created\r\n"});
  s.script.push_back({"CWD /books", "250 ok\r\n"});
  s.script.push_back({"MKD New Folder", "550 exists\r\n"});
  s.script.push_back({"QUIT", "221 bye\r\n"});
  ftp::Session ftp(s);
  ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  EXPECT_EQ(ftp.makeDir("/books", "New Folder"), ftp::Error::Ok);
  EXPECT_EQ(ftp.makeDir("/books", "New Folder"), ftp::Error::Refused);
  ftp.quit();
  s.expectDone();
}

TEST(FtpSession, TheControlConnectionDropsMidway) {
  FakeServer s;
  s.expectLogin();
  s.script.push_back({"CWD /", "250 ok\r\n"});
  s.script.push_back({"EPSV", ""});  // no answer: a timeout
  ftp::Session ftp(s);
  ASSERT_EQ(ftp.login("lef", "secret"), ftp::Error::Ok);
  remotefiles::Listing l;
  EXPECT_EQ(ftp.list("/", l), ftp::Error::Network);
}
