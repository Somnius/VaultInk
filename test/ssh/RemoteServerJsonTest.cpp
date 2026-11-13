#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <string>

#include "RemoteServer.h"

namespace {
// A stand-in for the device's obfuscation: reversible and visibly not the plain text.
std::string obfuscate(const std::string& plain) { return plain.empty() ? std::string() : "obf:" + plain; }
std::string deobfuscate(const char* stored) {
  const std::string s = stored ? stored : "";
  return s.rfind("obf:", 0) == 0 ? s.substr(4) : std::string();
}

RemoteServer roundTrip(const RemoteServer& in, std::string* jsonOut = nullptr) {
  JsonDocument doc;
  remoteserver::toJson(in, doc.to<JsonObject>(), obfuscate);
  std::string json;
  serializeJson(doc, json);
  if (jsonOut) *jsonOut = json;
  JsonDocument back;
  EXPECT_FALSE(deserializeJson(back, json));
  return remoteserver::fromJson(back.as<JsonObjectConst>(), deobfuscate);
}
}  // namespace

TEST(RemoteServerJson, SshServerRoundTrip) {
  RemoteServer in;
  in.name = "NAS \xCE\xBD\xCE\xB1\xCF\x82";
  in.url = "nas.local:2222/srv/books";
  in.username = "lef";
  in.protocol = RemoteServer::Protocol::Sftp;
  in.keyAuth = true;
  std::string json;
  const RemoteServer out = roundTrip(in, &json);
  EXPECT_EQ(out.name, in.name);
  EXPECT_EQ(out.url, in.url);
  EXPECT_EQ(out.username, in.username);
  EXPECT_EQ(out.protocol, RemoteServer::Protocol::Sftp);
  EXPECT_TRUE(out.keyAuth);
  EXPECT_TRUE(out.isSsh());
  EXPECT_FALSE(out.isPlain());
  EXPECT_NE(json.find("\"protocol\":\"sftp\""), std::string::npos);
}

TEST(RemoteServerJson, AnSshPasswordIsNeverWrittenOrRead) {
  RemoteServer in;
  in.url = "nas.local";
  in.username = "lef";
  in.protocol = RemoteServer::Protocol::Sftp;
  in.password = "hunter2";  // held in memory while a connection is made
  std::string json;
  const RemoteServer out = roundTrip(in, &json);
  EXPECT_EQ(json.find("hunter2"), std::string::npos);
  EXPECT_NE(json.find("\"password_obf\":\"\""), std::string::npos);
  EXPECT_TRUE(out.password.empty());

  // A file edited by hand to carry one: still not read.
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"protocol":"sftp","url":"nas.local","password_obf":"obf:hunter2"})"));
  EXPECT_TRUE(remoteserver::fromJson(doc.as<JsonObjectConst>(), deobfuscate).password.empty());
}

TEST(RemoteServerJson, WebDavAndFtpAreAsBefore) {
  RemoteServer dav;
  dav.name = "Cloud";
  dav.url = "https://cloud.example/dav";
  dav.username = "me";
  dav.password = "secret";
  dav.verifyCert = false;
  std::string json;
  RemoteServer out = roundTrip(dav, &json);
  EXPECT_EQ(out.protocol, RemoteServer::Protocol::WebDav);
  EXPECT_EQ(out.password, "secret");
  EXPECT_FALSE(out.verifyCert);
  EXPECT_FALSE(out.keyAuth);
  EXPECT_FALSE(out.isPlain());
  EXPECT_NE(json.find("\"password_obf\":\"obf:secret\""), std::string::npos);
  EXPECT_EQ(json.find("keyAuth"), std::string::npos);  // only SSH servers carry the field

  RemoteServer ftp;
  ftp.url = "ftp.example";
  ftp.protocol = RemoteServer::Protocol::Ftp;
  ftp.password = "p";
  out = roundTrip(ftp);
  EXPECT_EQ(out.protocol, RemoteServer::Protocol::Ftp);
  EXPECT_EQ(out.password, "p");
  EXPECT_TRUE(out.isPlain());
}

TEST(RemoteServerJson, MissingAndUnknownFieldsGetSafeValues) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"url":"http://plain.example"})"));
  RemoteServer out = remoteserver::fromJson(doc.as<JsonObjectConst>(), deobfuscate);
  EXPECT_EQ(out.protocol, RemoteServer::Protocol::WebDav);
  EXPECT_TRUE(out.verifyCert);
  EXPECT_FALSE(out.keyAuth);
  EXPECT_TRUE(out.isPlain());

  // A protocol from a newer version falls back to WebDAV; keyAuth on a non-SSH server is ignored.
  ASSERT_FALSE(deserializeJson(doc, R"({"protocol":"scp","url":"https://x.example","keyAuth":true})"));
  out = remoteserver::fromJson(doc.as<JsonObjectConst>(), deobfuscate);
  EXPECT_EQ(out.protocol, RemoteServer::Protocol::WebDav);
  EXPECT_FALSE(out.keyAuth);

  // An SSH server without the field signs in with a password.
  ASSERT_FALSE(deserializeJson(doc, R"({"protocol":"sftp","url":"nas.local"})"));
  out = remoteserver::fromJson(doc.as<JsonObjectConst>(), deobfuscate);
  EXPECT_TRUE(out.isSsh());
  EXPECT_FALSE(out.keyAuth);
}

TEST(RemoteServerJson, LongFieldsAreCutOnACharacterBoundary) {
  RemoteServer in;
  in.protocol = RemoteServer::Protocol::Sftp;
  in.name = std::string(126, 'a') + "\xCE\xB1" + "tail";  // the Greek letter straddles byte 127
  in.url = std::string(300, 'h');
  const RemoteServer out = roundTrip(in);
  EXPECT_EQ(out.name, std::string(126, 'a'));
  EXPECT_EQ(out.url.size(), remoteserver::MAX_FIELD_BYTES);
}

TEST(RemoteServerJson, ProtocolKeys) {
  EXPECT_STREQ(remoteserver::protocolKey(RemoteServer::Protocol::WebDav), "webdav");
  EXPECT_STREQ(remoteserver::protocolKey(RemoteServer::Protocol::Ftp), "ftp");
  EXPECT_STREQ(remoteserver::protocolKey(RemoteServer::Protocol::Sftp), "sftp");
  EXPECT_EQ(remoteserver::protocolFromKey(nullptr), RemoteServer::Protocol::WebDav);
  EXPECT_EQ(remoteserver::protocolFromKey("SFTP"), RemoteServer::Protocol::WebDav);  // exact names only
}
