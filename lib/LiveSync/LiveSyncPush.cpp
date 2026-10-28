#include "LiveSyncPush.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <vector>

#include "LiveSyncIds.h"

namespace livesync {
namespace {

std::string encodeId(const std::string& id) {
  static constexpr char kHexDigits[] = "0123456789ABCDEF";
  std::string out;
  for (const char ch : id) {
    const auto c = static_cast<unsigned char>(ch);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHexDigits[c >> 4];
      out += kHexDigits[c & 15];
    }
  }
  return out;
}

// Pieces of up to MAX_PIECE_BYTES, never cutting a UTF-8 sequence.
std::vector<std::string> split(const std::string& text) {
  std::vector<std::string> pieces;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = std::min(text.size(), pos + MAX_PIECE_BYTES);
    while (end < text.size() && end > pos && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;
    if (end == pos) end = std::min(text.size(), pos + MAX_PIECE_BYTES);  // not UTF-8: cut anyway
    pieces.push_back(text.substr(pos, end - pos));
    pos = end;
  }
  return pieces;
}

// POST _bulk_docs with the chunk docs; a chunk the server already has ("conflict") is fine.
bool putChunks(const Transport& net, JsonDocument& bulk, std::string& error) {
  std::string body, response;
  serializeJson(bulk, body);
  const int status = net.request("POST", "_bulk_docs", body, response);
  if (status != 201 && status != 200) {
    error = "The server answered " + std::to_string(status) + " to the note's pieces.";
    return false;
  }
  JsonDocument result;
  if (deserializeJson(result, response) != DeserializationError::Ok) {
    error = "The server's answer to the note's pieces could not be read.";
    return false;
  }
  for (JsonObjectConst row : result.as<JsonArrayConst>()) {
    const std::string err = row["error"] | "";
    if (!err.empty() && err != "conflict") {
      error = "The server refused a piece of the note (" + err + ").";
      return false;
    }
  }
  return true;
}

}  // namespace

PushResult pushNote(const Transport& net, const Key master, const PushIdentity& identity, const RandomFill& random,
                    const PushItem& item, const int64_t nowMs, std::string& newId, std::string& newRev,
                    std::string& error) {
  const std::string id = item.knownId.empty() ? noteId(identity.passDigest, item.path) : item.knownId;
  if (id.empty()) {
    error = "This note's name has letters VaultInk can't send yet.";
    return PushResult::Skipped;
  }
  newId = id;

  // The server's current revision, and the times it holds for the note.
  std::string response;
  const int got = net.request("GET", encodeId(id), "", response);
  std::string serverRev;
  bool serverDeleted = false;
  int64_t ctime = nowMs, lastMtime = 0;
  if (got == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, response) != DeserializationError::Ok) {
      error = "The server's copy of the note could not be read.";
      return PushResult::Failed;
    }
    serverRev = doc["_rev"] | "";
    serverDeleted = (doc["deleted"] | false) || (doc["_deleted"] | false);
    std::string metaJson;
    JsonDocument meta;
    if (decryptMetaPath(master, doc["path"] | "", metaJson) == Result::Ok &&
        deserializeJson(meta, metaJson) == DeserializationError::Ok) {
      ctime = meta["ctime"] | nowMs;
      lastMtime = meta["mtime"] | static_cast<int64_t>(0);
    }
  } else if (got != 404) {
    error = "The server answered " + std::to_string(got) + " when asked for the note.";
    return PushResult::Failed;
  }
  // Only on top of what the device last saw: a note the device made fresh must not replace a live
  // note of the same name, and an edit must not replace a newer server version.
  if (got == 200 && serverRev != item.knownRev && !(item.knownRev.empty() && serverDeleted)) {
    return PushResult::Conflict;
  }
  if (item.deleted && (got == 404 || serverDeleted)) return PushResult::Skipped;

  // A clock behind the server's copy must not make the edit look older than it.
  const int64_t mtime = std::max(nowMs, lastMtime + 1);
  std::vector<std::string> children;
  uint64_t size = 0;
  if (!item.deleted) {
    JsonDocument bulk;
    JsonArray docs = bulk["docs"].to<JsonArray>();
    int inBulk = 0;
    for (const std::string& piece : split(item.content)) {
      const std::string chunk = chunkId(piece, identity.hashedPass);
      children.push_back(chunk);
      std::string data;
      if (!encrypt(master, piece, random, data)) {
        error = "A piece of the note could not be encrypted.";
        return PushResult::Failed;
      }
      JsonObject c = docs.add<JsonObject>();
      c["_id"] = chunk;
      c["data"] = data;
      c["type"] = "leaf";
      c["e_"] = true;
      if (++inBulk == 32) {  // a few KB per request
        if (!putChunks(net, bulk, error)) return PushResult::Failed;
        bulk.clear();
        docs = bulk["docs"].to<JsonArray>();
        inBulk = 0;
      }
    }
    if (inBulk > 0 && !putChunks(net, bulk, error)) return PushResult::Failed;
    size = item.content.size();
  }

  // The metadata, in the plugin's key order, goes inside the encrypted path field.
  JsonDocument meta;
  meta["path"] = item.path;
  meta["mtime"] = mtime;
  meta["ctime"] = ctime;
  meta["size"] = size;
  JsonArray kids = meta["children"].to<JsonArray>();
  for (const auto& c : children) kids.add(c);
  std::string metaJson, pathField;
  serializeJson(meta, metaJson);
  if (!encryptMetaPath(master, metaJson, random, pathField)) {
    error = "The note's name could not be encrypted.";
    return PushResult::Failed;
  }
  JsonDocument note;
  note["_id"] = id;
  if (!serverRev.empty()) note["_rev"] = serverRev;
  note["path"] = pathField;
  note["children"].to<JsonArray>();
  note["ctime"] = 0;
  note["mtime"] = 0;
  note["size"] = 0;
  note["type"] = "plain";
  note["eden"].to<JsonObject>();
  if (item.deleted) note["deleted"] = true;
  std::string body;
  serializeJson(note, body);
  const int put = net.request("PUT", encodeId(id), body, response);
  if (put == 409) return PushResult::Conflict;
  if (put != 201 && put != 200) {
    error = "The server answered " + std::to_string(put) + " when the note was sent.";
    return PushResult::Failed;
  }
  JsonDocument answer;
  if (deserializeJson(answer, response) == DeserializationError::Ok) newRev = answer["rev"] | "";
  return PushResult::Pushed;
}

}  // namespace livesync
