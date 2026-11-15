#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>

#include "LiveSyncProfiles.h"

using namespace livesync;

namespace {

// An SD card in memory.
struct FakeCard {
  std::map<std::string, std::string> files;
  bool failWrites = false;
  bool failRenames = false;
  ProfileFs fs() {
    ProfileFs f;
    f.exists = [this](const std::string& p) { return files.count(p) > 0; };
    f.readAll = [this](const std::string& p, std::string& out) {
      const auto it = files.find(p);
      if (it == files.end()) return false;
      out = it->second;
      return true;
    };
    f.writeAll = [this](const std::string& p, const std::string& text) {
      if (failWrites) return false;
      files[p] = text;
      return true;
    };
    f.remove = [this](const std::string& p) {
      files.erase(p);
      return true;
    };
    f.rename = [this](const std::string& from, const std::string& to) {
      if (failRenames || !files.count(from) || files.count(to)) return false;
      files[to] = files[from];
      files.erase(from);
      return true;
    };
    return f;
  }
};

// Stands in for the device's obfuscation: reversed text behind a marker.
Obfuscator testObfuscator() {
  Obfuscator o;
  o.hide = [](const std::string& plain) { return "x:" + std::string(plain.rbegin(), plain.rend()); };
  o.reveal = [](const char* encoded, bool* ok) {
    const std::string text = encoded;
    if (text.empty()) return std::string();
    if (text.rfind("x:", 0) != 0) {
      if (ok) *ok = false;
      return std::string();
    }
    return std::string(text.rbegin(), text.rend() - 2);
  };
  return o;
}

Profile sample(const std::string& database, const std::string& vault) {
  Profile p;
  p.serverUrl = "https://couch.example.org";
  p.user = "dummy-user";
  p.password = "dummy-password";
  p.database = database;
  p.saltB64 = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
  for (size_t i = 0; i < sizeof(p.master); ++i) p.master[i] = static_cast<uint8_t>(i * 3 + 1);
  p.vaultName = vault;
  p.lastResult = "Synced: 3 updated, 0 removed";
  p.checkCert = false;
  p.autoHours = 6;
  p.lastAttempt = 1760000000;
  p.hashedPass = "dummy-hashed";
  p.passDigest = "dummy-digest";
  return p;
}

void saveSample(FakeCard& card, const int slot, const std::string& database, const std::string& vault) {
  card.files[profileFile(slot)] = profileToJson(sample(database, vault), testObfuscator());
}

const char* const URI_A = "obsidian://setuplivesync?settings=%25%24AAAA";
const char* const URI_B = "obsidian://setuplivesync?settings=%25%24BBBB";

// The profile.json firmware before profiles wrote (0.76.1 and earlier): the same keys, one file.
const char* const OLD_PROFILE_JSON =
    R"({"server":"https://couch.example.org","user":"dummy-user","password":"x:drowssap-ymmud","database":"vtm",)"
    R"("salt":"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=","key":"x:FEDCBA9876543210fedcba9876543210",)"
    R"("vault":"vtm","last":"Synced: 12 updated, 1 removed","checkCert":true,"autoHours":24,)"
    R"("lastAttempt":1761000000,"pushIdA":"x:a-di","pushIdB":"x:b-di"})";

}  // namespace

TEST(LiveSyncProfiles, ProfileJsonRoundTrip) {
  const Profile in = sample("vtm", "Constantinople By Night");
  const std::string text = profileToJson(in, testObfuscator());
  EXPECT_EQ(text.find("dummy-password"), std::string::npos);  // secrets only go in obfuscated
  Profile out;
  ASSERT_TRUE(profileFromJson(text, testObfuscator(), out));
  EXPECT_EQ(out.serverUrl, in.serverUrl);
  EXPECT_EQ(out.user, in.user);
  EXPECT_EQ(out.password, in.password);
  EXPECT_EQ(out.database, in.database);
  EXPECT_EQ(out.saltB64, in.saltB64);
  EXPECT_EQ(0, memcmp(out.master, in.master, sizeof(in.master)));
  EXPECT_EQ(out.vaultName, "Constantinople By Night");
  EXPECT_EQ(out.lastResult, in.lastResult);
  EXPECT_FALSE(out.checkCert);
  EXPECT_EQ(out.autoHours, 6);
  EXPECT_EQ(out.lastAttempt, 1760000000);
  EXPECT_EQ(out.hashedPass, in.hashedPass);
  EXPECT_EQ(out.passDigest, in.passDigest);
  EXPECT_TRUE(out.canPush());
  EXPECT_FALSE(out.idsV1());
}

TEST(LiveSyncProfiles, IdSchemeOneRoundTrip) {
  Profile in = sample("vtm", "vtm");
  in.hashedPass.clear();
  in.passDigest.clear();
  in.idKey = std::string(64, 'a');
  in.nfcPaths = true;
  Profile out;
  ASSERT_TRUE(profileFromJson(profileToJson(in, testObfuscator()), testObfuscator(), out));
  EXPECT_EQ(out.idKey, in.idKey);
  EXPECT_TRUE(out.nfcPaths);
  EXPECT_TRUE(out.idsV1());
}

TEST(LiveSyncProfiles, ReadsAProfileFromBeforeProfiles) {
  Profile out;
  ASSERT_TRUE(profileFromJson(OLD_PROFILE_JSON, testObfuscator(), out));
  EXPECT_EQ(out.database, "vtm");
  EXPECT_EQ(out.vaultName, "vtm");
  EXPECT_EQ(out.password, "dummy-password");
  EXPECT_EQ(0, memcmp(out.master, "0123456789abcdef0123456789ABCDEF", 32));
  EXPECT_EQ(out.autoHours, 24);
  EXPECT_TRUE(out.checkCert);
  EXPECT_EQ(out.hashedPass, "id-a");
  EXPECT_TRUE(out.canPush());
  // A setup of 0.19.0 had no checkCert, autoHours or push ids: the defaults apply.
  ASSERT_TRUE(profileFromJson(R"({"server":"https://c.example.org","password":"x:wp","database":"d",)"
                              R"("key":"x:FEDCBA9876543210fedcba9876543210","vault":"d"})",
                              testObfuscator(), out));
  EXPECT_TRUE(out.checkCert);
  EXPECT_EQ(out.autoHours, 0);
  EXPECT_FALSE(out.canPush());
}

TEST(LiveSyncProfiles, RefusesABrokenProfile) {
  Profile out;
  EXPECT_FALSE(profileFromJson("not json", testObfuscator(), out));
  EXPECT_FALSE(profileFromJson("{}", testObfuscator(), out));
  // A key from another device (can't be read back), a short key, no folder.
  EXPECT_FALSE(profileFromJson(R"({"server":"https://c","password":"x:p","key":"other-device","vault":"v"})",
                               testObfuscator(), out));
  EXPECT_FALSE(
      profileFromJson(R"({"server":"https://c","password":"x:p","key":"x:short","vault":"v"})", testObfuscator(), out));
  EXPECT_FALSE(profileFromJson(R"({"server":"https://c","password":"x:p","key":"x:FEDCBA9876543210fedcba9876543210"})",
                               testObfuscator(), out));
  EXPECT_TRUE(out.password.empty());
}

TEST(LiveSyncProfiles, FolderNames) {
  EXPECT_TRUE(validVaultFolder("Constantinople By Night"));
  EXPECT_TRUE(validVaultFolder("Lefteris-RPG"));
  EXPECT_TRUE(validVaultFolder("vtm-private-lef"));
  EXPECT_TRUE(validVaultFolder("notes_2026.v2"));
  EXPECT_TRUE(validVaultFolder("Σημειώσεις"));  // letters outside Latin
  EXPECT_TRUE(validVaultFolder("a"));
  EXPECT_TRUE(validVaultFolder(std::string(64, 'a')));
  EXPECT_FALSE(validVaultFolder(std::string(65, 'a')));
  EXPECT_FALSE(validVaultFolder(""));
  EXPECT_FALSE(validVaultFolder("."));
  EXPECT_FALSE(validVaultFolder(".."));
  EXPECT_FALSE(validVaultFolder("a/b"));
  EXPECT_FALSE(validVaultFolder("../Books"));
  EXPECT_FALSE(validVaultFolder("a\\b"));
  EXPECT_FALSE(validVaultFolder(".hidden"));
  EXPECT_FALSE(validVaultFolder("trailing."));
  EXPECT_FALSE(validVaultFolder("trailing "));
  EXPECT_FALSE(validVaultFolder(" leading"));
  for (const char* bad : {"a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b", "a\tb", "a\nb", "a+b", "a(b)", "a$b"}) {
    EXPECT_FALSE(validVaultFolder(bad)) << bad;
  }
}

TEST(LiveSyncProfiles, DefaultFolderIsTheDatabaseNameAsBefore) {
  EXPECT_EQ(defaultVaultFolder("vtm"), "vtm");
  EXPECT_EQ(defaultVaultFolder("vtm-private-lef"), "vtm-private-lef");
  EXPECT_EQ(defaultVaultFolder("notes$(2)+x"), "notes$(2)+x");  // CouchDB allows these; so did 0.76.1
  EXPECT_EQ(defaultVaultFolder("a/b"), "LiveSync");
  EXPECT_EQ(defaultVaultFolder(""), "LiveSync");
  EXPECT_EQ(defaultVaultFolder(".."), "LiveSync");
}

TEST(LiveSyncProfiles, SameFolderIgnoresAsciiCase) {
  EXPECT_TRUE(sameFolder("Lefteris-RPG", "lefteris-rpg"));
  EXPECT_FALSE(sameFolder("Lefteris-RPG", "Lefteris RPG"));
  EXPECT_FALSE(sameFolder("vtm", "vtm2"));
}

TEST(LiveSyncProfiles, MigratesTheSingleProfileToProfileOne) {
  FakeCard card;
  card.files[LEGACY_PROFILE] = OLD_PROFILE_JSON;
  card.files[LEGACY_PENDING] = std::string(URI_A) + "\n";
  const ProfileFs fs = card.fs();
  migrateLegacy(fs);
  EXPECT_EQ(card.files.count(LEGACY_PROFILE), 0u);
  EXPECT_EQ(card.files.count(LEGACY_PENDING), 0u);
  ASSERT_EQ(card.files.count(profileFile(1)), 1u);
  EXPECT_EQ(card.files[profileFile(1)], OLD_PROFILE_JSON);  // byte for byte: nothing lost
  Profile p;
  ASSERT_TRUE(profileFromJson(card.files[profileFile(1)], testObfuscator(), p));
  EXPECT_EQ(p.vaultName, "vtm");
  PendingSetup pending;
  ASSERT_TRUE(readPending(fs, 1, pending));
  EXPECT_EQ(pending.uri, URI_A);
  EXPECT_TRUE(pending.vault.empty());
  const SlotInfo info = slotInfo(fs, 1);
  EXPECT_TRUE(info.configured);
  EXPECT_TRUE(info.pending);
  EXPECT_EQ(info.vault, "vtm");
  EXPECT_EQ(info.lastResult, "Synced: 12 updated, 1 removed");
  EXPECT_EQ(info.autoHours, 24);
  EXPECT_EQ(info.lastAttempt, 1761000000);
  // Again: nothing changes.
  const auto before = card.files;
  migrateLegacy(fs);
  EXPECT_EQ(card.files, before);
}

TEST(LiveSyncProfiles, MigrationNeverLosesTheOldFiles) {
  // The card refuses the move: the old files stay where they were.
  FakeCard card;
  card.files[LEGACY_PROFILE] = OLD_PROFILE_JSON;
  card.files[LEGACY_PENDING] = URI_A;
  card.failRenames = true;
  card.failWrites = true;
  migrateLegacy(card.fs());
  EXPECT_EQ(card.files[LEGACY_PROFILE], OLD_PROFILE_JSON);
  EXPECT_EQ(card.files[LEGACY_PENDING], URI_A);
  EXPECT_EQ(card.files.size(), 2u);
  // Profile 1 already exists (an older firmware ran in between and wrote profile.json again):
  // profile 1 wins and the old file is left alone.
  FakeCard both;
  saveSample(both, 1, "vtm", "Constantinople By Night");
  const std::string kept = both.files[profileFile(1)];
  both.files[LEGACY_PROFILE] = OLD_PROFILE_JSON;
  migrateLegacy(both.fs());
  EXPECT_EQ(both.files[profileFile(1)], kept);
  EXPECT_EQ(both.files[LEGACY_PROFILE], OLD_PROFILE_JSON);
}

TEST(LiveSyncProfiles, ParsesTheSetupBody) {
  SetupRequest r;
  ASSERT_EQ(parseSetupBody(std::string(R"({"uri":")") + URI_A + R"("})", r), SetupError::Ok);
  EXPECT_EQ(r.uri, URI_A);
  EXPECT_TRUE(r.vault.empty());
  EXPECT_EQ(r.profile, 0);
  ASSERT_EQ(
      parseSetupBody(std::string(R"({"uri":")") + URI_A + R"(","vault":"Constantinople By Night","profile":2})", r),
      SetupError::Ok);
  EXPECT_EQ(r.vault, "Constantinople By Night");
  EXPECT_EQ(r.profile, 2);
  // The bare "%$" blob, an empty vault (= the default) and null fields are fine.
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$AAAA","vault":"","profile":null})", r), SetupError::Ok);
  EXPECT_TRUE(r.vault.empty());
  EXPECT_EQ(r.profile, 0);

  EXPECT_EQ(parseSetupBody("", r), SetupError::BadJson);
  EXPECT_EQ(parseSetupBody("{", r), SetupError::BadJson);
  EXPECT_EQ(parseSetupBody("[1]", r), SetupError::BadJson);
  EXPECT_EQ(parseSetupBody("{}", r), SetupError::NotSetupUri);
  EXPECT_EQ(parseSetupBody(R"({"uri":"https://example.org"})", r), SetupError::NotSetupUri);
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$"})", r), SetupError::NotSetupUri);
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$)" + std::string(MAX_SETUP_URI, 'A') + R"("})", r), SetupError::NotSetupUri);
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$AAAA","vault":"../Books"})", r), SetupError::BadVault);
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$AAAA","vault":"a/b"})", r), SetupError::BadVault);
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$AAAA","vault":7})", r), SetupError::BadVault);
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$AAAA","profile":0})", r), SetupError::BadProfile);
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$AAAA","profile":5})", r), SetupError::BadProfile);
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$AAAA","profile":"2"})", r), SetupError::BadProfile);
  EXPECT_EQ(parseSetupBody(R"({"uri":"%$AAAA","profile":1.5})", r), SetupError::BadProfile);
}

TEST(LiveSyncProfiles, AddsUpToFourAndRefusesTheFifth) {
  FakeCard card;
  const ProfileFs fs = card.fs();
  int slot = 0;
  for (int n = 1; n <= MAX_PROFILES; ++n) {
    const std::string uri = std::string(URI_A) + std::to_string(n);
    ASSERT_EQ(acceptSetup(fs, SetupRequest{uri, "Vault " + std::to_string(n), 0}, slot), SetupError::Ok);
    EXPECT_EQ(slot, n);  // the next free one
  }
  EXPECT_EQ(nextFreeSlot(fs), 0);
  EXPECT_EQ(acceptSetup(fs, SetupRequest{URI_B, "Vault 5", 0}, slot), SetupError::Full);
  EXPECT_EQ(slot, 0);
  // Removing one frees its slot; a saved profile counts as taken like a pending one.
  removeSlot(fs, 2);
  saveSample(card, 3, "db3", "Vault 3");
  card.files.erase(pendingFile(3));
  EXPECT_EQ(nextFreeSlot(fs), 2);
  ASSERT_EQ(acceptSetup(fs, SetupRequest{URI_B, "Vault 5", 0}, slot), SetupError::Ok);
  EXPECT_EQ(slot, 2);
  PendingSetup pending;
  ASSERT_TRUE(readPending(fs, 2, pending));
  EXPECT_EQ(pending.uri, URI_B);
  EXPECT_EQ(pending.vault, "Vault 5");
}

TEST(LiveSyncProfiles, TheSameUriTwiceUsesOneSlot) {
  FakeCard card;
  const ProfileFs fs = card.fs();
  int slot = 0;
  ASSERT_EQ(acceptSetup(fs, SetupRequest{URI_A, "", 0}, slot), SetupError::Ok);
  EXPECT_EQ(slot, 1);
  ASSERT_EQ(acceptSetup(fs, SetupRequest{URI_A, "Lefteris-RPG", 0}, slot), SetupError::Ok);
  EXPECT_EQ(slot, 1);  // the folder name was added to the waiting setup
  EXPECT_EQ(slotInfo(fs, 1).vault, "Lefteris-RPG");
  ASSERT_EQ(acceptSetup(fs, SetupRequest{URI_B, "", 0}, slot), SetupError::Ok);
  EXPECT_EQ(slot, 2);
}

TEST(LiveSyncProfiles, AProfileNumberReplacesThatSlotsPendingSetupOnly) {
  FakeCard card;
  const ProfileFs fs = card.fs();
  saveSample(card, 1, "vtm", "Constantinople By Night");
  const std::string saved = card.files[profileFile(1)];
  int slot = 0;
  ASSERT_EQ(acceptSetup(fs, SetupRequest{URI_A, "", 1}, slot), SetupError::Ok);
  EXPECT_EQ(slot, 1);
  EXPECT_EQ(card.files[profileFile(1)], saved);  // the working profile stays until the new one is unlocked
  EXPECT_TRUE(slotInfo(fs, 1).configured);
  EXPECT_TRUE(slotInfo(fs, 1).pending);
  ASSERT_EQ(acceptSetup(fs, SetupRequest{URI_B, "", 4}, slot), SetupError::Ok);
  EXPECT_EQ(slot, 4);
  EXPECT_EQ(acceptSetup(fs, SetupRequest{URI_B, "", 5}, slot), SetupError::BadProfile);
  EXPECT_EQ(acceptSetup(fs, SetupRequest{"nope", "", 0}, slot), SetupError::NotSetupUri);
  EXPECT_EQ(acceptSetup(fs, SetupRequest{URI_B, "a/b", 0}, slot), SetupError::BadVault);
  card.failWrites = true;
  EXPECT_EQ(acceptSetup(fs, SetupRequest{URI_B, "", 2}, slot), SetupError::WriteFailed);
}

TEST(LiveSyncProfiles, TwoProfilesNeverShareAFolder) {
  FakeCard card;
  const ProfileFs fs = card.fs();
  saveSample(card, 1, "vtm", "Constantinople By Night");
  int slot = 0;
  // Asked for at the web page: a saved profile's folder, in any case of A-Z.
  EXPECT_EQ(acceptSetup(fs, SetupRequest{URI_A, "constantinople by night", 0}, slot), SetupError::DuplicateVault);
  EXPECT_EQ(card.files.count(pendingFile(2)), 0u);
  // The same slot may keep its own folder.
  EXPECT_EQ(acceptSetup(fs, SetupRequest{URI_A, "Constantinople By Night", 1}, slot), SetupError::Ok);
  // A folder another waiting setup asked for is taken too.
  ASSERT_EQ(acceptSetup(fs, SetupRequest{URI_B, "Lefteris-RPG", 0}, slot), SetupError::Ok);
  EXPECT_EQ(slot, 2);
  EXPECT_EQ(acceptSetup(fs, SetupRequest{std::string(URI_B) + "x", "Lefteris-RPG", 0}, slot),
            SetupError::DuplicateVault);
  EXPECT_TRUE(folderTaken(fs, "LEFTERIS-RPG", 3));
  EXPECT_FALSE(folderTaken(fs, "Lefteris-RPG", 2));
  EXPECT_FALSE(folderTaken(fs, "Something Else", 0));
}

TEST(LiveSyncProfiles, TheFolderWhenASetupIsFinished) {
  FakeCard card;
  const ProfileFs fs = card.fs();
  std::string folder;
  // Asked for; else the database's name.
  ASSERT_EQ(resolveVaultFolder(fs, 1, "Constantinople By Night", "vtm", folder), SetupError::Ok);
  EXPECT_EQ(folder, "Constantinople By Night");
  ASSERT_EQ(resolveVaultFolder(fs, 1, "", "vtm", folder), SetupError::Ok);
  EXPECT_EQ(folder, "vtm");
  ASSERT_EQ(resolveVaultFolder(fs, 1, "", "a/b", folder), SetupError::Ok);
  EXPECT_EQ(folder, "LiveSync");
  EXPECT_EQ(resolveVaultFolder(fs, 1, "a/b", "vtm", folder), SetupError::BadVault);
  EXPECT_EQ(resolveVaultFolder(fs, 0, "", "vtm", folder), SetupError::BadProfile);

  saveSample(card, 1, "vtm", "Constantinople By Night");
  // Profile 1 set up again from the same database with no name: it keeps its folder.
  ASSERT_EQ(resolveVaultFolder(fs, 1, "", "vtm", folder), SetupError::Ok);
  EXPECT_EQ(folder, "Constantinople By Night");
  // Pointed at another database: that database's name, not the old folder.
  ASSERT_EQ(resolveVaultFolder(fs, 1, "", "other", folder), SetupError::Ok);
  EXPECT_EQ(folder, "other");
  // Profile 2 can't take profile 1's folder, by name or by default.
  EXPECT_EQ(resolveVaultFolder(fs, 2, "Constantinople By Night", "vtm-private-lef", folder),
            SetupError::DuplicateVault);
  EXPECT_TRUE(folder.empty());
  saveSample(card, 3, "vtm-private-lef", "vtm-private-lef");
  EXPECT_EQ(resolveVaultFolder(fs, 2, "", "vtm-private-lef", folder), SetupError::DuplicateVault);
  ASSERT_EQ(resolveVaultFolder(fs, 2, "Lefteris-RPG", "vtm-private-lef", folder), SetupError::Ok);
  EXPECT_EQ(folder, "Lefteris-RPG");
}

TEST(LiveSyncProfiles, RemoveLeavesTheOthers) {
  FakeCard card;
  const ProfileFs fs = card.fs();
  saveSample(card, 1, "vtm", "Constantinople By Night");
  saveSample(card, 2, "vtm-private-lef", "Lefteris-RPG");
  int slot = 0;
  ASSERT_EQ(acceptSetup(fs, SetupRequest{URI_A, "", 2}, slot), SetupError::Ok);
  removeSlot(fs, 2);
  EXPECT_FALSE(slotInfo(fs, 2).inUse());
  EXPECT_TRUE(slotInfo(fs, 1).configured);
  EXPECT_EQ(slotInfo(fs, 1).vault, "Constantinople By Night");
  removeSlot(fs, 0);
  removeSlot(fs, 9);
  EXPECT_EQ(card.files.size(), 1u);
  EXPECT_FALSE(slotInfo(fs, 0).inUse());
  EXPECT_FALSE(slotInfo(fs, 5).inUse());
}

TEST(LiveSyncProfiles, FilesPerSlot) {
  EXPECT_EQ(profileFile(1), "/.vaultink/livesync/profiles/1.json");
  EXPECT_EQ(pendingFile(4), "/.vaultink/livesync/profiles/4.setup.json");
  for (const SetupError e : {SetupError::BadJson, SetupError::NotSetupUri, SetupError::BadVault, SetupError::BadProfile,
                             SetupError::Full, SetupError::DuplicateVault, SetupError::WriteFailed}) {
    EXPECT_GT(strlen(setupErrorText(e)), 5u);
  }
}
