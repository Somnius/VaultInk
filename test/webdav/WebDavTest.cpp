#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "WebDav.h"

namespace {

std::string golden(const char* name) {
  std::ifstream in(std::string(WEBDAV_GOLDEN_DIR) + "/" + name, std::ios::binary);
  EXPECT_TRUE(in.good()) << name;
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

webdav::Url url(const char* text) {
  webdav::Url u;
  EXPECT_TRUE(webdav::parseUrl(text, u)) << text;
  return u;
}

// Feeds `xml` in pieces of `chunk` bytes, as the network would.
bool parse(webdav::MultistatusParser& p, const std::string& xml, size_t chunk) {
  for (size_t pos = 0; pos < xml.size(); pos += chunk) {
    if (!p.feed(xml.data() + pos, std::min(chunk, xml.size() - pos))) break;
  }
  return p.finish();
}

const remotefiles::Entry* find(webdav::MultistatusParser& p, const std::string& name) {
  for (const auto& e : p.listing().entries) {
    if (e.name == name) return &e;
  }
  return nullptr;
}

}  // namespace

TEST(WebDavUrl, ParsesHttpAndHttps) {
  webdav::Url u;
  ASSERT_TRUE(webdav::parseUrl("https://Cloud.Example.org/remote.php/dav/files/lef/", u));
  EXPECT_TRUE(u.https);
  EXPECT_EQ(u.host, "cloud.example.org");
  EXPECT_EQ(u.port, 443);
  EXPECT_EQ(u.path, "/remote.php/dav/files/lef");
  EXPECT_EQ(u.origin(), "https://cloud.example.org");

  ASSERT_TRUE(webdav::parseUrl("  http://nas.local:8080  ", u));
  EXPECT_FALSE(u.https);
  EXPECT_EQ(u.port, 8080);
  EXPECT_EQ(u.path, "");
  EXPECT_EQ(u.origin(), "http://nas.local:8080");
  EXPECT_EQ(u.hostHeader(), "nas.local:8080");
}

TEST(WebDavUrl, NormalisesThePath) {
  webdav::Url u;
  ASSERT_TRUE(webdav::parseUrl("https://h/a b/%CE%B1/./x/../?q=1#frag", u));
  EXPECT_EQ(u.path, "/a%20b/%CE%B1");
}

TEST(WebDavUrl, RefusesWhatItCannotUse) {
  webdav::Url u;
  EXPECT_FALSE(webdav::parseUrl("", u));
  EXPECT_FALSE(webdav::parseUrl("cloud.example.org/dav", u));
  EXPECT_FALSE(webdav::parseUrl("ftp://host/", u));
  EXPECT_FALSE(webdav::parseUrl("https://user:secret@host/dav", u));
  EXPECT_FALSE(webdav::parseUrl("https://[::1]/dav", u));
  EXPECT_FALSE(webdav::parseUrl("https://host:0/", u));
  EXPECT_FALSE(webdav::parseUrl("https://host:70000/", u));
  EXPECT_FALSE(webdav::parseUrl("https://host:8a/", u));
  EXPECT_FALSE(webdav::parseUrl("https://ho st/", u));
  EXPECT_FALSE(webdav::parseUrl("https://host/a/%zz", u));
  EXPECT_FALSE(webdav::parseUrl("https://host/a/%00", u));
  EXPECT_FALSE(webdav::parseUrl("https://host/../up", u));
  EXPECT_FALSE(webdav::parseUrl("https://host\r\nX: y/", u));
}

TEST(WebDavPaths, PercentCoding) {
  std::string out;
  EXPECT_TRUE(webdav::percentDecode("a%20b%2Fc%ce%b1", out));
  EXPECT_EQ(out, "a b/c\xCE\xB1");
  EXPECT_FALSE(webdav::percentDecode("abc%2", out));
  EXPECT_FALSE(webdav::percentDecode("%G0", out));
  EXPECT_FALSE(webdav::percentDecode("%00", out));
  EXPECT_EQ(webdav::encodePath("/a b/\xCE\xB1#?%.txt"), "/a%20b/%CE%B1%23%3F%25.txt");
  EXPECT_EQ(webdav::encodePath("/line\r\nbreak"), "/line%0D%0Abreak");
}

TEST(WebDavPaths, ResourceUrlAndRequestPath) {
  const webdav::Url base = url("https://cloud.example.org/remote.php/dav/files/lef");
  EXPECT_EQ(webdav::resourceUrl(base, "/", true), "https://cloud.example.org/remote.php/dav/files/lef/");
  EXPECT_EQ(webdav::resourceUrl(base, "/Books/Sci Fi", true),
            "https://cloud.example.org/remote.php/dav/files/lef/Books/Sci%20Fi/");
  EXPECT_EQ(webdav::resourceUrl(base, "/Books/a&b.epub", false),
            "https://cloud.example.org/remote.php/dav/files/lef/Books/a%26b.epub");
  EXPECT_EQ(webdav::decodedRequestPath(base, "/"), "/remote.php/dav/files/lef");
  EXPECT_EQ(webdav::decodedRequestPath(base, "/Books/Sci Fi"), "/remote.php/dav/files/lef/Books/Sci Fi");

  const webdav::Url root = url("http://nas.local:8080/");
  EXPECT_EQ(webdav::resourceUrl(root, "/", true), "http://nas.local:8080/");
  EXPECT_EQ(webdav::resourceUrl(root, "/a.txt", false), "http://nas.local:8080/a.txt");
  EXPECT_EQ(webdav::decodedRequestPath(root, "/"), "");
  // A remote path cannot climb out of the base folder.
  EXPECT_EQ(webdav::resourceUrl(base, "/../../etc", false), "https://cloud.example.org/remote.php/dav/files/lef");
}

TEST(WebDavPaths, HrefPath) {
  std::string path;
  bool slash = false;
  ASSERT_TRUE(webdav::hrefPath("/dav/a%20b/", path, slash));
  EXPECT_EQ(path, "/dav/a b");
  EXPECT_TRUE(slash);
  ASSERT_TRUE(webdav::hrefPath("https://host:8443/dav/x.txt?v=2", path, slash));
  EXPECT_EQ(path, "/dav/x.txt");
  EXPECT_FALSE(slash);
  ASSERT_TRUE(webdav::hrefPath("/", path, slash));
  EXPECT_EQ(path, "");
  EXPECT_FALSE(webdav::hrefPath("relative/name", path, slash));
  EXPECT_FALSE(webdav::hrefPath("https://host", path, slash));
  EXPECT_FALSE(webdav::hrefPath("/dav/%zz", path, slash));
  EXPECT_FALSE(webdav::hrefPath("/../x", path, slash));
}

TEST(WebDavRedirect, StaysOnTheHostAndNeverLosesEncryption) {
  const webdav::Url https = url("https://cloud.example.org/dav");
  const webdav::Url http = url("http://cloud.example.org/dav");
  EXPECT_TRUE(webdav::redirectAllowed(https, url("https://cloud.example.org/dav/")));
  EXPECT_TRUE(webdav::redirectAllowed(http, url("https://cloud.example.org/dav/")));
  EXPECT_TRUE(webdav::redirectAllowed(http, url("https://cloud.example.org:8443/dav/")));
  EXPECT_FALSE(webdav::redirectAllowed(https, http));
  EXPECT_FALSE(webdav::redirectAllowed(https, url("https://evil.example.org/dav")));
  EXPECT_FALSE(webdav::redirectAllowed(https, url("https://cloud.example.org:8443/dav")));
  EXPECT_FALSE(webdav::redirectAllowed(http, url("http://cloud.example.org:81/dav")));
  EXPECT_FALSE(webdav::redirectAllowed(http, url("http://other.example.org/dav")));
}

TEST(WebDavRequest, BasicAuthorization) {
  EXPECT_EQ(webdav::base64(""), "");
  EXPECT_EQ(webdav::base64("f"), "Zg==");
  EXPECT_EQ(webdav::base64("fo"), "Zm8=");
  EXPECT_EQ(webdav::base64("foo"), "Zm9v");
  EXPECT_EQ(webdav::base64("foob"), "Zm9vYg==");
  EXPECT_EQ(webdav::basicAuthorization("Aladdin", "open sesame"), "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
  EXPECT_EQ(webdav::basicAuthorization("", "secret"), "");
}

TEST(WebDavRequest, PutHeadHasALengthAndNoChunking) {
  const webdav::Url target = url("https://cloud.example.org:8443/dav/a%20b.epub");
  const std::string head =
      webdav::requestHead("PUT", target, {{"Authorization", "Basic abc"}, {"Content-Type", "application/octet-stream"}},
                          1234, "VaultInk/1");
  EXPECT_EQ(head,
            "PUT /dav/a%20b.epub HTTP/1.1\r\n"
            "Host: cloud.example.org:8443\r\n"
            "User-Agent: VaultInk/1\r\n"
            "Connection: close\r\n"
            "Authorization: Basic abc\r\n"
            "Content-Type: application/octet-stream\r\n"
            "Content-Length: 1234\r\n"
            "\r\n");
  EXPECT_EQ(head.find("chunked"), std::string::npos);
  // An empty file still says its length.
  EXPECT_NE(webdav::requestHead("PUT", target, {}, 0, "ua").find("Content-Length: 0\r\n"), std::string::npos);
  // No length for a request without a body.
  EXPECT_EQ(webdav::requestHead("MKCOL", target, {}, -1, "ua").find("Content-Length"), std::string::npos);
  EXPECT_EQ(webdav::requestHead("GET", url("http://h"), {}, -1, "ua").substr(0, 16), "GET / HTTP/1.1\r\n");
}

TEST(WebDavRequest, NothingCanAddAHeader) {
  const webdav::Url target = url("https://h/x");
  EXPECT_EQ(webdav::requestHead("PUT", target, {{"X", "a\r\nEvil: 1"}}, 1, "ua"), "");
  EXPECT_EQ(webdav::requestHead("PUT", target, {{"X\r\nEvil", "a"}}, 1, "ua"), "");
  EXPECT_EQ(webdav::requestHead("PUT", target, {{"X Y", "a"}}, 1, "ua"), "");
  EXPECT_EQ(webdav::requestHead("PUT", target, {}, 1, "ua\r\nEvil: 1"), "");
  EXPECT_EQ(webdav::requestHead("PUT /x HTTP/1.1\r\nEvil:", target, {}, 1, "ua"), "");
  webdav::Url bad = target;
  bad.path = "/a b";
  EXPECT_EQ(webdav::requestHead("PUT", bad, {}, 1, "ua"), "");
  bad.path = "/a\r\nEvil: 1";
  EXPECT_EQ(webdav::requestHead("PUT", bad, {}, 1, "ua"), "");
}

TEST(WebDavRequest, StatusLine) {
  EXPECT_EQ(webdav::statusOf("HTTP/1.1 201 Created"), 201);
  EXPECT_EQ(webdav::statusOf("HTTP/1.0 404 Not Found\r\n"), 404);
  EXPECT_EQ(webdav::statusOf("HTTP/1.1 207"), 207);
  EXPECT_EQ(webdav::statusOf("HTTP/1.1 20x OK"), 0);
  EXPECT_EQ(webdav::statusOf("HTTP/1.1 2000 OK"), 0);
  EXPECT_EQ(webdav::statusOf("220 ftp ready"), 0);
  EXPECT_EQ(webdav::statusOf(""), 0);
}

TEST(WebDavDate, Rfc1123) {
  EXPECT_EQ(webdav::parseHttpDate("Thu, 01 Jan 1970 00:00:10 GMT"), 10);
  EXPECT_EQ(webdav::parseHttpDate("Sat, 29 Feb 2020 12:34:56 GMT"), 1582979696);
  EXPECT_EQ(webdav::parseHttpDate(" Mon, 12 Jan 2026 10:00:00 GMT\n"), 1768212000);
  EXPECT_EQ(webdav::parseHttpDate("2026-01-12T10:00:00Z"), 0);
  EXPECT_EQ(webdav::parseHttpDate("Mon, 12 Foo 2026 10:00:00 GMT"), 0);
  EXPECT_EQ(webdav::parseHttpDate("Mon, 12 Jan 2026 10:00:00 CET"), 0);
  EXPECT_EQ(webdav::parseHttpDate("Mon, 12 Jan 2026 25:00:00 GMT"), 0);
  EXPECT_EQ(webdav::parseHttpDate(""), 0);
}

TEST(WebDavMultistatus, Nextcloud) {
  const webdav::Url base = url("https://cloud.example.org/remote.php/dav/files/lef");
  for (const size_t chunk : {size_t{1}, size_t{7}, size_t{4096}}) {
    webdav::MultistatusParser p(webdav::decodedRequestPath(base, "/Books"));
    ASSERT_TRUE(parse(p, golden("nextcloud.xml"), chunk)) << chunk;
    EXPECT_TRUE(p.sawSelf());
    EXPECT_EQ(p.responses(), 4u);
    ASSERT_EQ(p.listing().entries.size(), 3u);
    EXPECT_FALSE(p.listing().truncated);

    const auto* dir = find(p, "Sci Fi");
    ASSERT_NE(dir, nullptr);
    EXPECT_TRUE(dir->isDir);
    EXPECT_EQ(dir->size, 0u);
    EXPECT_EQ(dir->mtime, 1768293000);

    const auto* greek = find(p, "Ανάγνωση.epub");
    ASSERT_NE(greek, nullptr);
    EXPECT_FALSE(greek->isDir);
    EXPECT_EQ(greek->size, 1048576u);
    EXPECT_EQ(greek->mtime, 1767225599);

    const auto* big = find(p, "Dune (part 1).epub");
    ASSERT_NE(big, nullptr);
    EXPECT_EQ(big->size, 4294967296ULL);  // over 4 GB: the size is 64-bit
    EXPECT_EQ(big->mtime, 10);
  }
}

TEST(WebDavMultistatus, ApacheModDav) {
  const webdav::Url base = url("http://server.example/dav/");
  webdav::MultistatusParser p(webdav::decodedRequestPath(base, "/"));
  ASSERT_TRUE(parse(p, golden("apache.xml"), 33));
  EXPECT_TRUE(p.sawSelf());
  // The file two levels down and the "..%2f" one are not children of /dav.
  ASSERT_EQ(p.listing().entries.size(), 2u);

  const auto* notes = find(p, "notes.txt");
  ASSERT_NE(notes, nullptr);  // the href inside its lockdiscovery did not replace its own
  EXPECT_FALSE(notes->isDir);
  EXPECT_EQ(notes->size, 42u);
  EXPECT_EQ(notes->mtime, 1582979696);

  const auto* comics = find(p, "comics");
  ASSERT_NE(comics, nullptr);
  EXPECT_TRUE(comics->isDir);
  EXPECT_EQ(find(p, "escape.txt"), nullptr);
  EXPECT_EQ(find(p, "too-deep.cbz"), nullptr);
}

TEST(WebDavMultistatus, BareBonesServer) {
  const webdav::Url base = url("http://nas.local:8080/share");
  webdav::MultistatusParser p(webdav::decodedRequestPath(base, "/"));
  ASSERT_TRUE(parse(p, golden("barebones.xml"), 50));
  EXPECT_TRUE(p.sawSelf());
  ASSERT_EQ(p.listing().entries.size(), 3u);

  const auto* a = find(p, "a.txt");
  ASSERT_NE(a, nullptr);
  EXPECT_FALSE(a->isDir);
  EXPECT_EQ(a->size, 3u);
  EXPECT_EQ(a->mtime, 0);

  // No resourcetype at all: the trailing '/' marks the folder.
  const auto* sub = find(p, "sub");
  ASSERT_NE(sub, nullptr);
  EXPECT_TRUE(sub->isDir);

  const auto* pdf = find(p, "b&c.pdf");
  ASSERT_NE(pdf, nullptr);
  EXPECT_FALSE(pdf->isDir);
  EXPECT_EQ(pdf->size, 2048u);
}

TEST(WebDavMultistatus, NamesThatAreNotSafeAreSkipped) {
  const std::string xml =
      "<D:multistatus xmlns:D=\"DAV:\">"
      "<D:response><D:href>/d/</D:href></D:response>"
      "<D:response><D:href>/d/ok.txt</D:href></D:response>"
      "<D:response><D:href>/d/a%5Cb.txt</D:href></D:response>"
      "<D:response><D:href>/d/a%0Ab.txt</D:href></D:response>"
      "<D:response><D:href>/d/a%3Ab.txt</D:href></D:response>"
      "<D:response><D:href>/d/a%2Fb.txt</D:href></D:response>"
      "<D:response><D:href>/d/%zz</D:href></D:response>"
      "<D:response><D:href>/other/c.txt</D:href></D:response>"
      "<D:response></D:response>"
      "</D:multistatus>";
  webdav::MultistatusParser p("/d");
  ASSERT_TRUE(parse(p, xml, 16));
  ASSERT_EQ(p.listing().entries.size(), 1u);
  EXPECT_EQ(p.listing().entries[0].name, "ok.txt");
  // Backslash, line break and colon names, the broken escape and the empty response.
  EXPECT_EQ(p.listing().skipped, 5u);
}

TEST(WebDavMultistatus, CapsAtFiveHundredEntries) {
  std::string xml = "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/d/</d:href></d:response>";
  for (int i = 0; i < 620; ++i) {
    xml += "<d:response><d:href>/d/f" + std::to_string(i) +
           ".txt</d:href><d:propstat><d:prop><d:getcontentlength>1</d:getcontentlength></d:prop>"
           "<d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>";
  }
  xml += "</d:multistatus>";
  webdav::MultistatusParser p("/d");
  bool stopped = false;
  for (size_t pos = 0; pos < xml.size(); pos += 512) {
    if (!p.feed(xml.data() + pos, std::min<size_t>(512, xml.size() - pos))) {
      stopped = true;
      break;
    }
  }
  EXPECT_TRUE(stopped);  // the reader is told to stop: the rest is never downloaded
  EXPECT_TRUE(p.finish());
  EXPECT_TRUE(p.listing().truncated);
  EXPECT_EQ(p.listing().entries.size(), remotefiles::MAX_ENTRIES);
  EXPECT_EQ(p.error(), webdav::MultistatusParser::Error::None);
}

TEST(WebDavMultistatus, CapsTheBytesRead) {
  webdav::MultistatusParser p("/d");
  const std::string open = "<d:multistatus xmlns:d=\"DAV:\">";
  ASSERT_TRUE(p.feed(open.data(), open.size()));
  const std::string filler = "<d:response><d:x>" + std::string(4000, 'y') + "</d:x></d:response>";
  bool stopped = false;
  for (int i = 0; i < 400 && !stopped; ++i) stopped = !p.feed(filler.data(), filler.size());
  EXPECT_TRUE(stopped);
  EXPECT_TRUE(p.listing().truncated);
  EXPECT_TRUE(p.finish());
}

TEST(WebDavMultistatus, AnHrefOverTheLimitIsSkipped) {
  const std::string xml = "<multistatus xmlns=\"DAV:\"><response><href>/d/" +
                          std::string(webdav::MAX_HREF_BYTES + 10, 'a') +
                          "</href></response><response><href>/d/ok</href></response></multistatus>";
  webdav::MultistatusParser p("/d");
  ASSERT_TRUE(parse(p, xml, 100));
  ASSERT_EQ(p.listing().entries.size(), 1u);
  EXPECT_EQ(p.listing().entries[0].name, "ok");
  EXPECT_EQ(p.listing().skipped, 1u);
}

TEST(WebDavMultistatus, EndlessNestingIsRefused) {
  std::string xml = "<d:multistatus xmlns:d=\"DAV:\">";
  for (int i = 0; i < 200; i++) xml += "<a>";
  webdav::MultistatusParser p("/d");
  EXPECT_FALSE(parse(p, xml, 64));
  EXPECT_EQ(p.error(), webdav::MultistatusParser::Error::Malformed);
}

TEST(WebDavMultistatus, RefusesWhatIsNotAMultistatus) {
  {
    webdav::MultistatusParser p("/d");
    EXPECT_FALSE(parse(p, "<html><body>Sign in</body></html>", 8));
    EXPECT_EQ(p.error(), webdav::MultistatusParser::Error::Malformed);
  }
  {
    webdav::MultistatusParser p("/d");
    EXPECT_FALSE(parse(p, "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/d/a</d:href>", 8));
    EXPECT_EQ(p.error(), webdav::MultistatusParser::Error::Malformed);
    // What was read before the cut is not offered as a whole listing.
  }
  {
    webdav::MultistatusParser p("/d");
    EXPECT_FALSE(parse(p, "", 8));
  }
  {
    webdav::MultistatusParser p("/d");
    EXPECT_FALSE(parse(p, "{\"error\":\"not xml\"}", 8));
  }
}

TEST(WebDavMultistatus, EntityExpansionIsNotFollowed) {
  // With the firmware's expat flags (XML_GE=0) a general entity is never expanded.
  const std::string xml =
      "<?xml version=\"1.0\"?><!DOCTYPE m [<!ENTITY a \"aaaaaaaaaa\"><!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;\">]>"
      "<multistatus xmlns=\"DAV:\"><response><href>/d/&b;</href></response></multistatus>";
  webdav::MultistatusParser p("/d");
  parse(p, xml, 64);
  for (const auto& e : p.listing().entries) EXPECT_LT(e.name.size(), 20u) << e.name;
}
