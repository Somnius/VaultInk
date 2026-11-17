#include "Hyphenator.h"

#include <Utf8.h>

#include <algorithm>
#include <cassert>
#include <vector>

#include "../TypographyEngine.h"
#include "HyphenationCommon.h"
#include "LanguageHyphenator.h"
#include "LanguageRegistry.h"

const LanguageHyphenator* Hyphenator::cachedHyphenator_ = nullptr;
bool Hyphenator::bookLanguageUnset_ = true;
const LanguageHyphenator* Hyphenator::fallbackLatin_ = nullptr;
const LanguageHyphenator* Hyphenator::fallbackCyrillic_ = nullptr;

namespace {

// Normalize ISO 639-2 (three-letter) codes to ISO 639-1 (two-letter) codes used by the
// hyphenation registry.  EPUBs may use either form in their dc:language metadata (e.g.
// "eng" instead of "en").  Both the bibliographic ("fre"/"ger") and terminological
// ("fra"/"deu") ISO 639-2 variants are mapped.
struct Iso639Mapping {
  const char* iso639_2;
  const char* iso639_1;
};
static constexpr Iso639Mapping kIso639Mappings[] = {
    {"eng", "en"}, {"fra", "fr"}, {"fre", "fr"}, {"deu", "de"}, {"ger", "de"}, {"rus", "ru"}, {"spa", "es"},
    {"ita", "it"}, {"ukr", "uk"}, {"swe", "sv"}, {"fin", "fi"}, {"gre", "el"}, {"ell", "el"}};

// The primary subtag in lowercase (e.g., "en-US" -> "en", "ENG" -> "eng").
std::string primarySubtag(const std::string& langTag) {
  std::string primary;
  primary.reserve(langTag.size());
  for (char c : langTag) {
    if (c == '-' || c == '_') break;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    primary.push_back(c);
  }
  return primary;
}

// True when a book's dc:language names no language: empty, one of the "no language" codes (und, mul, zxx, mis), or
// not a two- or three-letter code at all. A well-formed code the reader has no patterns for ("nl", "ja") is a
// language the book did name, so it is not unset.
bool languageTagIsUnset(const std::string& langTag) {
  const std::string primary = primarySubtag(langTag);
  if (primary.size() < 2 || primary.size() > 3) return true;
  for (const char c : primary) {
    if (c < 'a' || c > 'z') return true;
  }
  return primary == "und" || primary == "mul" || primary == "zxx" || primary == "mis";
}

// Maps a BCP-47 or ISO 639-2 language tag to a language-specific hyphenator.
const LanguageHyphenator* hyphenatorForLanguage(const std::string& langTag) {
  if (langTag.empty()) return nullptr;

  // Extract primary subtag and normalize to lowercase (e.g., "en-US" -> "en", "ENG" -> "en").
  std::string primary = primarySubtag(langTag);
  if (primary.empty()) return nullptr;

  // Normalize ISO 639-2 three-letter codes to two-letter equivalents.
  for (const auto& mapping : kIso639Mappings) {
    if (primary == mapping.iso639_2) {
      primary = mapping.iso639_1;
      break;
    }
  }

  return getLanguageHyphenatorForPrimaryTag(primary);
}

// Greek words use the Greek patterns whatever the book's dc:language says. Greek EPUBs,
// translations especially, are often tagged "en" or not tagged at all, and the English
// (or no) hyphenator can't break Greek letters. A word with any polytonic letter uses the
// polytonic patterns, which also cover monotonic letters. Mixed-script words (e.g.
// "α-helix") keep the book's hyphenator so their Latin or Cyrillic parts still break.
//
// A letter of a script that is not Latin, Greek or Cyrillic (Hebrew, Arabic, Indic, CJK, Hangul and so on); not
// punctuation, digits, symbols or emoji.
bool isOtherScriptLetter(const uint32_t cp) {
  return (cp >= 0x0530 && cp < 0x2000) || (cp >= 0x2C00 && cp < 0x2DE0) || (cp >= 0x2E80 && cp <= 0xD7FF) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE70 && cp < 0xFEFF) ||
         (cp >= 0xFF66 && cp <= 0xFFDC) || (cp >= 0x10000 && cp < 0x1F000) || (cp >= 0x20000 && cp <= 0x323AF);
}

// A book that names no language passes the two fallbacks (nullptr otherwise): a word of Latin letters only or of
// Cyrillic letters only then takes that script's patterns. A word of mixed scripts (Latin with Cyrillic, or either
// with Greek, Hebrew, Arabic, CJK or any other script) stays unbroken, as before.
const LanguageHyphenator* hyphenatorForWord(const std::vector<CodepointInfo>& cps, const LanguageHyphenator* preferred,
                                            const LanguageHyphenator* latinFallback,
                                            const LanguageHyphenator* cyrillicFallback) {
  static const LanguageHyphenator* const greek = getLanguageHyphenatorForPrimaryTag("el");
  static const LanguageHyphenator* const polytonic = getLanguageHyphenatorForPrimaryTag("grc");
  const bool greekBook = preferred == greek || preferred == polytonic;
  bool hasGreek = false;
  bool hasPolytonic = false;
  bool hasLatin = false;
  bool hasCyrillic = false;
  bool hasOther = false;
  for (const auto& cp : cps) {
    if (isGreekLetter(cp.value)) {
      hasGreek = true;
      hasPolytonic = hasPolytonic || isPolytonicGreek(cp.value);
    } else if (isCyrillicLetter(cp.value)) {
      hasCyrillic = true;
    } else if (isLatinLetter(cp.value)) {
      hasLatin = true;
    } else if (isOtherScriptLetter(cp.value)) {
      hasOther = true;
    }
  }
  if (!greekBook && (hasLatin || hasCyrillic)) {
    if (preferred || hasGreek || hasOther || (hasLatin && hasCyrillic)) return preferred;
    return hasCyrillic ? cyrillicFallback : latinFallback;
  }
  if (!hasGreek) return preferred;
  if (hasPolytonic) return polytonic;
  return greekBook ? preferred : greek;
}

// Typography Engine, Hyphenation language: the chosen pattern set for every word, or nullptr for "Book language".
const LanguageHyphenator* overrideHyphenator(const uint8_t language) {
  if (language == 0 || language > typoengine::LANGUAGE_COUNT) return nullptr;
  static const LanguageHyphenator* table[typoengine::LANGUAGE_COUNT] = {};
  static bool filled = false;
  if (!filled) {
    for (size_t i = 0; i < typoengine::LANGUAGE_COUNT; ++i) {
      table[i] = getLanguageHyphenatorForPrimaryTag(typoengine::LANGUAGE_TAGS[i]);
    }
    filled = true;
  }
  return table[language - 1];
}

// Typography Engine, minimum letters: 0 keeps the language's own minimum.
size_t minPrefixFor(const LanguageHyphenator* hyphenator, const typoengine::Config& typo) {
  if (typo.minLeft != 0) return typo.minLeft;
  return hyphenator ? hyphenator->minPrefix() : LiangWordConfig::kDefaultMinPrefix;
}

size_t minSuffixFor(const LanguageHyphenator* hyphenator, const typoengine::Config& typo) {
  if (typo.minRight != 0) return typo.minRight;
  return hyphenator ? hyphenator->minSuffix() : LiangWordConfig::kDefaultMinSuffix;
}

std::vector<size_t> patternIndexes(const LanguageHyphenator& hyphenator, const std::vector<CodepointInfo>& cps,
                                   const typoengine::Config& typo) {
  if (typo.minLeft == 0 && typo.minRight == 0) return hyphenator.breakIndexes(cps);
  return hyphenator.breakIndexes(cps, minPrefixFor(&hyphenator, typo), minSuffixFor(&hyphenator, typo));
}

// Maps a codepoint index back to its byte offset inside the source word.
size_t byteOffsetForIndex(const std::vector<CodepointInfo>& cps, const size_t index) {
  return (index < cps.size()) ? cps[index].byteOffset : (cps.empty() ? 0 : cps.back().byteOffset);
}

// Builds a vector of break information from explicit hyphen markers in the given codepoints.
// Only hyphens that appear between two alphabetic characters are considered valid breaks.
//
// Example: "US-Satellitensystems" (cps: U, S, -, S, a, t, ...)
//   -> finds '-' at index 2 with alphabetic neighbors 'S' and 'S'
//   -> returns one BreakInfo at the byte offset of 'S' (the char after '-'),
//      with requiresInsertedHyphen=false because '-' is already visible.
//
// Example: "Satel\u00ADliten" (soft-hyphen between 'l' and 'l')
//   -> returns one BreakInfo with requiresInsertedHyphen=true (soft-hyphen
//      is invisible and needs a visible '-' when the break is used).
std::vector<Hyphenator::BreakInfo> buildExplicitBreakInfos(const std::vector<CodepointInfo>& cps) {
  std::vector<Hyphenator::BreakInfo> breaks;

  for (size_t i = 1; i + 1 < cps.size(); ++i) {
    const uint32_t cp = cps[i].value;
    if (!isExplicitHyphen(cp) || !isAlphabetic(cps[i - 1].value) || !isAlphabetic(cps[i + 1].value)) {
      continue;
    }
    // Offset points to the next codepoint so rendering starts after the hyphen marker.
    breaks.push_back({cps[i + 1].byteOffset, isSoftHyphen(cp)});
  }

  return breaks;
}

bool isSegmentSeparator(const uint32_t cp) { return isExplicitHyphen(cp) || isApostrophe(cp); }

void appendSegmentPatternBreaks(const std::vector<CodepointInfo>& cps, const LanguageHyphenator& hyphenator,
                                const typoengine::Config& typo, const bool includeFallback,
                                std::vector<Hyphenator::BreakInfo>& outBreaks) {
  size_t segStart = 0;

  for (size_t i = 0; i <= cps.size(); ++i) {
    const bool atEnd = i == cps.size();
    const bool atSeparator = !atEnd && isSegmentSeparator(cps[i].value);
    if (!atEnd && !atSeparator) {
      continue;
    }

    if (i > segStart) {
      std::vector<CodepointInfo> segment(cps.begin() + segStart, cps.begin() + i);
      auto segIndexes = patternIndexes(hyphenator, segment, typo);

      if (includeFallback && segIndexes.empty()) {
        const size_t minPrefix = minPrefixFor(&hyphenator, typo);
        const size_t minSuffix = minSuffixFor(&hyphenator, typo);
        for (size_t idx = minPrefix; idx + minSuffix <= segment.size(); ++idx) {
          segIndexes.push_back(idx);
        }
      }

      for (const size_t idx : segIndexes) {
        assert(idx > 0 && idx < segment.size());
        if (idx == 0 || idx >= segment.size()) continue;
        const size_t cpIdx = segStart + idx;
        if (cpIdx < cps.size()) {
          outBreaks.push_back({cps[cpIdx].byteOffset, true});
        }
      }
    }

    segStart = i + 1;
  }
}

void appendApostropheContractionBreaks(const std::vector<CodepointInfo>& cps,
                                       std::vector<Hyphenator::BreakInfo>& outBreaks) {
  constexpr size_t kMinLeftSegmentLen = 3;
  constexpr size_t kMinRightSegmentLen = 3;
  size_t segmentStart = 0;

  for (size_t i = 0; i < cps.size(); ++i) {
    if (isSegmentSeparator(cps[i].value)) {
      if (isApostrophe(cps[i].value) && i > 0 && i + 1 < cps.size() && isAlphabetic(cps[i - 1].value) &&
          isAlphabetic(cps[i + 1].value)) {
        size_t leftPrefixLen = 0;
        for (size_t j = segmentStart; j < i; ++j) {
          if (isAlphabetic(cps[j].value)) {
            ++leftPrefixLen;
          }
        }

        size_t rightSuffixLen = 0;
        for (size_t j = i + 1; j < cps.size() && !isSegmentSeparator(cps[j].value); ++j) {
          if (isAlphabetic(cps[j].value)) {
            ++rightSuffixLen;
          }
        }

        // Avoid stranding short clitics like "l'"/"d'" or contraction tails like "'ve"/"'re"/"'ll".
        if (leftPrefixLen >= kMinLeftSegmentLen && rightSuffixLen >= kMinRightSegmentLen) {
          outBreaks.push_back({cps[i + 1].byteOffset, false});
        }
      }
      segmentStart = i + 1;
    }
  }
}

void sortAndDedupeBreakInfos(std::vector<Hyphenator::BreakInfo>& infos) {
  std::sort(infos.begin(), infos.end(), [](const Hyphenator::BreakInfo& a, const Hyphenator::BreakInfo& b) {
    if (a.byteOffset != b.byteOffset) {
      return a.byteOffset < b.byteOffset;
    }
    return a.requiresInsertedHyphen < b.requiresInsertedHyphen;
  });

  infos.erase(std::unique(infos.begin(), infos.end(),
                          [](const Hyphenator::BreakInfo& a, const Hyphenator::BreakInfo& b) {
                            return a.byteOffset == b.byteOffset;
                          }),
              infos.end());
}

}  // namespace

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string& word, const bool includeFallback) {
  if (word.empty()) {
    return {};
  }

  // Convert to codepoints and normalize word boundaries.
  auto cps = collectCodepoints(word);
  trimSurroundingPunctuationAndFootnote(cps);
  // Typography Engine: a chosen language is used for every word, so the Greek-word rule applies to "Book language"
  // only. "Soft hyphens only" drops the pattern and apostrophe breaks; the every-N-letters fallback for a word wider
  // than the line stays, or that word would run off the page.
  const typoengine::Config typo = typoengine::current();
  const LanguageHyphenator* const chosen = overrideHyphenator(typo.language);
  // A book that names no language (see setPreferredLanguage) breaks its Latin and Cyrillic words by script.
  static const LanguageHyphenator* const english = getLanguageHyphenatorForPrimaryTag("en");
  static const LanguageHyphenator* const russian = getLanguageHyphenatorForPrimaryTag("ru");
  const bool byScript = cachedHyphenator_ == nullptr && bookLanguageUnset_;
  const LanguageHyphenator* const latinFallback = !byScript ? nullptr : fallbackLatin_ ? fallbackLatin_ : english;
  const LanguageHyphenator* const cyrillicFallback = !byScript           ? nullptr
                                                     : fallbackCyrillic_ ? fallbackCyrillic_
                                                                         : russian;
  const auto* hyphenator = chosen ? chosen : hyphenatorForWord(cps, cachedHyphenator_, latinFallback, cyrillicFallback);
  const bool softOnly = typo.hyphenMode == typoengine::HYPHEN_SOFT_ONLY;

  // Detect apostrophe-like separators early; used by both branches below.
  bool hasApostropheLikeSeparator = false;
  for (const auto& cp : cps) {
    if (isApostrophe(cp.value)) {
      hasApostropheLikeSeparator = true;
      break;
    }
  }

  // Explicit hyphen markers (soft or hard) take precedence over language breaks.
  auto explicitBreakInfos = buildExplicitBreakInfos(cps);
  if (!explicitBreakInfos.empty()) {
    // When a word contains explicit hyphens we also run Liang patterns on each alphabetic
    // segment between them. Without this, "US-Satellitensystems" would only offer one split
    // point (after "US-"), making it impossible to break mid-"Satellitensystems" even when
    // "US-Satelliten-" would fit on the line.
    //
    // Example: "US-Satellitensystems"
    //   Segments: ["US", "Satellitensystems"]
    //   Explicit break: after "US-"           -> @3  (no inserted hyphen)
    //   Pattern breaks on "Satellitensystems" -> @5  Sa|tel  (+hyphen)
    //                                            @8  Satel|li  (+hyphen)
    //                                            @10 Satelli|ten  (+hyphen)
    //                                            @13 Satelliten|sys  (+hyphen)
    //                                            @16 Satellitensys|tems  (+hyphen)
    //   Result: 6 sorted break points; the line-breaker picks the widest prefix that fits.
    if (hyphenator && !softOnly) {
      appendSegmentPatternBreaks(cps, *hyphenator, typo, /*includeFallback=*/false, explicitBreakInfos);
    }
    // Also add apostrophe contraction breaks when present (e.g. "l'état-major"
    // has both an explicit hyphen and an apostrophe that can independently break).
    if (hasApostropheLikeSeparator && !softOnly) {
      appendApostropheContractionBreaks(cps, explicitBreakInfos);
    }
    // Merge all break points into ascending byte-offset order.
    sortAndDedupeBreakInfos(explicitBreakInfos);
    return explicitBreakInfos;
  }

  // Apostrophe-like separators split compounds into alphabetic segments; run Liang on each segment.
  // This allows words like "all'improvviso" to hyphenate within "improvviso" instead of becoming
  // completely unsplittable due to the apostrophe punctuation. Apostrophe contraction breaks are
  // applied regardless of whether a language hyphenator is available.
  if (hasApostropheLikeSeparator && !softOnly) {
    std::vector<BreakInfo> segmentedBreaks;
    if (hyphenator) {
      appendSegmentPatternBreaks(cps, *hyphenator, typo, includeFallback, segmentedBreaks);
    }
    appendApostropheContractionBreaks(cps, segmentedBreaks);
    sortAndDedupeBreakInfos(segmentedBreaks);
    return segmentedBreaks;
  }

  // Ask language hyphenator for legal break points.
  std::vector<size_t> indexes;
  if (hyphenator && !softOnly) {
    indexes = patternIndexes(*hyphenator, cps, typo);
  }

  // Only add fallback breaks if needed
  if (includeFallback && indexes.empty()) {
    const size_t minPrefix = minPrefixFor(hyphenator, typo);
    const size_t minSuffix = minSuffixFor(hyphenator, typo);
    for (size_t idx = minPrefix; idx + minSuffix <= cps.size(); ++idx) {
      indexes.push_back(idx);
    }
  }

  if (indexes.empty()) {
    return {};
  }

  std::vector<Hyphenator::BreakInfo> breaks;
  breaks.reserve(indexes.size());
  for (const size_t idx : indexes) {
    // CJK characters can break without inserting a visible hyphen.
    // Check the codepoint at the break position: if it's a CJK character,
    // no hyphen is needed since CJK scripts don't use hyphenation.
    bool needsHyphen = true;
    if (idx < cps.size() && utf8IsCjkBreakable(cps[idx].value)) {
      needsHyphen = false;
    } else if (idx > 0 && utf8IsCjkBreakable(cps[idx - 1].value)) {
      needsHyphen = false;
    }
    breaks.push_back({byteOffsetForIndex(cps, idx), needsHyphen});
  }

  return breaks;
}

void Hyphenator::setPreferredLanguage(const std::string& lang) {
  cachedHyphenator_ = hyphenatorForLanguage(lang);
  bookLanguageUnset_ = languageTagIsUnset(lang);
}

void Hyphenator::setFallbackLanguage(const std::string& lang) {
  const LanguageHyphenator* const hyphenator = hyphenatorForLanguage(lang);
  const std::string primary = primarySubtag(lang);
  const bool cyrillic = primary == "ru" || primary == "rus" || primary == "uk" || primary == "ukr";
  const bool greek = primary == "el" || primary == "ell" || primary == "gre" || primary == "grc";
  // nullptr means the default of its script: English, Russian.
  fallbackCyrillic_ = cyrillic ? hyphenator : nullptr;
  fallbackLatin_ = cyrillic || greek ? nullptr : hyphenator;
}

uint8_t Hyphenator::scriptFallbackSignature(const std::string& bookLanguage) {
  if (!languageTagIsUnset(bookLanguage) || hyphenatorForLanguage(bookLanguage) != nullptr) return 0;
  // 1..LANGUAGE_COUNT for a pattern set, in the order of typoengine::LANGUAGE_TAGS. Two of them in one byte.
  static_assert(typoengine::LANGUAGE_COUNT <= 15, "a pattern set number has to fit in four bits");
  const auto number = [](const LanguageHyphenator* hyphenator, const char* fallbackTag) -> uint8_t {
    if (hyphenator == nullptr) hyphenator = getLanguageHyphenatorForPrimaryTag(fallbackTag);
    for (uint8_t i = 0; i < typoengine::LANGUAGE_COUNT; ++i) {
      if (getLanguageHyphenatorForPrimaryTag(typoengine::LANGUAGE_TAGS[i]) == hyphenator) return i + 1;
    }
    return 0;
  };
  return static_cast<uint8_t>(number(fallbackLatin_, "en") | (number(fallbackCyrillic_, "ru") << 4));
}
