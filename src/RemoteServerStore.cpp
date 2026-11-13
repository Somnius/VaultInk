#include "RemoteServerStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>
#include <strings.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace {
std::string obfuscate(const std::string& plain) { return std::string(obfuscation::obfuscateToBase64(plain).c_str()); }
std::string deobfuscate(const char* stored) { return obfuscation::deobfuscateFromBase64(stored); }
}  // namespace

void RemoteServerStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["servers"].to<JsonArray>();
  for (const auto& server : servers) remoteserver::toJson(server, arr.add<JsonObject>(), obfuscate);
}

bool RemoteServerStore::fromJson(JsonVariantConst doc) {
  servers.clear();
  JsonArrayConst arr = doc["servers"].as<JsonArrayConst>();
  servers.reserve(std::min(arr.size(), MAX_SERVERS));
  for (JsonObjectConst obj : arr) {
    if (servers.size() >= MAX_SERVERS) break;
    RemoteServer server = remoteserver::fromJson(obj, deobfuscate);
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
