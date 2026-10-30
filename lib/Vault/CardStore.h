#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "CardScheduler.h"

// Where each flashcard stands (Phase 10), on the SD card outside the vault: a journal with one
// line per answer, `/.vaultink/<vault>/cards.tsv`. Appending keeps a power cut from losing more than
// the last answer; on reading, the newest line for a card wins. When the journal has grown much
// larger than the cards it holds it is rewritten (compact), dropping cards that are gone and
// haven't been seen for 90 days.
namespace cardstore {

struct StoreFs {
  // Streams the lines of a file (without '\n'); false when it can't be opened.
  std::function<bool(const std::string& path, const std::function<bool(std::string_view line)>& line)> lines;
  // Adds bytes to the end of a file, creating it.
  std::function<bool(const std::string& path, std::string_view text)> appendTo;
  // Writes a whole new file: begin, append..., commit(true) replaces the old one, commit(false) drops it.
  std::function<bool(const std::string& path)> begin;
  std::function<bool(std::string_view text)> append;
  std::function<bool(bool keep)> commit;
};

constexpr const char* HEADER = "vaultink-cards 1";
constexpr size_t MAX_CARDS = 20000;
constexpr uint32_t FORGET_AFTER_DAYS = 90;
constexpr size_t COMPACT_BYTES = 1024 * 1024;

struct Entry {
  uint64_t id = 0;
  cardsched::CardState state;
};

std::string toLine(uint64_t id, const cardsched::CardState& state);
bool fromLine(std::string_view line, Entry& out);

class Store {
 public:
  Store(StoreFs fs, std::string path) : fs(std::move(fs)), path(std::move(path)) {}

  // Reads the journal into memory, keeping only the cards `wanted` accepts (null: all of them).
  // A missing file is an empty store; a file with another header is read as empty and rewritten
  // on the first save.
  bool load(const std::function<bool(uint64_t id)>& wanted = nullptr);

  const cardsched::CardState* find(uint64_t id) const;
  size_t size() const { return entries.size(); }

  // Remembers the state and appends it to the journal.
  bool save(uint64_t id, const cardsched::CardState& state);

  // True when the journal read last is over 1 MB: time to rewrite it.
  bool needsCompaction() const { return bytesRead > COMPACT_BYTES; }

  // Rewrites the journal with the newest line per card. A card is dropped when `present` says it
  // is no longer in any note and it was last seen over 90 days before `today`; past MAX_CARDS the
  // least recently seen go too. Reloads everything first, so call it before filtered loads.
  bool compact(uint32_t today, const std::function<bool(uint64_t id)>& present);

 private:
  bool readAll(const std::function<bool(uint64_t)>& wanted, std::vector<Entry>& into);
  bool writeHeader();

  StoreFs fs;
  std::string path;
  std::vector<Entry> entries;  // sorted by id
  bool headerOk = false;
  size_t bytesRead = 0;
};

}  // namespace cardstore
