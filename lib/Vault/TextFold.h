#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Search folding shared by the vault index and the search box: lower case, accents and breathings
// removed (Latin, Greek monotonic and polytonic, Cyrillic), final sigma as sigma. So "Καλημέρα",
// "καλημερα" and "ΚΑΛΗΜΕΡΑ" all fold to "καλημερα", and "ἄνθρωπος" to "ανθρωποσ".
namespace textfold {

uint32_t foldCodepoint(uint32_t cp);

// Folds a UTF-8 string.
std::string fold(std::string_view text);

// Splits folded text into words (letters and digits; everything else separates). Words shorter
// than `minLength` code points are dropped.
std::vector<std::string> words(std::string_view text, size_t minLength = 2);

}  // namespace textfold
