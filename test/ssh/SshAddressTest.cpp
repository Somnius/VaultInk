#include <gtest/gtest.h>

#include <string>

#include "SshAddress.h"

using sshaddr::Address;

namespace {
struct Good {
  const char* text;
  const char* host;
  uint16_t port;
  const char* path;
};
}  // namespace

TEST(SshAddress, ParsesWhatAUserTypes) {
  const Good cases[] = {
      {"nas.local", "nas.local", 22, ""},
      {"nas.local:2222", "nas.local", 2222, ""},
      {"192.168.1.10", "192.168.1.10", 22, ""},
      {"nas.local/srv/books", "nas.local", 22, "/srv/books"},
      {"nas.local:2222/srv/books/", "nas.local", 2222, "/srv/books"},
      {"nas.local/", "nas.local", 22, "/"},
      {"ssh://nas.local", "nas.local", 22, ""},
      {"SFTP://nas.local:22/home/me", "nas.local", 22, "/home/me"},
      {"  nas.local  ", "nas.local", 22, ""},
      {"[2001:db8::1]", "2001:db8::1", 22, ""},
      {"[2001:db8::1]:2222/data", "2001:db8::1", 2222, "/data"},
      {"nas.local/a/./b//c", "nas.local", 22, "/a/b/c"},
      {"nas.local/books/\xCE\xB5\xCE\xBB", "nas.local", 22, "/books/\xCE\xB5\xCE\xBB"},
  };
  for (const Good& c : cases) {
    Address a;
    ASSERT_TRUE(sshaddr::parse(c.text, a)) << c.text;
    EXPECT_EQ(a.host, c.host) << c.text;
    EXPECT_EQ(a.port, c.port) << c.text;
    EXPECT_EQ(a.path, c.path) << c.text;
  }
}

TEST(SshAddress, RefusesTheRest) {
  const char* bad[] = {"",
                       "   ",
                       "me@nas.local",  // the user name has its own field
                       "ssh://me:secret@nas.local",
                       "nas.local:0",
                       "nas.local:65536",
                       "nas.local:22x",
                       "nas.local:",
                       ":22",
                       "/srv/books",
                       "nas local",
                       "nas.local;rm",
                       "2001:db8::1",  // IPv6 needs the brackets
                       "[2001:db8::1",
                       "[]:22",
                       "[2001:db8::1]22",
                       "nas.local/a/../../etc",  // climbs above the top
                       "nas.local/a\nb",
                       "https://nas.local"};
  for (const char* text : bad) {
    Address a;
    a.host = "untouched";
    EXPECT_FALSE(sshaddr::parse(text, a)) << text;
    EXPECT_EQ(a.host, "untouched") << text;
  }
}

TEST(SshAddress, Display) {
  Address a;
  ASSERT_TRUE(sshaddr::parse("nas.local", a));
  EXPECT_EQ(sshaddr::display(a), "nas.local");
  ASSERT_TRUE(sshaddr::parse("nas.local:2222/x", a));
  EXPECT_EQ(sshaddr::display(a), "nas.local:2222");
  ASSERT_TRUE(sshaddr::parse("[::1]:2222", a));
  EXPECT_EQ(sshaddr::display(a), "[::1]:2222");
}

TEST(SshAddress, FileModes) {
  EXPECT_TRUE(sshaddr::isDirMode(040755));
  EXPECT_FALSE(sshaddr::isDirMode(0100644));
  EXPECT_TRUE(sshaddr::isFileMode(0100644));
  EXPECT_FALSE(sshaddr::isFileMode(0120777));  // a symbolic link is neither
  EXPECT_FALSE(sshaddr::isDirMode(0120777));
  EXPECT_FALSE(sshaddr::isFileMode(0));
}

TEST(SshAddress, RemotePathStaysUnderTheStartFolder) {
  std::string out;
  ASSERT_TRUE(sshaddr::remotePath("/home/me", "/", out));
  EXPECT_EQ(out, "/home/me");
  ASSERT_TRUE(sshaddr::remotePath("/home/me", "/books/new", out));
  EXPECT_EQ(out, "/home/me/books/new");
  ASSERT_TRUE(sshaddr::remotePath("/", "/books", out));
  EXPECT_EQ(out, "/books");
  ASSERT_TRUE(sshaddr::remotePath("/", "", out));
  EXPECT_EQ(out, "/");
  ASSERT_TRUE(sshaddr::remotePath("/home/me/", "/a/../b", out));
  EXPECT_EQ(out, "/home/me/b");
  EXPECT_FALSE(sshaddr::remotePath("/home/me", "/../other", out));
  EXPECT_FALSE(sshaddr::remotePath("home/me", "/", out));
  EXPECT_FALSE(sshaddr::remotePath("", "/", out));
}

TEST(SshAddress, ADownloadStopsAtItsBound) {
  using sshaddr::downloadMayTake;
  using sshaddr::MAX_DOWNLOAD_BYTES;
  // The size is known: up to it, and not one byte more.
  EXPECT_TRUE(downloadMayTake(0, 8192, 100000));
  EXPECT_TRUE(downloadMayTake(91808, 8192, 100000));  // ends exactly at the size
  EXPECT_FALSE(downloadMayTake(91809, 8192, 100000));
  EXPECT_FALSE(downloadMayTake(100000, 1, 100000));
  EXPECT_TRUE(downloadMayTake(100000, 0, 100000));
  EXPECT_FALSE(downloadMayTake(100001, 0, 100000));
  EXPECT_TRUE(downloadMayTake(0, 1, 1));
  EXPECT_FALSE(downloadMayTake(0, 2, 1));
  // The size is unknown: the hard cap.
  EXPECT_TRUE(downloadMayTake(0, 8192, 0));
  EXPECT_TRUE(downloadMayTake(MAX_DOWNLOAD_BYTES - 8192, 8192, 0));
  EXPECT_FALSE(downloadMayTake(MAX_DOWNLOAD_BYTES - 8191, 8192, 0));
  EXPECT_FALSE(downloadMayTake(MAX_DOWNLOAD_BYTES, 1, 0));
  // A size above the cap does not lift it, and huge numbers do not wrap around.
  EXPECT_FALSE(downloadMayTake(MAX_DOWNLOAD_BYTES, 1, MAX_DOWNLOAD_BYTES * 4));
  EXPECT_FALSE(downloadMayTake(UINT64_MAX, 1, 0));
  EXPECT_FALSE(downloadMayTake(1, UINT64_MAX, 0));
  EXPECT_FALSE(downloadMayTake(UINT64_MAX - 1, UINT64_MAX - 1, 100));
  EXPECT_EQ(MAX_DOWNLOAD_BYTES, 2147483648ull);
}
