#include <CaBundle.h>
#include <TlsPolicy.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace tlspolicy;

namespace {

// A card and a clock that count how they were used.
struct Fake {
  int64_t now = 1'800'000'000;
  int64_t afterSync = 0;  // what a sync sets the clock to (0: the sync fails)
  bool caExists = false;
  bool caReads = true;
  std::string caText = "-----BEGIN CERTIFICATE-----\nMIIB\n-----END CERTIFICATE-----\n";
  int syncs = 0;
  int reads = 0;
  int existsCalls = 0;

  Clock clock() {
    Clock c;
    c.now = [this] { return now; };
    c.sync = [this] {
      syncs++;
      if (afterSync != 0) now = afterSync;
      return afterSync != 0;
    };
    return c;
  }
  CaFile caFile() {
    CaFile f;
    f.exists = [this] {
      existsCalls++;
      return caExists;
    };
    f.read = [this](std::string& pem) {
      reads++;
      if (!caReads) return false;
      pem = caText;
      return true;
    };
    return f;
  }
};

Decision run(Fake& fake, const char* url, bool userVerify, std::string& pem) {
  return decide(url, userVerify, fake.clock(), fake.caFile(), pem);
}

}  // namespace

TEST(TlsPolicy, HostOfAUrl) {
  EXPECT_EQ(hostOf("https://api.github.com/repos/x/y/releases/latest"), "api.github.com");
  EXPECT_EQ(hostOf("https://EN.Wikipedia.ORG:443/api"), "en.wikipedia.org");
  EXPECT_EQ(hostOf("https://user:pass@books.example.com:8443/opds?x=1"), "books.example.com");
  EXPECT_EQ(hostOf("https://github.com@evil.example/x"), "evil.example");  // user info is not the host
  EXPECT_EQ(hostOf("https://[2001:db8::1]:8443/x"), "2001:db8::1");
  EXPECT_EQ(hostOf("https://github.com./x"), "github.com");
  EXPECT_EQ(hostOf("https://example.com"), "example.com");
  EXPECT_EQ(hostOf("https://example.com#frag"), "example.com");
  EXPECT_EQ(hostOf("not a url"), "");
  EXPECT_EQ(hostOf(""), "");
}

TEST(TlsPolicy, PublicHostsAreExactNamesOrNamesUnderThem) {
  EXPECT_TRUE(isPublicHost("github.com"));
  EXPECT_TRUE(isPublicHost("api.github.com"));
  EXPECT_TRUE(isPublicHost("release-assets.githubusercontent.com"));
  EXPECT_TRUE(isPublicHost("objects.githubusercontent.com"));
  EXPECT_TRUE(isPublicHost("en.wikipedia.org"));
  EXPECT_TRUE(isPublicHost("el.wikipedia.org"));
  EXPECT_FALSE(isPublicHost("evilgithub.com"));
  EXPECT_FALSE(isPublicHost("github.com.evil.example"));
  EXPECT_FALSE(isPublicHost("wikipedia.org.evil.example"));
  EXPECT_FALSE(isPublicHost("notwikipedia.org"));
  EXPECT_FALSE(isPublicHost("sync.example.com"));
  EXPECT_FALSE(isPublicHost(""));
}

TEST(TlsPolicy, PublicHostsAlwaysVerifyWhateverTheUserSet) {
  for (const bool userVerify : {false, true}) {
    for (const bool ca : {false, true}) {
      EXPECT_EQ(choose("https://el.wikipedia.org/api/rest_v1/page/summary/x", userVerify, ca), Mode::Verify);
      // GitHub is checked only once VERIFY_GITHUB is on; until then it keeps its old encrypted-but-unchecked mode
      // unless the user asked for the check on that server.
      const Mode github =
          VERIFY_GITHUB ? Mode::Verify : (userVerify ? (ca ? Mode::CustomCa : Mode::Verify) : Mode::Insecure);
      EXPECT_EQ(choose("https://api.github.com/repos/a/b", userVerify, ca), github);
      EXPECT_EQ(choose("HTTPS://GitHub.com/a", userVerify, ca), github);
    }
  }
}

TEST(TlsPolicy, SelfHostedStaysUncheckedUntilTheUserTurnsItOn) {
  EXPECT_EQ(choose("https://books.example.com/opds", false, false), Mode::Insecure);
  EXPECT_EQ(choose("https://books.example.com/opds", false, true), Mode::Insecure);  // ca.pem alone changes nothing
  EXPECT_EQ(choose("https://books.example.com/opds", true, false), Mode::Verify);
  EXPECT_EQ(choose("https://books.example.com/opds", true, true), Mode::CustomCa);
  EXPECT_EQ(choose("https://192.168.1.10:5984/db", true, true), Mode::CustomCa);
}

TEST(TlsPolicy, PlainHttpHasNothingToCheck) {
  EXPECT_EQ(choose("http://192.168.1.10/opds", true, true), Mode::Plain);
  EXPECT_EQ(choose("http://github.com/x", false, false), Mode::Plain);
  EXPECT_EQ(choose("ftp://example.com", true, false), Mode::Plain);
}

TEST(TlsPolicy, AnUncheckedRequestTouchesNeitherCardNorClock) {
  Fake fake;
  fake.now = 0;
  fake.caExists = true;
  std::string pem;
  const Decision d = run(fake, "https://books.example.com/opds", false, pem);
  EXPECT_TRUE(d.ok());
  EXPECT_EQ(d.mode, Mode::Insecure);
  EXPECT_FALSE(d.checked());
  EXPECT_EQ(fake.syncs, 0);
  EXPECT_EQ(fake.reads, 0);
  EXPECT_EQ(fake.existsCalls, 0);
}

TEST(TlsPolicy, AClockNear1970IsSyncedBeforeACheckedRequest) {
  Fake fake;
  fake.now = 0;
  fake.afterSync = 1'800'000'000;
  std::string pem;
  const Decision d = run(fake, "https://en.wikipedia.org/api/rest_v1/page/summary/Tea", false, pem);
  EXPECT_TRUE(d.ok());
  EXPECT_EQ(d.mode, Mode::Verify);
  EXPECT_EQ(fake.syncs, 1);
}

TEST(TlsPolicy, AClockThatCannotBeSetStopsTheRequest) {
  Fake fake;
  fake.now = 0;
  std::string pem;
  const Decision d = run(fake, "https://en.wikipedia.org/api/rest_v1/page/summary/Tea", false, pem);
  EXPECT_FALSE(d.ok());
  EXPECT_EQ(d.error, Error::NoClock);
  EXPECT_EQ(d.mode, Mode::Verify);  // never downgraded to an unchecked request
  EXPECT_EQ(fake.syncs, 1);
  EXPECT_STRNE(errorText(d.error), "");
}

TEST(TlsPolicy, AGoodClockIsNotSyncedAgain) {
  Fake fake;
  std::string pem;
  EXPECT_TRUE(run(fake, "https://en.wikipedia.org/a/b", false, pem).ok());
  EXPECT_EQ(fake.syncs, 0);
}

TEST(TlsPolicy, ThePublicHostsIgnoreTheUsersCaFile) {
  Fake fake;
  fake.caExists = true;
  std::string pem;
  const Decision d = run(fake, "https://en.wikipedia.org/x", true, pem);
  EXPECT_EQ(d.mode, Mode::Verify);
  EXPECT_EQ(fake.reads, 0);
  EXPECT_TRUE(pem.empty());
}

TEST(TlsPolicy, TheUsersCaFileReplacesTheBuiltInRoots) {
  Fake fake;
  fake.caExists = true;
  std::string pem;
  const Decision d = run(fake, "https://sync.example.com/users/auth", true, pem);
  EXPECT_TRUE(d.ok());
  EXPECT_EQ(d.mode, Mode::CustomCa);
  EXPECT_EQ(pem, fake.caText);
  // Kept for the next request: not read twice.
  EXPECT_TRUE(run(fake, "https://sync.example.com/syncs/progress", true, pem).ok());
  EXPECT_EQ(fake.reads, 1);
}

TEST(TlsPolicy, ACaFileThatCannotBeReadFailsInsteadOfFallingBack) {
  Fake fake;
  fake.caExists = true;
  fake.caReads = false;
  std::string pem;
  Decision d = run(fake, "https://sync.example.com/users/auth", true, pem);
  EXPECT_FALSE(d.ok());
  EXPECT_EQ(d.error, Error::CaUnreadable);
  EXPECT_EQ(d.mode, Mode::CustomCa);  // not Verify: the built-in roots are not a fallback
  EXPECT_TRUE(pem.empty());
  EXPECT_STRNE(errorText(d.error), "");

  fake.caReads = true;
  fake.caText.clear();  // an empty file is no trust anchor either
  d = run(fake, "https://sync.example.com/users/auth", true, pem);
  EXPECT_EQ(d.error, Error::CaUnreadable);
}

TEST(TlsPolicy, NoCaFileMeansTheBuiltInRoots) {
  Fake fake;
  std::string pem;
  const Decision d = run(fake, "https://sync.example.com/users/auth", true, pem);
  EXPECT_TRUE(d.ok());
  EXPECT_EQ(d.mode, Mode::Verify);
  EXPECT_EQ(fake.reads, 0);
}

TEST(TlsPolicy, MissingHooksFailClosed) {
  std::string pem;
  const Decision d = decide("https://en.wikipedia.org/a", false, Clock{}, CaFile{}, pem);
  EXPECT_EQ(d.error, Error::NoClock);
}

namespace {
int countCerts(const char* pem) {
  int n = 0;
  for (const char* p = pem; (p = strstr(p, "-----BEGIN CERTIFICATE-----")) != nullptr; p++) n++;
  return n;
}
}  // namespace

TEST(CaBundle, ThePublicRootsAreTheTailOfTheWholeBundle) {
  EXPECT_EQ(countCerts(ca_bundle::PEM), 10);
  EXPECT_EQ(countCerts(ca_bundle::publicPem()), 5);
  const char* pub = ca_bundle::publicPem();
  EXPECT_GT(pub, ca_bundle::PEM);
  EXPECT_LT(pub, ca_bundle::PEM + sizeof(ca_bundle::PEM));
  EXPECT_EQ(strncmp(pub, "-----BEGIN CERTIFICATE-----\n", 28), 0);
  // The same bytes, not a second copy.
  EXPECT_EQ(pub + strlen(pub), ca_bundle::PEM + sizeof(ca_bundle::PEM) - 1);
}

TEST(TlsPolicy, ReadwiseIsAlwaysCheckedAgainstEveryBuiltInRoot) {
  EXPECT_TRUE(alwaysChecked("readwise.io"));
  EXPECT_TRUE(alwaysChecked("www.readwise.io"));
  EXPECT_FALSE(alwaysChecked("readwise.io.evil.example"));
  EXPECT_FALSE(alwaysChecked("notreadwise.io"));
  // Not in the public group: the device side then gives it all the built-in roots.
  EXPECT_FALSE(isPublicHost("readwise.io"));
  for (const bool userVerify : {false, true}) {
    for (const bool ca : {false, true}) {
      EXPECT_EQ(choose("https://readwise.io/api/v2/highlights/", userVerify, ca), Mode::Verify);
    }
  }
  // A ca.pem on the card is not asked for and does not replace the roots.
  Fake fake;
  fake.caExists = true;
  std::string pem;
  const Decision d = run(fake, "https://readwise.io/api/v2/highlights/", true, pem);
  EXPECT_TRUE(d.ok());
  EXPECT_EQ(d.mode, Mode::Verify);
  EXPECT_EQ(fake.existsCalls, 0);
  EXPECT_TRUE(pem.empty());
  // No date, no request.
  Fake noClock;
  noClock.now = 0;
  EXPECT_EQ(run(noClock, "https://readwise.io/api/v2/highlights/", false, pem).error, Error::NoClock);
  // A Memos server is the user's own: unchecked until the switch is on.
  EXPECT_EQ(choose("https://memos.example.com/api/v1/memos", false, false), Mode::Insecure);
  EXPECT_EQ(choose("https://memos.example.com/api/v1/memos", true, false), Mode::Verify);
  EXPECT_EQ(choose("https://memos.example.com/api/v1/memos", true, true), Mode::CustomCa);
}

TEST(TlsPolicy, MyMemoryIsAlwaysCheckedAgainstEveryBuiltInRootAndALibreTranslateServerIsTheUsersOwn) {
  const char* const url = "https://api.mymemory.translated.net/get?q=book&langpair=en%7Cel";
  EXPECT_TRUE(alwaysChecked("api.mymemory.translated.net"));
  // Only that one name: not its parent, a sibling, or a look-alike.
  EXPECT_FALSE(alwaysChecked("translated.net"));
  EXPECT_FALSE(alwaysChecked("mymemory.translated.net"));
  EXPECT_FALSE(alwaysChecked("x.api.mymemory.translated.net"));
  EXPECT_FALSE(alwaysChecked("api.mymemory.translated.net.evil.example"));
  // Its root (Amazon Root CA 1) is in the self-hosted group, so it must get every built-in root.
  EXPECT_FALSE(isPublicHost("api.mymemory.translated.net"));
  for (const bool userVerify : {false, true}) {
    for (const bool ca : {false, true}) EXPECT_EQ(choose(url, userVerify, ca), Mode::Verify);
  }
  Fake fake;
  fake.caExists = true;
  std::string pem;
  const Decision d = run(fake, url, false, pem);
  EXPECT_TRUE(d.ok());
  EXPECT_EQ(d.mode, Mode::Verify);
  EXPECT_EQ(fake.existsCalls, 0);
  Fake noClock;
  noClock.now = 0;
  EXPECT_EQ(run(noClock, url, false, pem).error, Error::NoClock);
  // A LibreTranslate server is the user's own: unchecked until its switch is on, plain when it is http.
  EXPECT_EQ(choose("https://lt.example.org/translate", false, false), Mode::Insecure);
  EXPECT_EQ(choose("https://lt.example.org/translate", true, false), Mode::Verify);
  EXPECT_EQ(choose("https://lt.example.org/translate", true, true), Mode::CustomCa);
  EXPECT_EQ(choose("http://192.168.1.20:5000/translate", true, true), Mode::Plain);
}

// The switch is a build flag (-DVAULTINK_VERIFY_GITHUB=1, env:x4pro-dev-verify). CMake builds this file both ways:
// TlsPolicyTest with the default and TlsPolicyVerifyGithubTest with the flag.
TEST(TlsPolicy, GithubIsAlwaysCheckedOnlyWithTheSwitchOn) {
  EXPECT_EQ(alwaysChecked("api.github.com"), VERIFY_GITHUB);
  EXPECT_EQ(alwaysChecked("github.com"), VERIFY_GITHUB);
  EXPECT_EQ(alwaysChecked("objects.githubusercontent.com"), VERIFY_GITHUB);
  EXPECT_FALSE(alwaysChecked("github.com.evil.example"));
  EXPECT_TRUE(alwaysChecked("en.wikipedia.org"));  // never depends on the switch
#if VAULTINK_VERIFY_GITHUB
  EXPECT_TRUE(VERIFY_GITHUB);
#else
  EXPECT_FALSE(VERIFY_GITHUB);  // the default, which every shipped build gets
#endif
}
