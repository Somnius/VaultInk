#pragma once

#include <string>
#include <vector>

// Books the reader marked as favourites, newest first: /.crosspoint/favourites.json. Held in RAM
// (at most MAX_FAVOURITES short entries), so "is this a favourite?" never reads the card.
// Saves go through a .tmp file; a file that can't be read is kept as .bad, never overwritten.
class FavouritesStore {
 public:
  static constexpr int MAX_FAVOURITES = 50;

  struct Favourite {
    std::string path;
    std::string title;
    std::string author;
  };

  static FavouritesStore& getInstance() {
    static FavouritesStore instance;
    return instance;
  }

  void load();
  bool contains(const std::string& path) const;
  // Adds at the front (an existing entry moves there); the oldest drops off past the limit.
  void add(const std::string& path, const std::string& title, const std::string& author);
  bool remove(const std::string& path);
  // A book renamed or moved on the card keeps its place.
  void updatePath(const std::string& oldPath, const std::string& newPath);
  const std::vector<Favourite>& getAll() const { return favourites; }

 private:
  FavouritesStore() = default;
  bool save() const;
  std::vector<Favourite> favourites;
};

#define FAVOURITES FavouritesStore::getInstance()
