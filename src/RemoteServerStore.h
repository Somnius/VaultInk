#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

// A file server the user set up: WebDAV (http/https) or FTP (plain text).
struct RemoteServer {
  enum class Protocol : uint8_t { WebDav = 0, Ftp = 1 };

  std::string name;
  std::string url;  // WebDAV: "https://host/path"; FTP: "host[:port][/path]" or "ftp://..."
  std::string username;
  std::string password;  // plain text in memory; obfuscated with the hardware key on the card
  Protocol protocol = Protocol::WebDav;
  // WebDAV over https: check the server's certificate (lib/TlsPolicy). On for a new server.
  bool verifyCert = true;

  // The connection is not encrypted: FTP always, WebDAV when the address is http://.
  bool isPlain() const;
};

/**
 * The WebDAV and FTP servers, kept on the SD card like the OPDS servers: the password is
 * XOR-obfuscated with the device's MAC address and base64-encoded. That stops a casual look, not
 * a reader of the card who also has the device (docs/webdav-ftp.md says so).
 */
class RemoteServerStore : public PersistableStore<RemoteServerStore> {
 private:
  std::vector<RemoteServer> servers;

  RemoteServerStore() = default;
  friend class PersistableStore<RemoteServerStore>;

 public:
  static constexpr size_t MAX_SERVERS = 6;
  static constexpr size_t MAX_FIELD_BYTES = 127;

  static const char* getFilePath() { return "/.crosspoint/remotes.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool addServer(const RemoteServer& server);
  bool updateServer(size_t index, const RemoteServer& server);
  bool removeServer(size_t index);

  const std::vector<RemoteServer>& getServers() const { return servers; }
  const RemoteServer* getServer(size_t index) const;
  size_t getCount() const { return servers.size(); }
};

#define REMOTE_STORE RemoteServerStore::getInstance()
