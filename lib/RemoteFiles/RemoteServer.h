#pragma once

#include <ArduinoJson.h>
#include <strings.h>

#include <cstdint>
#include <cstring>
#include <string>

// A file or shell server the user set up: WebDAV (http/https), FTP (plain text) or SSH (the SFTP
// file browser and the terminal). The struct and its JSON form; no platform code, so the round
// trip is tested on the computer.
struct RemoteServer {
  enum class Protocol : uint8_t { WebDav = 0, Ftp = 1, Sftp = 2 };

  std::string name;
  // WebDAV: "https://host/path"; FTP: "host[:port][/path]" or "ftp://..."; SSH: "host[:port][/path]".
  std::string url;
  std::string username;
  // WebDAV and FTP: plain text in memory, obfuscated with the hardware key on the card.
  // SSH: typed at every connection, held only while that connection is being made, never saved.
  std::string password;
  Protocol protocol = Protocol::WebDav;
  // WebDAV over https: check the server's certificate (lib/TlsPolicy). On for a new server.
  bool verifyCert = true;
  // SSH: sign in with the device's own Ed25519 key instead of a password.
  bool keyAuth = false;

  bool isSsh() const { return protocol == Protocol::Sftp; }
  // The connection is not encrypted: FTP always, WebDAV when the address is http://. Never SSH.
  bool isPlain() const {
    if (protocol == Protocol::Sftp) return false;
    if (protocol == Protocol::Ftp) return true;
    // Anything that is not clearly https counts as not encrypted.
    return !(url.size() >= 8 && strncasecmp(url.c_str(), "https://", 8) == 0);
  }
};

namespace remoteserver {

inline constexpr size_t MAX_FIELD_BYTES = 127;

inline const char* protocolKey(const RemoteServer::Protocol protocol) {
  switch (protocol) {
    case RemoteServer::Protocol::Ftp:
      return "ftp";
    case RemoteServer::Protocol::Sftp:
      return "sftp";
    case RemoteServer::Protocol::WebDav:
    default:
      return "webdav";
  }
}

// A name this version does not know (a file from a newer version, or edited by hand) is WebDAV, as
// before SSH existed.
inline RemoteServer::Protocol protocolFromKey(const char* key) {
  if (key && strcmp(key, "ftp") == 0) return RemoteServer::Protocol::Ftp;
  if (key && strcmp(key, "sftp") == 0) return RemoteServer::Protocol::Sftp;
  return RemoteServer::Protocol::WebDav;
}

inline std::string capped(const char* text) {
  std::string out = text ? text : "";
  if (out.size() > MAX_FIELD_BYTES) {
    // Cut on a character boundary, not in the middle of a UTF-8 sequence.
    size_t cut = MAX_FIELD_BYTES;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
    out.resize(cut);
  }
  return out;
}

using Obfuscate = std::string (*)(const std::string& plain);
using Deobfuscate = std::string (*)(const char* stored);

// One server into `obj`. An SSH server's password is never written, whatever the struct holds.
inline void toJson(const RemoteServer& server, JsonObject obj, const Obfuscate obfuscate) {
  obj["name"] = server.name;
  obj["protocol"] = protocolKey(server.protocol);
  obj["url"] = server.url;
  obj["username"] = server.username;
  obj["password_obf"] = server.isSsh() ? std::string() : obfuscate(server.password);
  obj["verifyCert"] = server.verifyCert;
  if (server.isSsh()) obj["keyAuth"] = server.keyAuth;
}

inline RemoteServer fromJson(JsonObjectConst obj, const Deobfuscate deobfuscate) {
  RemoteServer server;
  server.name = capped(obj["name"] | "");
  server.url = capped(obj["url"] | "");
  server.username = capped(obj["username"] | "");
  server.protocol = protocolFromKey(obj["protocol"] | "webdav");
  // An SSH password is not read even if a file has one.
  if (!server.isSsh()) {
    server.password = deobfuscate(obj["password_obf"] | "");
    if (server.password.size() > MAX_FIELD_BYTES) server.password.clear();
  }
  // A file without the field (or edited by hand) gets the safe value.
  server.verifyCert = obj["verifyCert"] | true;
  server.keyAuth = server.isSsh() && (obj["keyAuth"] | false);
  return server;
}

}  // namespace remoteserver
