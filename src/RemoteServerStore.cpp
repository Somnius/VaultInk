#include "RemoteServerStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>
#include <strings.h>

#include <algorithm>
#include <cstring>

namespace {
std::string capped(const char* text) {
  std::string out = text ? text : "";
  if (out.size() > RemoteServerStore::MAX_FIELD_BYTES) out.resize(RemoteServerStore::MAX_FIELD_BYTES);
  return out;
}
}  // namespace

bool RemoteServer::isPlain() const {
  if (protocol == Protocol::Ftp) return true;
  // Anything that is not clearly https counts as not encrypted.
  return !(url.size() >= 8 && strncasecmp(url.c_str(), "https://", 8) == 0);
}

void RemoteServerStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& server : servers) {
    JsonObject obj = arr.add<JsonObject>();
    obj["name"] = server.name;
    obj["protocol"] = server.protocol == RemoteServer::Protocol::Ftp ? "ftp" : "webdav";
    obj["url"] = server.url;
    obj["username"] = server.username;
    obj["password_obf"] = obfuscation::obfuscateToBase64(server.password);
    obj["verifyCert"] = server.verifyCert;
  }
}

bool RemoteServerStore::fromJson(JsonVariantConst doc) {
  servers.clear();
  JsonArrayConst arr = doc["servers"].as<JsonArrayConst>();
  servers.reserve(std::min(arr.size(), MAX_SERVERS));
  for (JsonObjectConst obj : arr) {
    if (servers.size() >= MAX_SERVERS) break;
    RemoteServer server;
    server.name = capped(obj["name"] | "");
    server.url = capped(obj["url"] | "");
    server.username = capped(obj["username"] | "");
    server.password = obfuscation::deobfuscateFromBase64(obj["password_obf"] | "");
    if (server.password.size() > MAX_FIELD_BYTES) server.password.clear();
    server.protocol =
        strcmp(obj["protocol"] | "webdav", "ftp") == 0 ? RemoteServer::Protocol::Ftp : RemoteServer::Protocol::WebDav;
    // A file without the field (or edited by hand) gets the safe value.
    server.verifyCert = obj["verifyCert"] | true;
    servers.push_back(std::move(server));
  }
  LOG_DBG("RMT", "Loaded %zu file servers", servers.size());
  return true;
}

bool RemoteServerStore::addServer(const RemoteServer& server) {
  if (servers.size() >= MAX_SERVERS) return false;
  servers.push_back(server);
  return saveToFile();
}

bool RemoteServerStore::updateServer(const size_t index, const RemoteServer& server) {
  if (index >= servers.size()) return false;
  servers[index] = server;
  return saveToFile();
}

bool RemoteServerStore::removeServer(const size_t index) {
  if (index >= servers.size()) return false;
  servers.erase(servers.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const RemoteServer* RemoteServerStore::getServer(const size_t index) const {
  return index < servers.size() ? &servers[index] : nullptr;
}
