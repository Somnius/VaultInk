#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class LanguageHyphenator;

class Hyphenator {
 public:
  struct BreakInfo {
    size_t byteOffset;            // Byte position inside the UTF-8 word where a break may occur.
    bool requiresInsertedHyphen;  // true = a visible '-' must be rendered at the break (pattern/fallback breaks).
                                  // false = break occurs at an existing visible separator boundary
                                  //         (explicit '-' or eligible apostrophe contraction boundary).
  };

  // Returns byte offsets where the word may be hyphenated.
  //
  // Break sources (in priority order):
  //   1. Explicit hyphens already present in the word (e.g. '-' or soft-hyphen U+00AD).
  //      When found, language patterns are additionally run on each alphabetic segment
  //      between separators so compound words can break within their parts.
  //      Example: "US-Satellitensystems" yields breaks after "US-" (no inserted hyphen)
  //               plus pattern breaks inside "Satellitensystems" (Sa|tel|li|ten|sys|tems).
  //   2. Apostrophe contractions between letters (e.g. all'improvviso).
  //      Liang patterns are run per alphabetic segment around apostrophes.
  //      A direct break at the apostrophe boundary is allowed only when the left
  //      segment has at least 3 letters and the right segment has at least 3 letters,
  //      avoiding short clitics (e.g. l', d') and contraction tails (e.g. 've, 're, 'll).
  //   3. Language-specific Liang patterns (e.g. German de_patterns).
  //      Example: "Quadratkilometer" -> Qua|drat|ki|lo|me|ter.
  //   4. Fallback every-N-chars splitting (only when includeFallback is true AND no
  //      pattern breaks were found). Used as a last resort to prevent a single oversized
  //      word from overflowing the page width.
  static std::vector<BreakInfo> breakOffsets(const std::string& word, bool includeFallback);

  // Provide a publication-level language hint (e.g. "en", "en-US", "ru") used to select hyphenation rules.
  //
  // A book that names no language (an empty dc:language, "und"/"mul"/"zxx"/"mis", or something that is not a two-
  // or three-letter code) has no hyphenator of its own: its words then take patterns by script. Greek words use the
  // Greek patterns (as in every book), Cyrillic words the Russian ones (Ukrainian with a Ukrainian interface) and
  // Latin words those of the interface language, or English when it has none. A book that names a language the
  // reader has no patterns for ("nl") keeps its Latin and Cyrillic words unbroken, as before.
  static void setPreferredLanguage(const std::string& lang);

  // The interface language (e.g. "EN", "de"), for the words of a book that names no language. English until set.
  static void setFallbackLanguage(const std::string& lang);

  // Which pattern sets the Latin and the Cyrillic words of a book with this dc:language take by script: 0 when the
  // book names a language (they take none by script), else a number that changes with the interface language. The
  // section cache keeps it, so pages hyphenated for another interface language are built again.
  static uint8_t scriptFallbackSignature(const std::string& bookLanguage);

 private:
  static const LanguageHyphenator* cachedHyphenator_;
  static bool bookLanguageUnset_;
  static const LanguageHyphenator* fallbackLatin_;
  static const LanguageHyphenator* fallbackCyrillic_;
};