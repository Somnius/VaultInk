#include "FavouritesStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace {
constexpr const char* FILE_PATH = "/.crosspoint/favourites.json";
constexpr const char* TMP_PATH = "/.crosspoint/favourites.json.tmp";
constexpr const char* BAD_PATH = "/.crosspoint/favourites.json.bad";
}  // namespace

void FavouritesStore::load() {
  favourites.clear();
  // A save cut off between removing the old file and renaming the new one leaves only the .tmp.
  if (!Storage.exists(FILE_PATH) && Storage.exists(TMP_PATH)) Storage.rename(TMP_PATH, FILE_PATH);
  if (!Storage.exists(FILE_PATH)) return;

  String json = Storage.readFile(FILE_PATH);
  JsonDocument doc;
  if (json.isEmpty() || deserializeJson(doc, json)) {
    // Keep it for a look later instead of writing an empty list over it on the next add.
    LOG_ERR("FAV", "favourites.json unreadable; kept as .bad");
    Storage.remove(BAD_PATH);
    Storage.rename(FILE_PATH, BAD_PATH);
    return;
  }
  for (JsonObjectConst obj : doc["books"].as<JsonArrayConst>()) {
    if (static_cast<int>(favourites.size()) >= MAX_FAVOURITES) break;
    Favourite fav{obj["path"] | "", obj["title"] | "", obj["author"] | ""};
    if (!fav.path.empty() && !contains(fav.path)) favourites.push_back(std::move(fav));
  }
  LOG_DBG("FAV", "%d favourites", static_cast<int>(favourites.size()));
}

bool FavouritesStore::contains(const std::string& path) const {
  return std::any_of(favourites.begin(), favourites.end(), [&](const Favourite& f) { return f.path == path; });
}

void FavouritesStore::add(const std::string& path, const std::string& title, const std::string& author) {
  auto it = std::find_if(favourites.begin(), favourites.end(), [&](const Favourite& f) { return f.path == path; });
  if (it != favourites.end()) {
    std::rotate(favourites.begin(), it, it + 1);
    favourites.front().title = title;
    favourites.front().author = author;
  } else {
    favourites.insert(favourites.begin(), Favourite{path, title, author});
    if (static_cast<int>(favourites.size()) > MAX_FAVOURITES) favourites.resize(MAX_FAVOURITES);
  }
  save();
}

bool FavouritesStore::remove(const std::string& path) {
  auto it = std::find_if(favourites.begin(), favourites.end(), [&](const Favourite& f) { return f.path == path; });
  if (it == favourites.end()) return false;
  favourites.erase(it);
  save();
  return true;
}

void FavouritesStore::updatePath(const std::string& oldPath, const std::string& newPath) {
  auto it = std::find_if(favourites.begin(), favourites.end(), [&](const Favourite& f) { return f.path == oldPath; });
  if (it == favourites.end()) return;
  it->path = newPath;
  save();
}

bool FavouritesStore::save() const {
  JsonDocument doc;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& fav : favourites) {
    JsonObject obj = arr.add<JsonObject>();
    obj["path"] = fav.path;
    obj["title"] = fav.title;
    obj["author"] = fav.author;
  }
  String json;
  serializeJson(doc, json);
  Storage.mkdir("/.crosspoint");
  if (!Storage.writeFile(TMP_PATH, json)) {
    LOG_ERR("FAV", "cannot write favourites");
    return false;
  }
  Storage.remove(FILE_PATH);
  if (!Storage.rename(TMP_PATH, FILE_PATH)) {
    LOG_ERR("FAV", "cannot put favourites in place");
    return false;
  }
  return true;
}
