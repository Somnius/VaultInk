#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "CardScheduler.h"
#include "CardStore.h"
#include "Flashcards.h"
#include "VaultIndex.h"

// The cards of a vault, found without walking the SD card note by note (Phase 10). Notes whose
// tags include the flashcards tag (`#flashcards`, or `#flashcards/greek/verbs` for the deck
// "greek/verbs") are read once and their cards written to `cards.idx` next to the search index;
// a note that didn't change keeps its block. The search index must be up to date first (it holds
// the tags and the stamps).
namespace cardindex {

constexpr const char* VERSION = "vaultink-cardidx 1";

// /.vaultink/<vault>/cards.idx and cards.tsv, beside the vault's index.tsv.
std::string cardIndexPathFor(const std::string& root);
std::string storePathFor(const std::string& root);

// The decks a note belongs to given its tags ("" = the tag alone), for the flashcards tag `tag`.
// Empty when it isn't a flashcards note.
std::vector<std::string> decksOf(const std::vector<std::string>& tags, std::string_view tag);

struct BuildStats {
  size_t notes = 0;      // notes with the flashcards tag
  size_t rescanned = 0;  // of those, read again
  size_t cards = 0;
};
bool build(const std::string& root, const std::string& indexPath, const std::string& cardsPath, std::string_view tag,
           const vaultindex::IndexFs& fs, BuildStats* stats = nullptr);

struct DeckInfo {
  std::string name;  // "" = cards under the bare tag
  size_t cards = 0;
  size_t due = 0;    // reviewed before and due today or earlier
  size_t fresh = 0;  // never reviewed
};
// Every deck with its counts, sorted by name, from one pass over cards.idx joined with `store`.
std::vector<DeckInfo> listDecks(const std::string& cardsPath, const vaultindex::IndexFs& fs,
                                const cardstore::Store& store, uint32_t today);

struct SessionCard {
  uint64_t id = 0;
  std::string front;
  std::string back;
  uint32_t line = 0;
  std::string notePath;
  cardsched::CardState state;
};
struct SessionSpec {
  bool all = true;       // every deck (then `deck` is ignored)
  std::string deck;      // a deck and the decks below it ("greek" includes "greek/verbs")
  std::string onlyNote;  // only this note's cards (full path) when not empty
  size_t newLimit = 20;
  size_t reviewLimit = 100;
};
// Due reviews (most overdue first, up to reviewLimit) then new cards (up to newLimit, file order).
std::vector<SessionCard> collectSession(const std::string& cardsPath, const vaultindex::IndexFs& fs,
                                        const cardstore::Store& store, uint32_t today, const SessionSpec& spec);

// Ids of every card in cards.idx (for the store's compaction).
bool allIds(const std::string& cardsPath, const vaultindex::IndexFs& fs, const std::function<void(uint64_t)>& id);

}  // namespace cardindex
