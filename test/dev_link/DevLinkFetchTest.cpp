// CMD:FETCH (VAULTINK_DEVLINK_FULL): the URL and address guard, the arguments, the SHA-256 text
// and the head of the HTTP answer. No network: the device side is src/util/DevLinkDevice.cpp.
#include <gtest/gtest.h>

#include <cctype>
#include <cstring>
#include <string>

#include "DevLink.h"
#include "DevLinkFetch.h"
#include "util/DevLinkPaths.h"

namespace {

using devlink::FetchUrl;
using devlink::HeadCheck;
using devlink::HttpHead;
using devlink::UrlCheck;

UrlCheck check(const std::string& url) {
  FetchUrl out;
  return devlink::checkFetchUrl(url.data(), url.size(), out);
}

std::string ipOf(const FetchUrl& url) {
  return std::to_string(url.ip[0]) + "." + std::to_string(url.ip[1]) + "." + std::to_string(url.ip[2]) + "." +
         std::to_string(url.ip[3]);
}

const std::string HASH = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";  // sha256("test")

HttpHead::State feedAll(HttpHead& head, const std::string& text, size_t* used = nullptr) {
  HttpHead::State state = HttpHead::State::More;
  size_t i = 0;
  for (; i < text.size() && state == HttpHead::State::More; ++i) state = head.feed(text[i]);
  if (used) *used = i;
  return state;
}

TEST(FetchUrlGuard, TakesAPrivateAddressWithAPath) {
  const std::string text = "http://192.168.1.20:8000/firmware.bin";
  FetchUrl url;
  ASSERT_EQ(devlink::checkFetchUrl(text.data(), text.size(), url), UrlCheck::Ok);
  EXPECT_EQ(ipOf(url), "192.168.1.20");
  EXPECT_EQ(url.port, 8000);
  EXPECT_EQ(std::string(url.path, url.pathLength), "/firmware.bin");

  const std::string noPort = "http://10.0.0.5/fw/x4pro-dev_1.2.3~a.bin";
  ASSERT_EQ(devlink::checkFetchUrl(noPort.data(), noPort.size(), url), UrlCheck::Ok);
  EXPECT_EQ(ipOf(url), "10.0.0.5");
  EXPECT_EQ(url.port, 80);
  EXPECT_EQ(std::string(url.path, url.pathLength), "/fw/x4pro-dev_1.2.3~a.bin");
}

TEST(FetchUrlGuard, ThePrivateRangesAndTheirEdges) {
  EXPECT_EQ(check("http://10.0.0.0/a"), UrlCheck::Ok);
  EXPECT_EQ(check("http://10.255.255.255/a"), UrlCheck::Ok);
  EXPECT_EQ(check("http://9.255.255.255/a"), UrlCheck::NotPrivate);
  EXPECT_EQ(check("http://11.0.0.0/a"), UrlCheck::NotPrivate);

  EXPECT_EQ(check("http://172.15.255.255/a"), UrlCheck::NotPrivate);
  EXPECT_EQ(check("http://172.16.0.0/a"), UrlCheck::Ok);
  EXPECT_EQ(check("http://172.31.255.255/a"), UrlCheck::Ok);
  EXPECT_EQ(check("http://172.32.0.0/a"), UrlCheck::NotPrivate);
  EXPECT_EQ(check("http://171.16.0.1/a"), UrlCheck::NotPrivate);

  EXPECT_EQ(check("http://192.168.0.0/a"), UrlCheck::Ok);
  EXPECT_EQ(check("http://192.168.255.255/a"), UrlCheck::Ok);
  EXPECT_EQ(check("http://192.167.1.1/a"), UrlCheck::NotPrivate);
  EXPECT_EQ(check("http://192.169.1.1/a"), UrlCheck::NotPrivate);
  EXPECT_EQ(check("http://193.168.1.1/a"), UrlCheck::NotPrivate);
}

TEST(FetchUrlGuard, OtherAddressesAreRefused) {
  EXPECT_EQ(check("http://127.0.0.1/a"), UrlCheck::NotPrivate);  // loopback
  EXPECT_EQ(check("http://127.1.2.3:8000/a"), UrlCheck::NotPrivate);
  EXPECT_EQ(check("http://169.254.1.1/a"), UrlCheck::NotPrivate);  // link-local
  EXPECT_EQ(check("http://0.0.0.0/a"), UrlCheck::NotPrivate);
  EXPECT_EQ(check("http://100.64.0.1/a"), UrlCheck::NotPrivate);  // carrier NAT
  EXPECT_EQ(check("http://8.8.8.8/a"), UrlCheck::NotPrivate);
  EXPECT_EQ(check("http://224.0.0.1/a"), UrlCheck::NotPrivate);
  EXPECT_EQ(check("http://255.255.255.255/a"), UrlCheck::NotPrivate);

  const uint8_t loopback[4] = {127, 0, 0, 1};
  const uint8_t lan[4] = {172, 20, 1, 1};
  EXPECT_FALSE(devlink::isPrivateIpv4(loopback));
  EXPECT_TRUE(devlink::isPrivateIpv4(lan));
}

TEST(FetchUrlGuard, OnlyPlainHttpInLowerCase) {
  EXPECT_EQ(check("https://192.168.1.2/a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check("HTTP://192.168.1.2/a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check("Http://192.168.1.2/a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check("ftp://192.168.1.2/a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check("file:///a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check("192.168.1.2/a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check("//192.168.1.2/a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check("http:/192.168.1.2/a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check("http:192.168.1.2/a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check(" http://192.168.1.2/a.bin"), UrlCheck::NotHttp);
  EXPECT_EQ(check(""), UrlCheck::TooLong);
  FetchUrl out;
  EXPECT_EQ(devlink::checkFetchUrl(nullptr, 5, out), UrlCheck::TooLong);
}

TEST(FetchUrlGuard, TheHostIsFourPlainNumbers) {
  EXPECT_EQ(check("http://reader.local/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://localhost/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.1.2.example.com/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.1.2.5/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.1/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.1./a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168..1/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.1.256/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.1.1000/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.01.2/a.bin"), UrlCheck::BadHost);   // could be read as octal
  EXPECT_EQ(check("http://0300.0250.1.2/a.bin"), UrlCheck::BadHost);  // octal 192.168
  EXPECT_EQ(check("http://0xc0.0xa8.1.2/a.bin"), UrlCheck::BadHost);  // hex
  EXPECT_EQ(check("http://3232235778/a.bin"), UrlCheck::BadHost);     // 192.168.1.2 as one number
  EXPECT_EQ(check("http://10.1/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://[::1]/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://[fe80::1]:8000/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://[::ffff:192.168.1.2]/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http:///a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://-192.168.1.2/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://+192.168.1.2/a.bin"), UrlCheck::BadHost);
}

TEST(FetchUrlGuard, UserinfoIsRefused) {
  EXPECT_EQ(check("http://user@192.168.1.2/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://user:pass@192.168.1.2/a.bin"), UrlCheck::BadHost);
  // The address in front is the user name here, and the real host comes after the '@'.
  EXPECT_EQ(check("http://192.168.1.2@8.8.8.8/a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.1.2:80@8.8.8.8/a.bin"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:pw@evil.example/a.bin"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2/a@8.8.8.8/b.bin"), UrlCheck::BadPath);
}

TEST(FetchUrlGuard, Ports) {
  FetchUrl url;
  const std::string low = "http://192.168.1.2:1/a";
  ASSERT_EQ(devlink::checkFetchUrl(low.data(), low.size(), url), UrlCheck::Ok);
  EXPECT_EQ(url.port, 1);
  const std::string high = "http://192.168.1.2:65535/a";
  ASSERT_EQ(devlink::checkFetchUrl(high.data(), high.size(), url), UrlCheck::Ok);
  EXPECT_EQ(url.port, 65535);
  EXPECT_EQ(check("http://192.168.1.2:0/a"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:65536/a"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:99999/a"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:123456/a"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:4294967376/a"), UrlCheck::BadPort);  // 80 after a 32-bit wrap
  EXPECT_EQ(check("http://192.168.1.2:080/a"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:/a"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:-80/a"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:80a/a"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:80:81/a"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2:8000"), UrlCheck::BadPath);  // no file named
}

TEST(FetchUrlGuard, ThePathIsPlainWithNoTraversal) {
  EXPECT_EQ(check("http://192.168.1.2"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/fw/"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/../secret.bin"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/fw/../../a.bin"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/fw/.."), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/a..b.bin"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/%2e%2e/a.bin"), UrlCheck::BadPath);  // no percent escapes at all
  EXPECT_EQ(check("http://192.168.1.2/a%20b.bin"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2//a.bin"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/fw//a.bin"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/fw\\a.bin"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/a.bin?x=1"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/a.bin#top"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/a.bin;x"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/./a.bin"), UrlCheck::Ok);  // one dot names the same folder
}

TEST(FetchUrlGuard, NothingMayFollowTheUrl) {
  EXPECT_EQ(check("http://192.168.1.2/a.bin "), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/a.bin junk"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/a.bin\r\nHost: evil"), UrlCheck::BadPath);  // no header can be added
  EXPECT_EQ(check("http://192.168.1.2/a.bin\n"), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2/a.bin\t"), UrlCheck::BadPath);
  EXPECT_EQ(check(std::string("http://192.168.1.2/a.bin\0x", 26)), UrlCheck::BadPath);
  EXPECT_EQ(check("http://192.168.1.2 /a.bin"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.1.2:80 /a.bin"), UrlCheck::BadPort);
  EXPECT_EQ(check("http://192.168.1.2/\xce\xb1.bin"), UrlCheck::BadPath);  // not ASCII
}

TEST(FetchUrlGuard, VeryLongUrls) {
  const std::string head = "http://192.168.1.2/";
  const std::string fits = head + std::string(devlink::FETCH_MAX_URL - head.size(), 'a');
  ASSERT_EQ(fits.size(), devlink::FETCH_MAX_URL);
  EXPECT_EQ(check(fits), UrlCheck::Ok);
  EXPECT_EQ(check(fits + "a"), UrlCheck::TooLong);
  EXPECT_EQ(check(head + std::string(100000, 'a')), UrlCheck::TooLong);
  EXPECT_EQ(check("http://" + std::string(5000, '1') + "/a"), UrlCheck::TooLong);
  // Many digits inside the limit: no number is read past its width.
  EXPECT_EQ(check("http://" + std::string(100, '1') + "/a"), UrlCheck::BadHost);
  EXPECT_EQ(check("http://192.168.1.2:" + std::string(100, '1') + "/a"), UrlCheck::BadPort);
}

TEST(FetchUrlGuard, ARefusedUrlLeavesTheResultAlone) {
  FetchUrl url;
  url.port = 1234;
  const std::string bad = "http://8.8.8.8:99/a.bin";
  EXPECT_EQ(devlink::checkFetchUrl(bad.data(), bad.size(), url), UrlCheck::NotPrivate);
  EXPECT_EQ(url.port, 1234);
  EXPECT_EQ(url.pathLength, 0u);
}

TEST(FetchUrlGuard, EveryResultHasAText) {
  for (const UrlCheck value : {UrlCheck::Ok, UrlCheck::TooLong, UrlCheck::NotHttp, UrlCheck::BadHost,
                               UrlCheck::NotPrivate, UrlCheck::BadPort, UrlCheck::BadPath}) {
    EXPECT_GT(strlen(devlink::urlCheckText(value)), 0u);
  }
  for (const HeadCheck value : {HeadCheck::Ok, HeadCheck::NotOk200, HeadCheck::Encoded, HeadCheck::NoLength,
                                HeadCheck::Empty, HeadCheck::TooLarge}) {
    EXPECT_GT(strlen(devlink::headCheckText(value)), 0u);
  }
}

TEST(FetchArgs, UrlPathAndHash) {
  const std::string text = "http://192.168.1.2:8000/fw.bin /firmware.bin " + HASH;
  devlink::FetchArgs args;
  ASSERT_TRUE(devlink::parseFetchArgs(text.data(), text.size(), args));
  EXPECT_EQ(std::string(args.url, args.urlLength), "http://192.168.1.2:8000/fw.bin");
  EXPECT_EQ(std::string(args.path, args.pathLength), "/firmware.bin");
  EXPECT_EQ(args.sha256[0], 0x9f);
  EXPECT_EQ(args.sha256[31], 0x08);
  char hex[devlink::SHA256_HEX + 1];
  devlink::sha256Hex(args.sha256, hex);
  EXPECT_EQ(std::string(hex), HASH);
}

TEST(FetchArgs, ThePathKeepsItsBlanks) {
  const std::string text = "  http://10.0.0.1/a.bin   /new builds/my fw.bin \t " + HASH + "  ";
  devlink::FetchArgs args;
  ASSERT_TRUE(devlink::parseFetchArgs(text.data(), text.size(), args));
  EXPECT_EQ(std::string(args.url, args.urlLength), "http://10.0.0.1/a.bin");
  EXPECT_EQ(std::string(args.path, args.pathLength), "/new builds/my fw.bin");
}

TEST(FetchArgs, Refused) {
  devlink::FetchArgs args;
  const auto parses = [&](const std::string& text) { return devlink::parseFetchArgs(text.data(), text.size(), args); };
  EXPECT_FALSE(parses(""));
  EXPECT_FALSE(parses("   "));
  EXPECT_FALSE(parses("http://10.0.0.1/a.bin"));
  EXPECT_FALSE(parses("http://10.0.0.1/a.bin /a.bin"));
  EXPECT_FALSE(parses("http://10.0.0.1/a.bin " + HASH));  // no path
  EXPECT_FALSE(parses(HASH));
  EXPECT_FALSE(parses("http://10.0.0.1/a.bin /a.bin " + HASH.substr(1)));
  EXPECT_FALSE(parses("http://10.0.0.1/a.bin /a.bin " + HASH + "0"));
  EXPECT_FALSE(parses("http://10.0.0.1/a.bin /a.bin " + HASH.substr(0, 63) + "g"));
  EXPECT_FALSE(parses("http://10.0.0.1/a.bin /a.bin " + HASH + " extra"));
  EXPECT_FALSE(devlink::parseFetchArgs(nullptr, 0, args));
}

TEST(FetchArgs, HashText) {
  uint8_t digest[devlink::SHA256_BYTES];
  std::string upper = HASH;
  for (char& c : upper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  ASSERT_TRUE(devlink::parseSha256Hex(upper.data(), upper.size(), digest));
  char hex[devlink::SHA256_HEX + 1];
  devlink::sha256Hex(digest, hex);
  EXPECT_EQ(std::string(hex), HASH);  // always written in lower case
  EXPECT_FALSE(devlink::parseSha256Hex(HASH.data(), 63, digest));
  EXPECT_FALSE(devlink::parseSha256Hex(nullptr, 64, digest));
  const std::string spaced = HASH.substr(0, 63) + " ";
  EXPECT_FALSE(devlink::parseSha256Hex(spaced.data(), spaced.size(), digest));
}

// The whole command as the host sends it fits one line and passes every pure check.
TEST(FetchArgs, AWholeCommandLine) {
  const std::string line = "CMD:FETCH http://192.168.1.20:8000/firmware.bin /firmware-dev.bin " + HASH;
  ASSERT_LE(line.size(), devlink::LineAssembler::MAX_LINE);
  const devlink::Parsed parsed = devlink::parse(line.data(), line.size());
  ASSERT_EQ(parsed.command, devlink::Command::Fetch);
  devlink::FetchArgs args;
  ASSERT_TRUE(devlink::parseFetchArgs(parsed.args, parsed.argsLength, args));
  FetchUrl url;
  EXPECT_EQ(devlink::checkFetchUrl(args.url, args.urlLength, url), UrlCheck::Ok);
  using devlinkpaths::Access;
  EXPECT_TRUE(devlinkpaths::isAllowed(std::string(args.path, args.pathLength), Access::Flash));
  // What FETCH may not write: anything FLASH could not read back as an image.
  EXPECT_FALSE(devlinkpaths::isAllowed("/.crosspoint/settings.bin", Access::Flash));
  EXPECT_FALSE(devlinkpaths::isAllowed("/.vaultink/x.bin", Access::Flash));
  EXPECT_FALSE(devlinkpaths::isAllowed("/Vaults/note.md", Access::Flash));
  EXPECT_FALSE(devlinkpaths::isAllowed("/fw/../.crosspoint/a.bin", Access::Flash));
  EXPECT_FALSE(devlinkpaths::isAllowed("firmware.bin", Access::Flash));
}

// scripts/deploy_web_installer.sh looks for this text to refuse a dev-user image with the full link.
TEST(FetchArgs, TheHelpTextKeepsTheDeployGuard) {
  const std::string list = devlink::commandList();
  EXPECT_NE(list.find("KEY TAP SWIPE FLASH LS GET PUT"), std::string::npos);
  EXPECT_NE(list.find(" FETCH "), std::string::npos);
}

TEST(FetchHead, WhatPythonsHttpServerSends) {
  HttpHead head;
  const std::string text =
      "HTTP/1.0 200 OK\r\nServer: SimpleHTTP/0.6 Python/3.13.0\r\nDate: Sat, 14 Nov 2026 10:00:00 GMT\r\n"
      "Content-type: application/octet-stream\r\nContent-Length: 6543210\r\n"
      "Last-Modified: Sat, 14 Nov 2026 09:00:00 GMT\r\n\r\nBODY";
  size_t used = 0;
  ASSERT_EQ(feedAll(head, text, &used), HttpHead::State::Done);
  EXPECT_EQ(text.substr(used), "BODY");  // the body starts right after the empty line
  EXPECT_EQ(head.status(), 200);
  EXPECT_TRUE(head.hasLength());
  EXPECT_EQ(head.contentLength(), 6543210u);
  EXPECT_EQ(devlink::checkHead(head, devlink::FETCH_MAX_BYTES), HeadCheck::Ok);
  EXPECT_EQ(head.feed('x'), HttpHead::State::Done);
}

TEST(FetchHead, BareNewlinesAndAnyCaseOfTheName) {
  HttpHead head;
  ASSERT_EQ(feedAll(head, "HTTP/1.1 200\ncontent-LENGTH:   42  \n\n"), HttpHead::State::Done);
  EXPECT_EQ(head.contentLength(), 42u);
  EXPECT_EQ(devlink::checkHead(head, 42), HeadCheck::Ok);
  EXPECT_EQ(devlink::checkHead(head, 41), HeadCheck::TooLarge);
}

TEST(FetchHead, RedirectsAndErrorsAreNotTaken) {
  for (const char* status : {"301 Moved Permanently", "302 Found", "307 Temporary Redirect", "308 Permanent Redirect",
                             "404 File not found", "206 Partial Content", "500 Oops", "100 Continue"}) {
    HttpHead head;
    const std::string text =
        std::string("HTTP/1.1 ") + status + "\r\nLocation: http://8.8.8.8/a.bin\r\nContent-Length: 10\r\n\r\n";
    ASSERT_EQ(feedAll(head, text), HttpHead::State::Done) << status;
    EXPECT_EQ(devlink::checkHead(head, devlink::FETCH_MAX_BYTES), HeadCheck::NotOk200) << status;
  }
}

TEST(FetchHead, TheLengthMustBeThereAndFit) {
  {
    HttpHead head;
    ASSERT_EQ(feedAll(head, "HTTP/1.0 200 OK\r\nServer: x\r\n\r\n"), HttpHead::State::Done);
    EXPECT_EQ(devlink::checkHead(head, devlink::FETCH_MAX_BYTES), HeadCheck::NoLength);
  }
  {
    HttpHead head;
    ASSERT_EQ(feedAll(head, "HTTP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n"), HttpHead::State::Done);
    EXPECT_EQ(devlink::checkHead(head, devlink::FETCH_MAX_BYTES), HeadCheck::Empty);
  }
  {
    HttpHead head;
    ASSERT_EQ(feedAll(head, "HTTP/1.0 200 OK\r\nContent-Length: 8323073\r\n\r\n"), HttpHead::State::Done);
    EXPECT_EQ(devlink::checkHead(head, devlink::FETCH_MAX_BYTES), HeadCheck::TooLarge);  // the slot + 1
  }
  {
    HttpHead head;
    ASSERT_EQ(feedAll(head, "HTTP/1.0 200 OK\r\nContent-Length: 8323072\r\n\r\n"), HttpHead::State::Done);
    EXPECT_EQ(devlink::checkHead(head, devlink::FETCH_MAX_BYTES), HeadCheck::Ok);
  }
  {
    HttpHead head;
    ASSERT_EQ(feedAll(head, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n"),
              HttpHead::State::Done);
    EXPECT_EQ(devlink::checkHead(head, devlink::FETCH_MAX_BYTES), HeadCheck::Encoded);
  }
}

TEST(FetchHead, ABadLengthIsAnError) {
  for (const char* value : {"-1", "12a", "1 2", "", "0x10", "4294967296", "99999999999", "1.5", "+5"}) {
    HttpHead head;
    const std::string text = std::string("HTTP/1.0 200 OK\r\nContent-Length: ") + value + "\r\n\r\n";
    EXPECT_EQ(feedAll(head, text), HttpHead::State::Bad) << value;
  }
  HttpHead twice;
  EXPECT_EQ(feedAll(twice, "HTTP/1.0 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n"), HttpHead::State::Bad);
  HttpHead same;
  EXPECT_EQ(feedAll(same, "HTTP/1.0 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n"), HttpHead::State::Done);
  EXPECT_EQ(same.contentLength(), 5u);
}

TEST(FetchHead, NotAnHttpAnswer) {
  for (const char* first : {"SSH-2.0-OpenSSH_9.9", "220 ftp ready", "HTTP/2 200", "HTTP/1.1 20 OK", "HTTP/1.1 2000 OK",
                            "HTTP/1.1  200 OK", "http/1.1 200 OK", "", "<html>"}) {
    HttpHead head;
    EXPECT_EQ(feedAll(head, std::string(first) + "\r\nContent-Length: 5\r\n\r\n"), HttpHead::State::Bad) << first;
    EXPECT_EQ(head.feed('\n'), HttpHead::State::Bad);
  }
  HttpHead longStatus;
  EXPECT_EQ(feedAll(longStatus, "HTTP/1.1 200 " + std::string(HttpHead::LINE_MAX, 'x') + "\r\n\r\n"),
            HttpHead::State::Bad);
}

TEST(FetchHead, LongHeaderLinesAndAnEndlessHead) {
  HttpHead head;
  const std::string text = "HTTP/1.0 200 OK\r\nX-Long: " + std::string(1000, 'y') + "\r\nContent-Length: 7\r\n\r\n";
  ASSERT_EQ(feedAll(head, text), HttpHead::State::Done);
  EXPECT_EQ(head.contentLength(), 7u);

  // A header line that was cut is still known by its name.
  HttpHead encoded;
  const std::string longEncoding =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: " + std::string(500, 'z') + "\r\nContent-Length: 7\r\n\r\n";
  ASSERT_EQ(feedAll(encoded, longEncoding), HttpHead::State::Done);
  EXPECT_TRUE(encoded.encoded());

  HttpHead longLength;
  EXPECT_EQ(feedAll(longLength, "HTTP/1.0 200 OK\r\nContent-Length: " + std::string(300, ' ') + "7\r\n\r\n"),
            HttpHead::State::Bad);

  HttpHead endless;
  std::string many = "HTTP/1.0 200 OK\r\n";
  while (many.size() <= HttpHead::HEAD_MAX) many += "X-Filler: abcdefghijklmnopqrstuvwxyz\r\n";
  EXPECT_EQ(feedAll(endless, many), HttpHead::State::Bad);

  HttpHead noLineEnd;
  EXPECT_EQ(feedAll(noLineEnd, std::string(HttpHead::HEAD_MAX + 1, 'a')), HttpHead::State::Bad);
  HttpHead cut;
  EXPECT_EQ(feedAll(cut, "HTTP/1.0 200 OK\r\nContent-Le"), HttpHead::State::More);
}

}  // namespace
