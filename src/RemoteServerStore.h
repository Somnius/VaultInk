#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>
#include <RemoteServer.h>

#include <string>
#include <vector>

/**
 * The WebDAV, FTP and SSH servers (struct RemoteServer, lib/RemoteFiles/RemoteServer.h), kept on
 * the SD card like the OPDS servers: the password is XOR-obfuscated with the device's MAC address
 * and base64-encoded. That stops a casual look, not a reader of the card who also has the device
 * (docs/webdav-ftp.md says so). An SSH server has no saved password at all (docs/ssh.md).
 */
class RemoteServerStore : public PersistableStore<RemoteServerStore> {
 private:
  std::vector<RemoteServer> servers;

  RemoteServerStore() = default;
  friend class PersistableStore<RemoteServerStore>;

 public:
  static constexpr size_t MAX_SERVERS = 6;
  static constexpr size_t MAX_FIELD_BYTES = remoteserver::MAX_FIELD_BYTES;

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
