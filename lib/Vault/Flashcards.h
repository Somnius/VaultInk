#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Flashcards written in a note (Phase 10): a subset of the Obsidian Spaced Repetition plugin's
// syntax. `Front::Back` (one card), `Front:::Back` (two: both ways), a few lines then a `?` line
// then the answer lines (`??` for both ways), and clozes `{{hidden}}` (plus `**hidden**` when the
// note's frontmatter says `sr-cloze: bold`). `==x==` is a highlight in VaultInk, never a cloze.
// Lines inside code fences, the frontmatter and the plugin's `<!--SR:...-->` comments are not cards.
// Which notes are read at all (the flashcards tag) is the caller's business.
namespace flashcards {

constexpr size_t MAX_CARDS_PER_NOTE = 500;
constexpr size_t MAX_FIELD_BYTES = 2000;  // same cap as an editable line

struct Card {
  std::string front;
  std::string back;
  uint32_t line = 0;  // 1-based line of the card's first line, to open the note there
};

// The cards in a note's text. A reversible card gives two (front/back swapped in the second).
std::vector<Card> parseCards(std::string_view text);

// Stable id of a card in one note: FNV-1a 64 of the path and the folded, whitespace-collapsed
// text, so retyping an accent or the spacing keeps the card's history, an edit starts it again.
uint64_t cardId(std::string_view relPath, const Card& card);

// "%016llx" of an id, for the sidecar files.
std::string idToHex(uint64_t id);

}  // namespace flashcards
