#include "LiveSyncPull.h"

#include <cstring>

namespace livesync {
namespace {

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

// Generation-13 features (livesync-commonlib remoteFeatureCompatibility.ts).
bool knownFeature(const char* f) {
  return std::strcmp(f, "encrypted-internal-metadata-v1") == 0 || std::strcmp(f, "independent-id-derivation-v1") == 0 ||
         std::strcmp(f, "nfc-path-identity-v1") == 0;
}

// Compressed payloads start with "\x0ELZ\x1D" (commonlib compress.ts); VaultInk doesn't inflate.
bool compressed(const std::string& s) {
  return s.size() >= 4 && std::memcmp(s.data(),
                                      "\x0EL"
                                      "Z\x1D",
                                      4) == 0;
}

}  // namespace

bool remoteVersionSupported(JsonObjectConst versionDoc, std::string* why) {
  const int version = versionDoc["version"] | 0;
  if (version <= 0 || version > MAX_REMOTE_VERSION) {
    if (why) *why = "remote version " + std::to_string(version);
    return false;
  }
  for (JsonVariantConst f : versionDoc["used_features"].as<JsonArrayConst>()) {
    const char* name = f | "";
    if (!knownFeature(name)) {
      if (why) *why = std::string("remote feature ") + name;
      return false;
    }
  }
  return true;
}

Result readFileDoc(JsonObjectConst doc, const Key master, FileEntry& out, DocKind& kind) {
  kind = DocKind::Skip;
  out = FileEntry{};
  const std::string id = doc["_id"] | "";
  const std::string type = doc["type"] | "";
  if (id.empty() || id[0] == '_' || startsWith(id, "h:") || startsWith(id, "i:") || startsWith(id, "ix:") ||
      startsWith(id, "ps:") || id == "obsydian_livesync_version" || id == "syncinfo") {
    return Result::Ok;
  }
  if (type != "plain" && type != "newnote") return Result::Ok;
  out.id = id;
  out.binary = type == "newnote";
  out.deleted = (doc["deleted"] | false) || (doc["_deleted"] | false);
  std::string path = doc["path"] | "";
  if (startsWith(path, "/\\:")) {
    // Obfuscated: path, times, size and the chunk list are all inside the encrypted field.
    std::string json;
    const Result r = decryptMetaPath(master, path, json);
    if (r != Result::Ok) return r;
    JsonDocument meta;
    if (deserializeJson(meta, json) != DeserializationError::Ok) return Result::BadFormat;
    out.path = meta["path"] | "";
    out.mtime = meta["mtime"] | static_cast<int64_t>(0);
    out.size = meta["size"] | static_cast<uint64_t>(0);
    for (JsonVariantConst c : meta["children"].as<JsonArrayConst>()) out.children.emplace_back(c | "");
  } else {
    out.path = path;
    out.mtime = doc["mtime"] | static_cast<int64_t>(0);
    out.size = doc["size"] | static_cast<uint64_t>(0);
    for (JsonVariantConst c : doc["children"].as<JsonArrayConst>()) out.children.emplace_back(c | "");
  }
  if (out.path.empty()) return Result::BadFormat;
  kind = DocKind::File;
  return Result::Ok;
}

Result assemble(const FileEntry& file, const Key master, const ChunkGetter& chunk, const ByteSink& sink) {
  std::string stored, piece, bytes;
  for (const auto& id : file.children) {
    bool encrypted = false;
    if (!chunk(id, stored, encrypted)) return Result::BadFormat;
    if (encrypted) {
      const Result r = decrypt(master, stored, piece);
      if (r != Result::Ok) return r;
    } else {
      piece.swap(stored);
    }
    if (compressed(piece)) return Result::Unsupported;
    if (file.binary) {
      if (!base64Decode(piece, bytes)) return Result::BadFormat;
      if (!sink(bytes.data(), bytes.size())) return Result::BadFormat;
    } else if (!sink(piece.data(), piece.size())) {
      return Result::BadFormat;
    }
  }
  return Result::Ok;
}

}  // namespace livesync
