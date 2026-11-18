#include "ParsedText.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <TextFeatures.h>
#include <Utf8.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <string_view>
#include <vector>

#include "CjkBreaks.h"
#include "TokenBoundary.h"
#include "TypographyEngine.h"
#include "hyphenation/HyphenationCommon.h"
#include "hyphenation/Hyphenator.h"

constexpr int MAX_COST = std::numeric_limits<int>::max();

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;
// Paragraph-level direction: scan the first N words to find base direction.
constexpr size_t RTL_PARAGRAPH_PROBE_WORDS = 3;
// Per-word: scan enough chars to see through leading neutrals (quotes, numbers)
// before giving up. 64 is a hedge for pathological cases like long numeric tokens.
constexpr int RTL_PER_WORD_PROBE_DEPTH = 64;
constexpr size_t MIN_JUSTIFY_GAPS = 1;

// Byte-level pre-check: Hebrew UTF-8 lead bytes 0xD6-0xD7, Arabic/Syriac 0xD8-0xDB.
bool mayContainRtlBytes(const char* str) {
  for (const auto* p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
    if (*p >= 0xD6 && *p <= 0xDB) return true;
  }
  return false;
}

// Returns the first rendered codepoint of a word (skipping leading soft hyphens).
uint32_t firstCodepoint(const std::string_view word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.data());
  const auto* const end = ptr + word.size();
  while (ptr < end) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;  // skip soft hyphens
  }
  return 0;
}

// Returns the last codepoint of a word by scanning backward for the start of the last UTF-8 sequence.
// Trailing soft hyphens (U+00AD) are skipped, as firstCodepoint skips leading ones: they are not drawn, and the line
// breaker sees words with them while extractLine sees them stripped, so both must find the same last letter (the
// optical word gap and kerning depend on it).
uint32_t lastCodepoint(std::string_view word) {
  while (!word.empty()) {
    // UTF-8 continuation bytes start with 10xxxxxx; scan backward to find the leading byte.
    size_t i = word.size() - 1;
    while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
      --i;
    }
    const auto* ptr = reinterpret_cast<const unsigned char*>(word.data() + i);
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp != 0x00AD) return cp;
    word = word.substr(0, i);
  }
  return 0;
}

// Hanging punctuation (textfeatures::HANGING): an opening quote or bracket that starts a line hangs into the left
// margin so the first letter sits on the text edge, and a closing mark that ends a line hangs into the right margin
// so justified lines look straight. The hang is the glyph's advance, capped so it never leaves the page margin.
constexpr int MAX_HANG_PX = 12;

bool isHangingOpener(const uint32_t cp) {
  switch (cp) {
    case '"':
    case '\'':
    case '(':
    case '[':
    case '{':
    case 0x2018:  // ‘
    case 0x201B:  // ‛
    case 0x201C:  // “
    case 0x201F:  // ‟
    case 0x201E:  // „
    case 0x201A:  // ‚
    case 0x00AB:  // «
    case 0x2039:  // ‹
      return true;
    default:
      return false;
  }
}

bool isHangingCloser(const uint32_t cp) {
  switch (cp) {
    case '.':
    case ',':
    case ';':
    case ':':
    case '!':
    case '?':
    case '-':  // also the hyphen added by hyphenation
    case ')':
    case ']':
    case '}':
    case '"':
    case '\'':
    case 0x2010:  // hyphen
    case 0x2011:  // non-breaking hyphen
    case 0x2012:  // figure dash
    case 0x2013:  // en dash
    case 0x2014:  // em dash
    case 0x2019:  // ’
    case 0x201D:  // ”
    case 0x00BB:  // »
    case 0x203A:  // ›
    case 0x0387:  // Greek ano teleia
    case 0x037E:  // Greek question mark
    case 0x00B7:  // middle dot
    case 0x2026:  // …
      return true;
    default:
      return false;
  }
}

// Advance of one glyph in the line's font, capped to MAX_HANG_PX.
int hangWidth(const GfxRenderer& renderer, const int fontId, const uint32_t cp, const EpdFontFamily::Style style) {
  char buf[8] = {};
  utf8AppendCodepoint(buf, cp);
  const int width = renderer.getTextWidth(fontId, buf, style);
  return std::max(0, std::min(width, MAX_HANG_PX));
}

bool containsSoftHyphen(const std::string_view word) { return word.find(SOFT_HYPHEN_UTF8) != std::string_view::npos; }

bool containsCjkBreakableCodepoint(const std::string& text) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*ptr) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (utf8IsCjkBreakable(cp)) {
      return true;
    }
  }
  return false;
}

uint32_t countCodepoints(const std::string_view text) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(text.data());
  const auto* const end = ptr + text.size();
  uint32_t count = 0;
  while (ptr < end) {
    utf8NextCodepoint(&ptr);
    count++;
  }
  return count;
}

// Line-end split points inside a Hangul word, using the CJK boundary rules (no hyphen is drawn).
std::vector<Hyphenator::BreakInfo> hangulLineEndBreaks(const std::string& word) {
  std::vector<Hyphenator::BreakInfo> breaks;
  if (word.empty()) return breaks;
  const auto* const start = reinterpret_cast<const unsigned char*>(word.c_str());
  const auto* ptr = start;
  uint32_t prev = utf8NextCodepoint(&ptr);
  while (*ptr) {
    const size_t offset = static_cast<size_t>(ptr - start);
    const uint32_t cur = utf8NextCodepoint(&ptr);
    if ((utf8IsHangul(prev) || utf8IsHangul(cur)) && CjkBreaks::boundaryAllowsBreak(prev, cur)) {
      breaks.push_back({offset, false});
    }
    prev = cur;
  }
  return breaks;
}

// Scans text for explicit hyphens or dashes and returns byte offsets where a line break is permitted right after
// the hyphen/dash. Multi-character hyphen/dash runs are kept together as an indivisible unit.
std::vector<size_t> explicitHyphenBreakByteOffsets(const std::string& text, const bool attachToPrevious) {
  std::vector<size_t> breakOffsets;
  const auto* ptr = reinterpret_cast<const unsigned char*>(text.c_str());
  const auto* const start = ptr;
  const auto* const end = ptr + text.size();

  bool hasLeadingContent = attachToPrevious;

  while (ptr < end) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) break;

    if (TokenBoundary::allowsBreakAfterExplicitHyphen(cp)) {
      while (ptr < end) {
        const auto* const nextStart = ptr;
        const uint32_t nextCp = utf8NextCodepoint(&ptr);
        if (!TokenBoundary::allowsBreakAfterExplicitHyphen(nextCp)) {
          ptr = nextStart;
          break;
        }
      }

      if (hasLeadingContent && ptr < end) {
        breakOffsets.push_back(static_cast<size_t>(ptr - start));
      }
      hasLeadingContent = true;
    } else {
      hasLeadingContent = true;
    }
  }

  return breakOffsets;
}

int computeJustifyExtra(const int spareSpace, const size_t gapCount) {
  if (gapCount < MIN_JUSTIFY_GAPS || spareSpace <= 0) return 0;
  // Distribute the spare space evenly across gaps. Do NOT bail out to 0 when the
  // per-gap stretch is large: a sparse line (few words on a wide page) legitimately
  // needs big gaps to reach the margin. Returning 0 there disables justification for
  // that line, leaving it right-aligned (RTL) / left-aligned (LTR) — the mismatched
  // alignment bug. Match the un-capped behavior of the old code.
  return spareSpace / static_cast<int>(gapCount);
}

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

constexpr int scaleSpace(const int advance, const uint8_t wordSpacingPercent) {
  return wordSpacingPercent == 100 ? advance : (advance * wordSpacingPercent + 50) / 100;
}

// Returns the advance width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
// Uses advance width (sum of glyph advances + kerning) rather than bounding box width so that italic glyph overhangs
// don't inflate inter-word spacing.
// `word` is usually NUL-terminated at data()[size()] (arena entries and
// std::string arguments), but focus-split candidates pass PREFIX views of a
// stored word. Probing data()[size()] is in-bounds for every caller (it is
// either the word's own NUL or a mid-word byte of the same arena entry) and
// routes unterminated views through the copying path so the C API never
// measures past the view.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string_view word,
                          const EpdFontFamily::Style style, const int8_t characterSpacing,
                          const uint8_t wordSpacingPercent, const bool appendHyphen = false) {
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return scaleSpace(renderer.getSpaceWidth(fontId, style), wordSpacingPercent);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen && word.data()[word.size()] == '\0') {
    return renderer.getTextAdvanceX(fontId, word.data(), style, characterSpacing);
  }

  std::string sanitized(word);
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextAdvanceX(fontId, sanitized.c_str(), style, characterSpacing);
}

// True when a token ends with a hyphen or dash that allows a line break after it. U+00AD is
// excluded because it renders as nothing, so only a hyphenation break -- which substitutes a
// visible '-' -- may use it; U+2011 because forbidding that break is its purpose.
bool endsWithBreakableHyphen(const std::string_view token) {
  if (token.empty()) return false;
  return TokenBoundary::allowsBreakAfterExplicitHyphen(lastCodepoint(token));
}

// Focus Reading renders the first `focusBoundary` bytes of a token bold and the rest at the
// token's own style; 0 means no emphasis. The bold run is at most 9 codepoints (see addWord),
// so it is copied to a stack buffer rather than a substring: this runs once per word during
// pagination and must not allocate.
constexpr size_t FOCUS_PREFIX_BUF_SIZE = 40;

// Advance from the token origin to the start of its regular-weight suffix: the bold prefix plus
// the kerning across the weight change. Doubles as the suffix's x offset inside the word.
uint16_t measureFocusPrefixAdvance(const GfxRenderer& renderer, const int fontId, const std::string_view word,
                                   const EpdFontFamily::Style style, const uint8_t focusBoundary,
                                   const int8_t characterSpacing) {
  char prefixBuf[FOCUS_PREFIX_BUF_SIZE];
  const size_t prefixLen = std::min<size_t>(focusBoundary, FOCUS_PREFIX_BUF_SIZE - 1);
  memcpy(prefixBuf, word.data(), prefixLen);
  prefixBuf[prefixLen] = '\0';

  const auto boldStyle = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD);
  const auto* suffixPtr = reinterpret_cast<const unsigned char*>(word.data() + focusBoundary);
  const int kerning =
      renderer.getKerning(fontId, lastCodepoint(prefixBuf), utf8NextCodepoint(&suffixPtr), boldStyle, characterSpacing);
  return static_cast<uint16_t>(renderer.getTextAdvanceX(fontId, prefixBuf, boldStyle, characterSpacing) + kerning);
}

// Advance width of a whole token, accounting for a bold focus prefix when it has one.
uint16_t measureFocusWordWidth(const GfxRenderer& renderer, const int fontId, const std::string_view word,
                               const EpdFontFamily::Style style, const uint8_t focusBoundary,
                               const int8_t characterSpacing, const uint8_t wordSpacingPercent,
                               const bool appendHyphen = false) {
  if (focusBoundary == 0) {
    return measureWordWidth(renderer, fontId, word, style, characterSpacing, wordSpacingPercent, appendHyphen);
  }
  if (focusBoundary >= word.size()) {
    // The bold run covers the whole token, as for a split candidate ending on the boundary.
    return measureWordWidth(renderer, fontId, word, static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD),
                            characterSpacing, wordSpacingPercent, appendHyphen);
  }
  // Through measureWordWidth (not getTextAdvanceX directly): `word` itself can
  // be an unterminated prefix view here, and the suffix view inherits that.
  const uint16_t suffixWidth = measureWordWidth(renderer, fontId, word.substr(focusBoundary), style, characterSpacing,
                                                wordSpacingPercent, appendHyphen);
  return measureFocusPrefixAdvance(renderer, fontId, word, style, focusBoundary, characterSpacing) + suffixWidth;
}

// Focus boundaries for the two halves of a token split at `splitOffset`.
uint8_t focusBoundaryBefore(const uint8_t focusBoundary, const size_t splitOffset) {
  return static_cast<uint8_t>(std::min<size_t>(focusBoundary, splitOffset));
}
uint8_t focusBoundaryAfter(const uint8_t focusBoundary, const size_t splitOffset) {
  return focusBoundary > splitOffset ? static_cast<uint8_t>(focusBoundary - splitOffset) : 0;
}

// Checks if a UTF-8 codepoint should be counted as part of a word for Focus Reading
bool isWordCharacter(uint32_t cp) {
  // ASCII range (Catches 95%+ of characters immediately)
  if (cp < 128) {
    // Bitwise trick: (cp | 0x20) converts uppercase ASCII to lowercase.
    // This checks for A-Z and a-z mathematically, avoiding memory lookups and <cctype>
    return ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') || cp == '\'';
  }

  // General Punctuation Block, Currency, Math, Arrows, & Symbols (0x2000 - 0x2BFF)
  if (cp >= 0x2000 && cp <= 0x2BFF) {
    // Explicitly allow smart quotes, reject all other general punctuation (em-dashes, etc.)
    return cp == 0x2018 || cp == 0x2019;
  }

  // Latin-1 Punctuation Block (0x00A1 - 0x00BF)
  if (cp >= 0x00A1 && cp <= 0x00BF) {
    // Allow ordinal indicators and micro sign, reject the rest (¡, ¿, «, », etc.)
    return cp == 0x00AA || cp == 0x00B5 || cp == 0x00BA;
  }

  // Rejects Two-em dash, Three-em dash, Double oblique hyphen, etc.
  if (cp >= 0x2E00 && cp <= 0x2E7F) return false;

  // Rejects Modifier Minus (0x02D7), Small Hyphen (0xFE63), and Fullwidth Hyphen (0xFF0D)
  if (cp == 0x02D7 || cp == 0xFE63 || cp == 0xFF0D) return false;
  // Assume all other Unicode ranges (accented letters, Cyrillic, Greek, etc.) are valid

  return true;
}

}  // namespace

uint32_t ParsedText::visibleOffsetBaseAt(const size_t wordIndex) const {
  uint32_t base = visibleOffsetBase;
  for (const auto& rebase : visibleOffsetRebases) {
    if (rebase.wordIndex > wordIndex) break;
    base = rebase.base;
  }
  return base;
}

uint32_t ParsedText::visibleOffsetAt(const size_t wordIndex) const {
  if (wordIndex >= wordVisibleOffsetDeltas.size()) return 0;
  return visibleOffsetBaseAt(wordIndex) + wordVisibleOffsetDeltas[wordIndex];
}

uint32_t ParsedText::sourceOffsetAfter(const uint32_t start, const uint32_t renderedLength) const {
  uint32_t end = start + renderedLength;
  const auto* chunk =
      absorbedSourceCursor && start >= absorbedSourceCursorStart ? absorbedSourceCursor : absorbedSourceHead.get();
  while (chunk && chunk->base + chunk->deltas[chunk->count - 1] <= start) chunk = chunk->next.get();
  absorbedSourceCursor = chunk;
  absorbedSourceCursorStart = start;
  for (; chunk; chunk = chunk->next.get()) {
    const auto* first = chunk->deltas + chunk->begin;
    const auto* last = chunk->deltas + chunk->count;
    if (start >= chunk->base) first = std::upper_bound(first, last, start - chunk->base);
    for (; first != last; ++first) {
      if (chunk->base + *first > end) return end;
      ++end;
    }
  }
  return end;
}

bool ParsedText::recordAbsorbedSourceOffset(const uint32_t offset) {
  if (!absorbedSourceTail || absorbedSourceTail->count == 64 || offset < absorbedSourceTail->base ||
      offset - absorbedSourceTail->base > std::numeric_limits<uint16_t>::max()) {
    // About 140 bytes per chunk on C3; sparse and paragraph-lived, with no large growth copy.
    auto chunk = makeUniqueNoThrow<AbsorbedSourceChunk>();
    if (!chunk) {
      LOG_ERR("PTX", "OOM: NFC source offsets (%u bytes)", static_cast<unsigned>(sizeof(AbsorbedSourceChunk)));
      droppedWords = true;
      return false;
    }
    chunk->base = offset;
    auto* tail = chunk.get();
    if (absorbedSourceTail) {
      absorbedSourceTail->next = std::move(chunk);
    } else {
      absorbedSourceHead = std::move(chunk);
    }
    absorbedSourceTail = tail;
  }
  absorbedSourceTail->deltas[absorbedSourceTail->count++] = static_cast<uint16_t>(offset - absorbedSourceTail->base);
  return true;
}

void ParsedText::retireAbsorbedSourceOffsets(const uint32_t remainingStart) {
  absorbedSourceCursor = nullptr;
  while (absorbedSourceHead &&
         absorbedSourceHead->base + absorbedSourceHead->deltas[absorbedSourceHead->count - 1] <= remainingStart) {
    auto retired = std::move(absorbedSourceHead);
    absorbedSourceHead = std::move(retired->next);
  }
  if (!absorbedSourceHead) {
    absorbedSourceTail = nullptr;
  } else if (remainingStart >= absorbedSourceHead->base) {
    absorbedSourceHead->begin =
        std::upper_bound(absorbedSourceHead->deltas, absorbedSourceHead->deltas + absorbedSourceHead->count,
                         remainingStart - absorbedSourceHead->base) -
        absorbedSourceHead->deltas;
  }
}

void ParsedText::pushVisibleOffset(const uint32_t offset) {
  uint32_t base = visibleOffsetBase;
  if (wordVisibleOffsetDeltas.empty()) {
    visibleOffsetBase = offset;
    base = offset;
  } else if (!visibleOffsetRebases.empty()) {
    base = visibleOffsetRebases.back().base;
  }

  if (offset < base || offset - base > std::numeric_limits<uint16_t>::max()) {
    visibleOffsetRebases.push_back({wordVisibleOffsetDeltas.size(), offset});
    base = offset;
  }
  wordVisibleOffsetDeltas.push_back(static_cast<uint16_t>(offset - base));
}

void ParsedText::insertVisibleOffset(const size_t wordIndex, const uint32_t offset) {
  const uint32_t base = wordIndex > 0 ? visibleOffsetBaseAt(wordIndex - 1) : visibleOffsetBase;
  for (auto& rebase : visibleOffsetRebases) {
    if (rebase.wordIndex >= wordIndex) rebase.wordIndex++;
  }

  uint32_t insertionBase = base;
  if (offset < base || offset - base > std::numeric_limits<uint16_t>::max()) {
    const auto rebaseIt = std::find_if(visibleOffsetRebases.begin(), visibleOffsetRebases.end(),
                                       [wordIndex](const auto& rebase) { return rebase.wordIndex > wordIndex; });
    visibleOffsetRebases.insert(rebaseIt, {wordIndex, offset});
    insertionBase = offset;
  }
  wordVisibleOffsetDeltas.insert(wordVisibleOffsetDeltas.begin() + wordIndex,
                                 static_cast<uint16_t>(offset - insertionBase));
}

void ParsedText::eraseVisibleOffsetPrefix(const size_t count) {
  if (count >= wordVisibleOffsetDeltas.size()) {
    wordVisibleOffsetDeltas.clear();
    visibleOffsetRebases.clear();
    visibleOffsetBase = 0;
    retireAbsorbedSourceOffsets(UINT32_MAX);
    return;
  }

  const uint32_t remainingStart = visibleOffsetAt(count);
  retireAbsorbedSourceOffsets(remainingStart);
  const uint32_t newBase = visibleOffsetBaseAt(count);
  wordVisibleOffsetDeltas.erase(wordVisibleOffsetDeltas.begin(), wordVisibleOffsetDeltas.begin() + count);
  size_t writeIndex = 0;
  for (auto rebase : visibleOffsetRebases) {
    if (rebase.wordIndex <= count) continue;
    rebase.wordIndex -= count;
    visibleOffsetRebases[writeIndex++] = rebase;
  }
  visibleOffsetRebases.resize(writeIndex);
  visibleOffsetBase = newBase;
}

bool ParsedText::storeWord(const std::string_view text, WordStore::StoredWord& out) {
  if (wordStore.append(text.data(), text.size(), out)) return true;
  if (!droppedWords) {
    LOG_ERR("PTX", "OOM: dropping paragraph text (arena chunk alloc failed)");
  }
  droppedWords = true;
  return false;
}

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle, const bool underline,
                         const bool attachToPrevious, const uint32_t visibleTextOffset, const uint8_t linkId,
                         const uint8_t sizeSlot, const bool noSpaceBefore) {
  if (word.empty()) return;

  // The device fonts carry no combining-mark positioning, so EPUB text stored in NFD
  // (a base letter followed by separate combining accents -- common for Vietnamese,
  // and used for many EPUB <h1> chapter headings) renders with the marks detached or
  // misplaced. Compose to NFC here, the single funnel every word passes through, so a
  // precomposed glyph is used instead. This runs once per word at layout time (the
  // result is cached in the section file) and is a cheap no-op for mark-free text.
  struct SourceContext {
    ParsedText* text;
    uint32_t start;
  } sourceContext{this, visibleTextOffset};
  word = utf8ComposeNfc(
      word,
      [](const uint32_t offset, void* context) {
        const auto& source = *static_cast<SourceContext*>(context);
        return source.text->recordAbsorbedSourceOffset(source.start + offset);
      },
      &sourceContext);
  if (word.empty()) return;

  EpdFontFamily::Style baseStyle = fontStyle;
  if (underline) {
    baseStyle = static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::UNDERLINE);
  }
  const bool wordStartsRtl = !hasRtlWord && mayContainRtlBytes(word.c_str()) &&
                             BidiUtils::startsWithRtl(word.c_str(), RTL_PER_WORD_PROBE_DEPTH);

  // All token pushes funnel through here: the arena append is the only
  // fallible step, and a failed append drops the token without touching the
  // parallel arrays (they must stay in lockstep with words).
  const auto pushStyledToken = [&](std::string_view token, const EpdFontFamily::Style style, const bool continues,
                                   const bool noSpaceBefore, const uint8_t focusBoundary, const uint32_t tokenOffset,
                                   const bool padRuby) {
    WordStore::StoredWord stored;
    if (!storeWord(token, stored)) return;
    words.push_back(stored);
    // Small caps share the style byte's high bit with the discretionary-hyphen flag, so it
    // rides in the size slot instead.
    wordStyles.push_back(
        static_cast<EpdFontFamily::Style>(style & ~(EpdFontFamily::SMALL_CAPS | EpdFontFamily::HIGHLIGHT)));
    wordContinues.push_back(continues);
    wordNoSpaceBefore.push_back(noSpaceBefore);
    wordFocusBoundary.push_back(focusBoundary);
    wordLinkIds.push_back(linkId);
    wordSizeSlots.push_back(
        static_cast<uint8_t>(sizeSlot | ((style & EpdFontFamily::SMALL_CAPS) ? TextBlock::SMALL_CAPS_SLOT_FLAG : 0) |
                             ((style & EpdFontFamily::HIGHLIGHT) ? TextBlock::HIGHLIGHT_SLOT_FLAG : 0)));
    pushVisibleOffset(tokenOffset);
    if (padRuby && !rubyTexts.empty()) {
      rubyTexts.push_back("");
    }
  };

  const auto pushToken = [&](std::string_view token, const bool continues, const bool noSpaceBefore,
                             const uint8_t focusBoundary, const uint32_t tokenOffset) {
    pushStyledToken(token, baseStyle, continues, noSpaceBefore, focusBoundary, tokenOffset, true);
  };

  bool effectiveAttachToPrevious = attachToPrevious;
  bool effectiveNoSpaceBefore = noSpaceBefore;
  // Only a glued token (attachToPrevious == true, i.e. no whitespace separated it from the
  // previous one in the source) may be turned into a gap-less break opportunity. When real
  // whitespace separated the two words, that space is content and must be rendered: Korean
  // is a space-delimited script written in Hangul, which utf8IsCjkBreakable() covers.
  if (attachToPrevious && !words.empty() && endsWithBreakableHyphen(wordStore.view(words.back()))) {
    effectiveAttachToPrevious = true;
    effectiveNoSpaceBefore = true;
  } else if (attachToPrevious && !words.empty() &&
             CjkBreaks::hasBreakOpportunityBetween(lastCodepoint(wordStore.view(words.back())), firstCodepoint(word))) {
    effectiveAttachToPrevious = false;
    effectiveNoSpaceBefore = true;
  }

  // Bulk-reserve the per-token parallel arrays before a burst of pushes so they
  // don't repeatedly double. Only the std::vector arrays are reserved: words and
  // rubyTexts are std::deque (chunked growth, no reserve()/capacity() and no large
  // contiguous reallocation to avoid). wordStyles' capacity gauges them all since
  // pushToken() keeps every array in lockstep.
  const auto ensureTokenCapacity = [&](const size_t additionalTokens) {
    if (additionalTokens == 0) return;
    const size_t requiredSize = words.size() + additionalTokens;
    if (wordStyles.capacity() >= requiredSize) return;

    size_t newCapacity = wordStyles.capacity() < 16 ? 16 : wordStyles.capacity();
    while (newCapacity < requiredSize) {
      newCapacity *= 2;
    }

    wordStyles.reserve(newCapacity);
    wordContinues.reserve(newCapacity);
    wordNoSpaceBefore.reserve(newCapacity);
    wordFocusBoundary.reserve(newCapacity);
    wordLinkIds.reserve(newCapacity);
    wordSizeSlots.reserve(newCapacity);
    wordVisibleOffsetDeltas.reserve(newCapacity);
  };

  if (auto breakOffsets = CjkBreaks::characterBreakByteOffsets(word); !breakOffsets.empty()) {
    // CJK-heavy paragraphs can push hundreds of tiny tokens quickly when CSS toggles
    // inline styles. Reserve once up front to avoid repeated vector growth reallocations.
    ensureTokenCapacity(breakOffsets.size() + 1);
    bool firstToken = true;
    size_t tokenStart = 0;
    uint32_t tokenVisibleOffset = visibleTextOffset;
    for (const size_t breakOffset : breakOffsets) {
      if (breakOffset <= tokenStart || breakOffset > word.size()) continue;
      const std::string_view token(word.data() + tokenStart, breakOffset - tokenStart);
      const bool attach = firstToken ? effectiveAttachToPrevious
                                     : (!words.empty() && endsWithBreakableHyphen(wordStore.view(words.back())));
      pushToken(token, attach, firstToken ? effectiveNoSpaceBefore : true,
                /*focusBoundary=*/0, tokenVisibleOffset);
      tokenVisibleOffset = sourceOffsetAfter(tokenVisibleOffset, countCodepoints(token));
      firstToken = false;
      tokenStart = breakOffset;
    }
    if (tokenStart < word.size()) {
      const bool attach = firstToken ? effectiveAttachToPrevious
                                     : (!words.empty() && endsWithBreakableHyphen(wordStore.view(words.back())));
      pushToken(std::string_view(word).substr(tokenStart), attach, firstToken ? effectiveNoSpaceBefore : true,
                /*focusBoundary=*/0, tokenVisibleOffset);
    }
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  if (containsCjkBreakableCodepoint(word)) {
    pushToken(word, effectiveAttachToPrevious, effectiveNoSpaceBefore, /*focusBoundary=*/0, visibleTextOffset);
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  // Already-bold text should stay fully bold; focus splitting would make its suffix regular later.
  if (!this->focusReadingEnabled || (baseStyle & EpdFontFamily::BOLD) != 0) {
    if (auto breakOffsets = explicitHyphenBreakByteOffsets(word, effectiveAttachToPrevious); !breakOffsets.empty()) {
      ensureTokenCapacity(breakOffsets.size() + 1);
      bool firstToken = true;
      size_t tokenStart = 0;
      uint32_t tokenVisibleOffset = visibleTextOffset;
      for (const size_t breakOffset : breakOffsets) {
        if (breakOffset <= tokenStart || breakOffset > word.size()) continue;
        const std::string_view token(word.data() + tokenStart, breakOffset - tokenStart);
        pushToken(token, firstToken ? effectiveAttachToPrevious : true, firstToken ? effectiveNoSpaceBefore : true,
                  /*focusBoundary=*/0, tokenVisibleOffset);
        tokenVisibleOffset = sourceOffsetAfter(tokenVisibleOffset, countCodepoints(token));
        firstToken = false;
        tokenStart = breakOffset;
      }
      if (tokenStart < word.size()) {
        pushToken(std::string_view(word).substr(tokenStart), firstToken ? effectiveAttachToPrevious : true,
                  firstToken ? effectiveNoSpaceBefore : true, /*focusBoundary=*/0, tokenVisibleOffset);
      }
      if (wordStartsRtl) {
        hasRtlWord = true;
      }
      return;
    }

    pushToken(word, effectiveAttachToPrevious, effectiveNoSpaceBefore, /*focusBoundary=*/0, visibleTextOffset);
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  // --- FOCUS READING LOGIC BELOW ---

  // Worst case: a segment boundary on each byte (highly punctuated UTF-8 text).
  ensureTokenCapacity(word.length());

  // Lambda helper to process and push individual sub-segments of the string
  // Use std::string_view to avoid heap allocations when slicing
  auto processSegment = [&](std::string_view segment, bool isWord, bool attach, bool noSpaceBefore) {
    const unsigned char* wordBegin = reinterpret_cast<const unsigned char*>(word.data());
    const unsigned char* segmentBegin = reinterpret_cast<const unsigned char*>(segment.data());
    uint32_t segmentLength = 0;
    const unsigned char* offsetPtr = wordBegin;
    while (offsetPtr < segmentBegin) {
      utf8NextCodepoint(&offsetPtr);
      segmentLength++;
    }
    const uint32_t segmentOffset = sourceOffsetAfter(visibleTextOffset, segmentLength);
    if (!isWord) {
      // Punctuation and Numbers stay regular
      pushStyledToken(segment, baseStyle, attach, noSpaceBefore, /*focusBoundary=*/0, segmentOffset, false);
    } else {
      size_t charCount = 0;
      const unsigned char* countPtr = reinterpret_cast<const unsigned char*>(segment.data());
      const unsigned char* countEnd = countPtr + segment.length();

      while (countPtr < countEnd) {
        utf8NextCodepoint(&countPtr);
        charCount++;
      }

      // Target 45% for 1-bold at 4 chars and 3-bold at 7 chars with floor truncation
      constexpr size_t FOCUS_READING_PERCENT = 45;
      size_t targetBoldChars = (charCount * FOCUS_READING_PERCENT) / 100;
      targetBoldChars = std::clamp<size_t>(targetBoldChars, 1, 9);

      if (targetBoldChars >= charCount) {
        // Whole segment is bold - no suffix split needed
        pushStyledToken(segment, static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::BOLD), attach,
                        noSpaceBefore, /*focusBoundary=*/0, segmentOffset, false);
      } else {
        countPtr = reinterpret_cast<const unsigned char*>(segment.data());
        for (size_t i = 0; i < targetBoldChars; ++i) {
          utf8NextCodepoint(&countPtr);
        }
        size_t splitByteOffset = countPtr - reinterpret_cast<const unsigned char*>(segment.data());

        // One token carrying the emphasis as a byte boundary, so the word stays whole for the
        // hyphenator and the line breaker. The renderer applies BOLD to bytes [0, splitByteOffset).
        pushStyledToken(segment, baseStyle, attach, noSpaceBefore,
                        static_cast<uint8_t>(std::min<size_t>(splitByteOffset, 255)), segmentOffset, false);
      }
    }
  };

  // Tokenize the string by alternating states (Word vs. Non-Word)
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  const unsigned char* end = ptr + word.length();

  const unsigned char* segmentStart = ptr;
  uint32_t firstCp = utf8NextCodepoint(&ptr);  // Consume the first char to determine initial state
  bool inWordSegment = isWordCharacter(firstCp);

  bool isFirstSegment = true;

  while (ptr < end) {
    const unsigned char* currentCpStart = ptr;
    uint32_t cp = utf8NextCodepoint(&ptr);
    bool isWordChar = isWordCharacter(cp);

    // Whenever the character type flips, slice off the segment we just completed and process it
    if (isWordChar != inWordSegment) {
      size_t segmentLen = currentCpStart - segmentStart;
      std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);

      // Only the very first segment inherits the original attachToPrevious flag.
      // Every subsequent segment glues seamlessly to the prefix. After a visible explicit-hyphen
      // character, continues=true + noSpaceBefore=true records a breakable attachment: it may wrap,
      // but when it stays on the line it receives kerning only, never a space or justification.
      const bool breakAfterPrev =
          !isFirstSegment && !words.empty() && endsWithBreakableHyphen(wordStore.view(words.back()));
      processSegment(segment, inWordSegment, isFirstSegment ? effectiveAttachToPrevious : true,
                     isFirstSegment ? effectiveNoSpaceBefore : breakAfterPrev);

      // Setup for the next segment
      segmentStart = currentCpStart;
      inWordSegment = isWordChar;
      isFirstSegment = false;
    }
  }

  // Process the final remaining segment
  size_t segmentLen = end - segmentStart;
  std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);
  const bool breakAfterPrev =
      !isFirstSegment && !words.empty() && endsWithBreakableHyphen(wordStore.view(words.back()));
  processSegment(segment, inWordSegment, isFirstSegment ? effectiveAttachToPrevious : true,
                 isFirstSegment ? effectiveNoSpaceBefore : breakAfterPrev);
  if (wordStartsRtl) {
    hasRtlWord = true;
  }
}

uint8_t ParsedText::addLinkTarget(const char* href) {
  if (!href || href[0] == '\0' || strnlen(href, FOOTNOTE_HREF_LEN) >= FOOTNOTE_HREF_LEN ||
      linkTargets.size() >= UINT8_MAX) {
    return 0;
  }
  linkTargets.emplace_back(href);
  return static_cast<uint8_t>(linkTargets.size());
}

bool ParsedText::linkTargetMatches(const uint8_t linkId, const char* href) const {
  return linkId > 0 && linkId <= linkTargets.size() && href && linkTargets[linkId - 1] == href;
}

void ParsedText::setRubyForWordAt(size_t index, const std::string& ruby) {
  if (index >= words.size()) return;
  if (rubyTexts.size() <= index) {
    rubyTexts.resize(words.size());
  }
  rubyTexts[index] = ruby;
}

void ParsedText::setRubyGroupAt(size_t startIndex, size_t count, const std::string& ruby) {
  if (startIndex >= words.size()) return;
  if (rubyTexts.size() <= startIndex) {
    rubyTexts.resize(words.size());
  }
  rubyTexts[startIndex] = ruby;
  for (size_t i = 1; i < count; i++) {
    size_t idx = startIndex + i;
    if (idx >= words.size()) break;
    if (rubyTexts.size() <= idx) {
      rubyTexts.resize(words.size());
    }
    rubyTexts[idx] = "";
    wordStyles[idx] =
        static_cast<EpdFontFamily::Style>(static_cast<uint8_t>(wordStyles[idx]) | EpdFontFamily::RUBY_CONTINUE);
    wordContinues[idx] = true;       // Prevent page breaker from splitting the Group Ruby!
    wordNoSpaceBefore[idx] = false;  // Ensure allowsBreak returns false!
  }
}

void ParsedText::ensureRubyCapacity() {
  // No-op: rubyTexts is a std::deque (chunked growth, no capacity to pre-reserve
  // and no large contiguous reallocation to avoid). Kept for call-site stability.
}

int ParsedText::resolveFirstLineIndent(const bool isFirstLine, const GfxRenderer& renderer, const int fontId) const {
  if (firstLineConsumed || !isFirstLine || !isNaturalAlign) {
    return 0;
  }
  if (blockStyle.textIndentDefined && blockStyle.textIndent < 0) return blockStyle.textIndent;
  if (paragraphIndentSpaces == 0) return 0;
  return scaleSpace(renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR), wordSpacingPercent) * paragraphIndentSpaces;
}
// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& processLine,
                                       const bool includeLastLine, const int8_t characterSpacing,
                                       const uint8_t wordSpacingPercent, const size_t maxLines,
                                       const bool* stopRequested) {
  if (words.empty() || maxLines == 0 || (stopRequested && *stopRequested)) {
    return;
  }
  // Stamped here rather than at construction: the parser replaces blockStyle as CSS resolves.
  blockStyle.characterSpacing = characterSpacing;
  this->wordSpacingPercent = wordSpacingPercent;

  // Per-paragraph RTL auto-detection: only when CSS/HTML didn't explicitly set direction.
  // Explicit dir="ltr" must be respected and not overridden by content heuristic.
  if (!blockStyle.directionDefined && hasRtlWord) {
    // Check the first few words for RTL letter codepoints (no heap allocation).
    const size_t wordsToScan = std::min(words.size(), RTL_PARAGRAPH_PROBE_WORDS);
    for (size_t i = 0; i < wordsToScan; ++i) {
      if (BidiUtils::startsWithRtl(wordStore.cstr(words[i]), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH)) {
        blockStyle.isRtl = true;
        break;
      }
    }
  }

  isNaturalAlign =
      blockStyle.alignment == CssTextAlign::Justify ||
      (blockStyle.isRtl ? blockStyle.alignment == CssTextAlign::Right : blockStyle.alignment == CssTextAlign::Left);

  // Ensure SD card font glyph metrics are loaded before measuring word widths.
  // For flash-based fonts isSdCardFont() returns false and this block is skipped
  // entirely — no heap allocation. For SD card fonts this reads glyph metadata
  // (advanceX only, no bitmaps) for all unique codepoints in this paragraph so
  // that calculateWordWidths() can measure text without on-demand SD I/O.
  // Every size in the paragraph measures through its own font: the block font plus any
  // inline-size slots resolved by the parser.
  int32_t paragraphFonts[MAX_SIZE_SLOTS] = {fontId};
  size_t paragraphFontCount = 1;
  for (uint8_t slot = 1; slot < sizeSlotCount; ++slot) {
    const int32_t slotFont = sizeSlotFontIds[slot];
    if (slotFont != 0 && std::find(paragraphFonts, paragraphFonts + paragraphFontCount, slotFont) ==
                             paragraphFonts + paragraphFontCount) {
      paragraphFonts[paragraphFontCount++] = slotFont;
    }
  }
  const bool anySdFont = std::any_of(paragraphFonts, paragraphFonts + paragraphFontCount,
                                     [&renderer](const int32_t font) { return renderer.isSdCardFont(font); });
  if (anySdFont) {
    // Style mask: only ask the SD font to load advances for styles actually
    // used in this paragraph. Style index is the low two bits (regular/bold/
    // italic/bold-italic); the underline bit is irrelevant to advance metrics.
    uint8_t styleMask = 0;
    for (auto s : wordStyles) {
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(s) & 0x03));
    }
    if (styleMask == 0) styleMask = 0x01;  // defensive: regular only
    const bool includeSmallCaps = std::any_of(wordSizeSlots.begin(), wordSizeSlots.end(), [](const uint8_t slot) {
      return (slot & TextBlock::SMALL_CAPS_SLOT_FLAG) != 0;
    });
    // Hand the arena chunks over as packed NUL-separated word runs. Two small
    // pointer tables (~8 B per live chunk) instead of per-word iteration.
    std::vector<const char*> segments;
    std::vector<size_t> segmentLens;
    segments.reserve(wordStore.chunkCount());
    segmentLens.reserve(wordStore.chunkCount());
    for (size_t i = 0; i < wordStore.chunkCount(); ++i) {
      const char* data = wordStore.chunkData(i);
      if (!data) continue;  // retired chunk
      segments.push_back(data);
      segmentLens.push_back(wordStore.chunkUsed(i));
    }
    for (size_t i = 0; i < paragraphFontCount; ++i) {
      if (!renderer.isSdCardFont(paragraphFonts[i])) continue;
      renderer.ensureSdCardFontReady(paragraphFonts[i], segments.data(), segmentLens.data(), segments.size(),
                                     words.size() > 1, hyphenationEnabled, styleMask, includeSmallCaps);
    }
  }

  const int pageWidth = viewportWidth;
  auto wordWidths = calculateWordWidths(renderer, fontId);

  // Typography Engine: left-to-right text only. A paragraph with a CJK-style zero-width gap (a break with no
  // space) or ruby is left alone, and so is RTL and mixed-direction text. Space condensing also needs justified
  // text. The scan is skipped while the engine is off.
  enginePlainText = false;
  condensePercent = 100;
  punctSpace = 0;
  opticalSpacing = false;
  opticalAverageRoom = 0;
  // The total-fit breaker takes the same plain text, justified, with hyphenation on and no line limit, whether the
  // engine is on or off.
  const bool engineOn = typoengine::storage().load(std::memory_order_relaxed) != 0;
  const bool totalFitWanted = totalFitHyphenation && hyphenationEnabled && maxLines == SIZE_MAX &&
                              blockStyle.alignment == CssTextAlign::Justify;
  bool plainText = false;
  if ((engineOn || totalFitWanted) && !blockStyle.isRtl && !hasRtlWord && rubyTexts.empty()) {
    bool hasZeroWidthGap = false;
    for (size_t i = 0; i < words.size() && !hasZeroWidthGap; ++i) {
      hasZeroWidthGap = wordNoSpaceBefore[i] && !wordContinues[i];
    }
    plainText = !hasZeroWidthGap;
    enginePlainText = engineOn && plainText;
    if (enginePlainText) {
      const typoengine::Config typo = typoengine::current();
      if (blockStyle.alignment == CssTextAlign::Justify) condensePercent = typo.condensePercent;
      punctSpace = typo.punctSpace;
      if (typo.opticalSpacing) {
        // The average side room of the paragraph's ordinary word gaps: a gap is corrected by how far its own side
        // room is from it, so the text keeps its length on average.
        int roomSum = 0;
        int roomCount = 0;
        for (size_t i = 1; i < words.size(); ++i) {
          int room = 0;
          if (wordContinues[i] || wordNoSpaceBefore[i] || narrowedAfter(wordAt(i - 1)) ||
              !gapSideRoom(renderer, fontId, wordAt(i - 1), wordAt(i), getWordStyleAt(i - 1), getWordStyleAt(i),
                           room)) {
            continue;
          }
          roomSum += room;
          ++roomCount;
        }
        if (roomCount > 0) {
          opticalSpacing = true;
          opticalAverageRoom = (roomSum >= 0 ? roomSum + roomCount / 2 : roomSum - roomCount / 2) / roomCount;
        }
      }
    }
  }

  std::vector<size_t> lineBreakIndices;
  if (hyphenationEnabled || maxLines != SIZE_MAX) {
    if (totalFitWanted && plainText) {
      lineBreakIndices =
          computeTotalFitLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, wordNoSpaceBefore);
    }
    // Bounded layout must leave words beyond the emitted lines unsplit for a later width.
    if (lineBreakIndices.empty()) {
      lineBreakIndices = computeHyphenatedLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues,
                                                     wordNoSpaceBefore, maxLines);
    }
  } else {
    lineBreakIndices = computeLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, wordNoSpaceBefore);
  }
  const bool allLinesComputed = lineBreakIndices.back() == words.size();
  const size_t lineCount = lineBreakIndices.size() - (!includeLastLine && allLinesComputed ? 1 : 0);

  const bool endIsKnown = includeLastLine && allLinesComputed;
  for (size_t i = 0; i < lineCount; ++i) {
    if (stopRequested && *stopRequested) return;
    linesAfterCurrent = endIsKnown ? static_cast<int>(lineCount - 1 - i) : -1;
    extractLine(i, pageWidth, wordWidths, wordContinues, wordNoSpaceBefore, lineBreakIndices, processLine, renderer,
                fontId);
  }
  linesAfterCurrent = -1;

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    firstLineConsumed = true;
    paragraphStartEmitted = true;

    const size_t consumed = lineBreakIndices[lineCount - 1];
    for (size_t i = 0; i < consumed; ++i) {
      wordStore.release(words[i]);  // retires arena chunks as lines are consumed
    }
    words.erase(words.begin(), words.begin() + consumed);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + consumed);
    wordContinues.erase(wordContinues.begin(), wordContinues.begin() + consumed);
    wordNoSpaceBefore.erase(wordNoSpaceBefore.begin(), wordNoSpaceBefore.begin() + consumed);
    wordFocusBoundary.erase(wordFocusBoundary.begin(), wordFocusBoundary.begin() + consumed);
    wordLinkIds.erase(wordLinkIds.begin(), wordLinkIds.begin() + consumed);
    wordSizeSlots.erase(wordSizeSlots.begin(), wordSizeSlots.begin() + consumed);
    eraseVisibleOffsetPrefix(consumed);
    if (!rubyTexts.empty()) {
      const size_t rtConsumed = std::min(consumed, rubyTexts.size());
      rubyTexts.erase(rubyTexts.begin(), rubyTexts.begin() + rtConsumed);
    }
  }
}

static inline bool isCjkIdeograph(uint32_t cp) {
  return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0x20000 && cp <= 0x3FFFF);
}

// The first word of a line may have its ruby characters wider than the word (the base text). In that case, we need to
// move the base text to the right a bit so that ruby text doesn't overflow the left border, and it is still centered
// over the base text. This function calculates how much we need to move the base text to the right.
int ParsedText::calculateRubyExtraStartOffset(const size_t wordIdx, const size_t maxWordIdx,
                                              const GfxRenderer& renderer, const int fontId) const {
  if (rubyTexts.empty() || wordIdx >= rubyTexts.size() || rubyTexts[wordIdx].empty() ||
      (wordStyles[wordIdx] & EpdFontFamily::RUBY_CONTINUE) != 0) {
    return 0;
  }

  size_t groupWordCount = 1;
  while (wordIdx + groupWordCount < maxWordIdx &&
         (wordStyles[wordIdx + groupWordCount] & EpdFontFamily::RUBY_CONTINUE) != 0) {
    groupWordCount++;
  }
  int groupActualWidth = 0;
  for (size_t k = 0; k < groupWordCount; ++k) {
    groupActualWidth += measureWordWidth(renderer, fontId, wordAt(wordIdx + k), getWordStyleAt(wordIdx + k),
                                         blockStyle.characterSpacing, wordSpacingPercent);
  }
  const int rubyWidth =
      renderer.getTextAdvanceX(fontId, rubyTexts[wordIdx].c_str(), EpdFontFamily::SUP, blockStyle.characterSpacing);
  if (rubyWidth <= groupActualWidth) {
    return 0;
  }

  const int leftOverlap = (rubyWidth - groupActualWidth) / 2;

  // This function is only ever called for the first word of a line.
  // words[wordIdx - 1], if it exists, is always the last word of the *prior* line
  // and cannot absorb any left overhang on the current line.
  // The full leftOverlap must therefore be reserved as a visual indent so the
  // ruby text does not overflow the left margin.
  return leftOverlap;
}

// The last ruby group on a line may have its ruby characters wider than the group's base text.
// The right half of that overhang protrudes past the last base character. This function returns
// the amount of right-margin space that must be reserved so the ruby does not overflow the right
// border. It mirrors calculateRubyExtraStartOffset: words[lineBreak] is on the *next* line and
// cannot absorb any of the right overhang on the current line, so the full rightOverlap is returned.
int ParsedText::calculateRubyExtraEndOffset(const size_t lineStartIdx, const size_t lineBreakIdx,
                                            const GfxRenderer& renderer, const int fontId) const {
  if (rubyTexts.empty() || lineBreakIdx == 0 || lineStartIdx >= lineBreakIdx) {
    return 0;
  }

  // Walk backwards from the last word to find the leader of the last ruby group on the line.
  size_t leaderIdx = lineBreakIdx - 1;
  while (leaderIdx > lineStartIdx && (wordStyles[leaderIdx] & EpdFontFamily::RUBY_CONTINUE) != 0) {
    leaderIdx--;
  }

  // leaderIdx must be a ruby group leader (non-empty ruby, no RUBY_CONTINUE flag).
  if (leaderIdx >= rubyTexts.size() || rubyTexts[leaderIdx].empty() ||
      (wordStyles[leaderIdx] & EpdFontFamily::RUBY_CONTINUE) != 0) {
    return 0;
  }

  // Measure the group.
  int groupActualWidth = 0;
  for (size_t k = leaderIdx; k < lineBreakIdx; ++k) {
    groupActualWidth += measureWordWidth(renderer, fontId, wordAt(k), getWordStyleAt(k), blockStyle.characterSpacing,
                                         wordSpacingPercent);
  }
  const int rubyWidth =
      renderer.getTextAdvanceX(fontId, rubyTexts[leaderIdx].c_str(), EpdFontFamily::SUP, blockStyle.characterSpacing);
  if (rubyWidth <= groupActualWidth) {
    return 0;
  }

  return (rubyWidth - groupActualWidth) / 2;
}

int ParsedText::wordFontId(const size_t wordIndex, const int blockFontId) const {
  const uint8_t slot = wordSizeSlots[wordIndex] & ~TextBlock::SLOT_FLAGS;
  return slot == 0 || sizeSlotFontIds[slot] == 0 ? blockFontId : sizeSlotFontIds[slot];
}

uint8_t ParsedText::sizeSlotFor(const float scale) {
  uint8_t nearest = 1;
  for (uint8_t slot = 1; slot < sizeSlotCount; ++slot) {
    if (std::fabs(sizeSlotScales[slot] - scale) < 0.01f) return slot;
    if (std::fabs(sizeSlotScales[slot] - scale) < std::fabs(sizeSlotScales[nearest] - scale)) nearest = slot;
  }
  if (sizeSlotCount < MAX_SIZE_SLOTS) {
    sizeSlotScales[sizeSlotCount] = scale;
    return sizeSlotCount++;
  }
  return nearest;
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(words.size());

  for (size_t i = 0; i < words.size(); ++i) {
    wordWidths.push_back(measureFocusWordWidth(renderer, wordFontId(i, fontId), wordAt(i), getWordStyleAt(i),
                                               wordFocusBoundary[i], blockStyle.characterSpacing, wordSpacingPercent));
  }

  // Adjust widths for ruby groups to comply with JLReq standards
  if (!rubyTexts.empty()) {
    struct RubyGroupInfo {
      size_t start;
      size_t count;
      int baseWidth;
      int rubyWidth;
      int leftOverlap;
      int rightOverlap;
    };

    std::vector<RubyGroupInfo> groups;
    for (size_t i = 0; i < words.size(); ++i) {
      if (i < rubyTexts.size() && !rubyTexts[i].empty() && (wordStyles[i] & EpdFontFamily::RUBY_CONTINUE) == 0) {
        RubyGroupInfo g;
        g.start = i;
        g.baseWidth = wordWidths[i];
        g.count = 1;
        while (i + g.count < words.size() && (wordStyles[i + g.count] & EpdFontFamily::RUBY_CONTINUE) != 0) {
          g.baseWidth += wordWidths[i + g.count];
          g.count++;
        }
        g.rubyWidth =
            renderer.getTextAdvanceX(fontId, rubyTexts[i].c_str(), EpdFontFamily::SUP, blockStyle.characterSpacing);
        g.leftOverlap = std::max(0, (g.rubyWidth - g.baseWidth) / 2);
        g.rightOverlap = std::max(0, (g.rubyWidth - g.baseWidth) / 2);
        groups.push_back(g);
        i += g.count - 1;
      }
    }

    // Adjust widths based on adjacent characters and group-to-group spacing
    for (size_t gIdx = 0; gIdx < groups.size(); ++gIdx) {
      const auto& g = groups[gIdx];

      // 1. Preceding character (left overhang)
      if (g.start > 0) {
        const uint32_t cpPrev = lastCodepoint(wordAt(g.start - 1));
        if (isCjkIdeograph(cpPrev)) {
          wordWidths[g.start - 1] += g.leftOverlap;
        } else {
          const int maxLeftOverhang = wordWidths[g.start - 1] / 2;
          wordWidths[g.start - 1] += std::max(0, g.leftOverlap - maxLeftOverhang);
        }
      }

      // 2. Succeeding character (right overhang / group collision)
      const size_t nextIdx = g.start + g.count;
      if (nextIdx < words.size()) {
        if (gIdx + 1 < groups.size() && groups[gIdx + 1].start == nextIdx) {
          // Adjacent ruby groups: compute collision
          const auto& nextG = groups[gIdx + 1];
          const int collision = g.rightOverlap + nextG.leftOverlap;
          if (collision > 0) {
            wordWidths[g.start + g.count - 1] += collision;
          }
        } else {
          // Regular character following: check if it's Kanji
          const uint32_t cpNext = firstCodepoint(wordAt(nextIdx));
          if (isCjkIdeograph(cpNext)) {
            wordWidths[g.start + g.count - 1] += g.rightOverlap;
          } else {
            const int maxRightOverhang = wordWidths[nextIdx] / 2;
            wordWidths[g.start + g.count - 1] += std::max(0, g.rightOverlap - maxRightOverhang);
          }

          // Check if there is another ruby group further ahead separated only by non-ideographs
          if (gIdx + 1 < groups.size()) {
            const auto& nextG = groups[gIdx + 1];
            bool onlyNonIdeographsInBetween = true;
            int gapWidth = 0;
            for (size_t k = nextIdx; k < nextG.start; ++k) {
              const uint32_t cp = firstCodepoint(wordAt(k));
              if (isCjkIdeograph(cp)) {
                onlyNonIdeographsInBetween = false;
                break;
              }
              gapWidth += wordWidths[k];
            }
            if (onlyNonIdeographsInBetween) {
              const int maxRightOverhang = wordWidths[g.start + g.count - 1] / 2;
              const int maxLeftOverhang = wordWidths[nextG.start - 1] / 2;
              const int allowedRight = std::min(g.rightOverlap, maxRightOverhang);
              const int allowedLeft = std::min(nextG.leftOverlap, maxLeftOverhang);
              const int touchOverlap = allowedRight + allowedLeft - gapWidth;
              if (touchOverlap > 0) {
                wordWidths[g.start + g.count - 1] += touchOverlap;
              }
            }
          }
        }
      }
    }
  }

  return wordWidths;
}

// Typography Engine, Space after punctuation: true when the space after this word is narrowed or removed. Such a
// gap is never stretched or shrunk.
bool ParsedText::narrowedAfter(const std::string_view previousWord) const {
  return punctSpace != typoengine::PUNCT_SPACE_NORMAL && typoengine::isSpacedPunctuation(lastCodepoint(previousWord));
}

// Typography Engine, Optical spacing: the empty room beside the two glyphs around a word gap. False when the font
// has no metrics in memory for one of them.
bool ParsedText::gapSideRoom(const GfxRenderer& renderer, const int fontId, const std::string_view previousWord,
                             const std::string_view nextWord, const EpdFontFamily::Style previousStyle,
                             const EpdFontFamily::Style nextStyle, int& room) const {
  int lastLeft = 0;
  int lastRight = 0;
  int firstLeft = 0;
  int firstRight = 0;
  if (!renderer.getGlyphSideBearings(fontId, lastCodepoint(previousWord), previousStyle, lastLeft, lastRight) ||
      !renderer.getGlyphSideBearings(fontId, firstCodepoint(nextWord), nextStyle, firstLeft, firstRight)) {
    return false;
  }
  room = lastRight + firstLeft;
  return true;
}

// The natural width of an ordinary word gap. With the engine's two gap settings off (and in text that is not plain
// left-to-right) it is the font's space with its kerning, scaled by Word spacing, as before.
int ParsedText::spaceGap(const GfxRenderer& renderer, const int fontId, const std::string_view previousWord,
                         const std::string_view nextWord, const EpdFontFamily::Style previousStyle,
                         const EpdFontFamily::Style nextStyle, int* fontSpace) const {
  const int space =
      scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(previousWord), firstCodepoint(nextWord), previousStyle),
                 wordSpacingPercent);
  if (fontSpace != nullptr) *fontSpace = space;
  if (narrowedAfter(previousWord)) return typoengine::punctuationGap(space, punctSpace);
  int room = 0;
  if (opticalSpacing && gapSideRoom(renderer, fontId, previousWord, nextWord, previousStyle, nextStyle, room)) {
    return typoengine::opticalGap(space, room, opticalAverageRoom);
  }
  return space;
}

// Typography Engine: the pixels the ordinary word gap before words[wordIndex] can give up when its line is
// condensed. 0 when condensing is off for this paragraph, and for a locked gap (after an opening quote or a dash).
// `space` is the font's space there (see spaceGap): the gap never ends under typoengine::gapFloor of it.
int ParsedText::condensableShrink(const size_t wordIndex, const int gap, const int space) const {
  if (condensePercent >= 100 || wordIndex == 0) return 0;
  if (narrowedAfter(wordAt(wordIndex - 1))) return 0;
  if (typoengine::current().lockedSpace && typoengine::isLockedSpaceWord(wordAt(wordIndex - 1))) return 0;
  return typoengine::gapAllowance(gap, space, condensePercent);
}

std::vector<size_t> ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                                  std::vector<bool>& noSpaceBeforeVec) {
  if (words.empty()) {
    return {};
  }

  const int firstLineIndent = resolveFirstLineIndent(true, renderer, fontId);

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    // First word needs to fit in reduced width if there's an indent
    const int effectiveWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;
    while (wordWidths[i] > effectiveWidth) {
      if (!hyphenateWordAtIndex(i, effectiveWidth, renderer, fontId, wordWidths, /*allowFallbackBreaks=*/true)) {
        break;
      }
    }
  }

  const size_t totalWordCount = words.size();

  // DP table to store the minimum badness (cost) of lines starting at index i
  std::vector<int> dp(totalWordCount);
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line starting at 'i'
  std::vector<size_t> ans(totalWordCount);

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = 0;
    int shrink = 0;  // what the line's word gaps can give up (Typography Engine, space condensing)
    dp[i] = MAX_COST;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;

    for (size_t j = i; j < totalWordCount; ++j) {
      // Add space before word j, unless it's the first word on the line or a continuation
      int gap = 0;
      if (j > static_cast<size_t>(i) && continuesVec[j]) {
        // Attached and breakable-attached boundaries both use kerning when kept on one line.
        gap = renderer.getKerning(fontId, lastCodepoint(wordAt(j - 1)), firstCodepoint(wordAt(j)),
                                  getWordStyleAt(j - 1), blockStyle.characterSpacing);
      } else if (j > static_cast<size_t>(i) && noSpaceBeforeVec[j]) {
        gap = blockStyle.characterSpacing;
      } else if (j > static_cast<size_t>(i)) {
        int fontSpace = 0;
        gap =
            spaceGap(renderer, fontId, wordAt(j - 1), wordAt(j), getWordStyleAt(j - 1), getWordStyleAt(j), &fontSpace);
        shrink += condensableShrink(j, gap, fontSpace);
      }

      // Calculate extraStartOffset for the first word on the line (i) (protect left margin)
      const int extraStartOffset = (j == i) ? calculateRubyExtraStartOffset(i, totalWordCount, renderer, fontId) : 0;

      currlen += wordWidths[j] + gap + (j == i ? extraStartOffset : 0);

      if (currlen - shrink > effectivePageWidth) {
        break;
      }
      // The paragraph's last line is not justified, so it has to fit without condensing.
      if (j == totalWordCount - 1 && currlen > effectivePageWidth) {
        break;
      }

      // A normal continuation is unbreakable. continues=true + noSpaceBefore=true is the compact
      // breakable-attachment state used after explicit hyphen characters.
      if (j + 1 < totalWordCount && !TokenBoundary::allowsBreak(continuesVec[j + 1], noSpaceBeforeVec[j + 1])) {
        continue;
      }

      const int extraEndOffset = calculateRubyExtraEndOffset(i, j + 1, renderer, fontId);

      if (currlen - shrink + extraEndOffset > effectivePageWidth) {
        continue;  // Cannot split here as it would overflow the right margin
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;  // Last line
      } else {
        const int remainingSpace = effectivePageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      // Favor longer lines when line-breaking costs are equal, to avoid unnecessary short lines in Chinese and Japanese
      // text.
      if (cost <= dp[i]) {
        dp[i] = cost;
        ans[i] = j;  // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word line
    // This prevents cascade failure where one oversized word breaks all preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i;  // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index + 1)
  std::vector<size_t> lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

// Builds break indices while opportunistically splitting the word that would overflow the current line.
std::vector<size_t> ParsedText::computeHyphenatedLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                            const int pageWidth, std::vector<uint16_t>& wordWidths,
                                                            std::vector<bool>& continuesVec,
                                                            std::vector<bool>& noSpaceBeforeVec,
                                                            const size_t maxLines) {
  const int firstLineIndent = resolveFirstLineIndent(true, renderer, fontId);

  std::vector<size_t> lineBreakIndices;
  size_t currentIndex = 0;
  bool isFirstLine = !firstLineConsumed;

  // Typography Engine, loose line balancing (0.77.1): this breaker fills each line as far as it goes, so a long
  // word that can not be split leaves the line before it with few words and wide gaps. When a justified line's gaps
  // would each grow by more than a space, the line above gives it its last word (two at most) if that leaves the
  // looser of the two lines less loose. Only whole words between ordinary spaces move, never a part of a hyphenated
  // word. A line that is not loose is known from the width the loop below has already added up, so it is not
  // measured again (a line that ends in a part of a hyphenated word is, once). For plain left-to-right justified
  // text with Space condensing or Letter spacing on loose lines on, so text with the engine off breaks as before.
  const bool balance = balanceLooseLines && enginePlainText && blockStyle.alignment == CssTextAlign::Justify &&
                       (condensePercent < 100 || typoengine::current().letterSpacing > 0);
  const bool lockedSpaces = balance && typoengine::current().lockedSpace;
  const int looseStretch =
      balance ? std::max(1, scaleSpace(renderer.getSpaceAdvance(fontId, 'a', 'a', EpdFontFamily::REGULAR),
                                       wordSpacingPercent))
              : 0;
  // What each gap of words [start, end) on a line `width` wide would grow by when justified: the spare room over the
  // gaps that stretch. A line with no such gap counts as one gap. `natural` gets the line's unstretched width.
  const auto gapStretch = [&](const size_t start, const size_t end, const int width, int& natural) {
    natural = 0;
    int gaps = 0;
    for (size_t idx = start; idx < end; ++idx) {
      natural += wordWidths[idx];
      if (idx == start) continue;
      if (continuesVec[idx]) {
        natural += renderer.getKerning(fontId, lastCodepoint(wordAt(idx - 1)), firstCodepoint(wordAt(idx)),
                                       getWordStyleAt(idx - 1), blockStyle.characterSpacing);
      } else if (noSpaceBeforeVec[idx]) {
        natural += blockStyle.characterSpacing;
      } else {
        natural +=
            spaceGap(renderer, fontId, wordAt(idx - 1), wordAt(idx), getWordStyleAt(idx - 1), getWordStyleAt(idx));
        if (!narrowedAfter(wordAt(idx - 1)) && !(lockedSpaces && typoengine::isLockedSpaceWord(wordAt(idx - 1)))) {
          ++gaps;
        }
      }
    }
    return std::max(0, width - natural) / std::max(1, gaps);
  };
  size_t previousStart = 0;
  int previousPageWidth = 0;
  bool previousEndsInSplit = false;

  while (currentIndex < wordWidths.size() && lineBreakIndices.size() < maxLines) {
    const size_t lineStart = currentIndex;
    size_t lastFittingBreak = lineStart;
    bool endsInSplit = false;
    int lineGaps = 0;   // the ordinary spaces of the line so far
    int fitWidth = -1;  // the line's width and spaces at lastFittingBreak
    int fitGaps = 0;
    int lineWidth = calculateRubyExtraStartOffset(lineStart, wordWidths.size(), renderer, fontId);
    int lineShrink = 0;  // what the line's word gaps can give up (Typography Engine, space condensing)

    // First line has reduced width due to text-indent
    const int effectivePageWidth = isFirstLine ? pageWidth - firstLineIndent : pageWidth;

    // Consume as many words as possible for current line, splitting when prefixes fit
    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      int spacing = 0;
      int spacingShrink = 0;
      if (!isFirstWord && continuesVec[currentIndex]) {
        // Attached and breakable-attached boundaries both use kerning when kept on one line.
        spacing =
            renderer.getKerning(fontId, lastCodepoint(wordAt(currentIndex - 1)), firstCodepoint(wordAt(currentIndex)),
                                getWordStyleAt(currentIndex - 1), blockStyle.characterSpacing);
      } else if (!isFirstWord && noSpaceBeforeVec[currentIndex]) {
        spacing = blockStyle.characterSpacing;
      } else if (!isFirstWord) {
        int fontSpace = 0;
        spacing = spaceGap(renderer, fontId, wordAt(currentIndex - 1), wordAt(currentIndex),
                           getWordStyleAt(currentIndex - 1), getWordStyleAt(currentIndex), &fontSpace);
        spacingShrink = condensableShrink(currentIndex, spacing, fontSpace);
        ++lineGaps;
      }
      const int candidateWidth = spacing + wordWidths[currentIndex];
      // Condensing: the line may be this much wider than the page, because extractLine takes it back out of the
      // gaps. Not for the paragraph's last word: the last line is not justified, so it has to fit as it is.
      const int condensable = currentIndex + 1 == wordWidths.size() ? 0 : lineShrink + spacingShrink;

      // Word fits on current line
      const int extraEndOffset = calculateRubyExtraEndOffset(lineStart, currentIndex + 1, renderer, fontId);
      if (lineWidth + candidateWidth - condensable <= effectivePageWidth) {
        lineWidth += candidateWidth;
        lineShrink += spacingShrink;
        ++currentIndex;
        if (lineWidth - condensable + extraEndOffset <= effectivePageWidth &&
            (currentIndex == wordWidths.size() ||
             TokenBoundary::allowsBreak(continuesVec[currentIndex], noSpaceBeforeVec[currentIndex]))) {
          lastFittingBreak = currentIndex;
          fitWidth = lineWidth;
          fitGaps = lineGaps;
        }
        continue;
      }

      // Word would overflow — try to split based on hyphenation points
      // A syllable may also use the room that condensing the line's gaps frees. The rest of the word becomes a new
      // word after this one, so this line is not the paragraph's last.
      const int availableWidth = effectivePageWidth - lineWidth - spacing + lineShrink + spacingShrink;
      const bool allowFallbackBreaks = isFirstWord;  // Only for first word on line

      if (lineWidth + candidateWidth > effectivePageWidth && availableWidth > 0 &&
          (hyphenationEnabled || isFirstWord) &&
          hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths, allowFallbackBreaks)) {
        // Prefix now fits; append it to this line and move to next line
        ++currentIndex;
        lastFittingBreak = currentIndex;
        endsInSplit = true;
        break;
      }

      // Could not split: force at least one word per line to avoid infinite loop
      if (currentIndex == lineStart) {
        ++currentIndex;
      }
      break;
    }

    if (lastFittingBreak > lineStart) currentIndex = lastFittingBreak;

    // Don't break before a continuation word (e.g., orphaned "?" after "question").
    // Backtrack to the start of the continuation group so the whole group moves to the next line.
    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() &&
           !TokenBoundary::allowsBreak(continuesVec[currentIndex], noSpaceBeforeVec[currentIndex])) {
      --currentIndex;
    }

    // Loose line balancing (see above). Not for the paragraph's last line, which is not justified.
    // The width added up above already tells for most lines that they are not loose. (Its count of spaces includes
    // the ones that do not stretch, so a line may pass as not loose here that the measuring below would call loose.)
    const bool knownNotLoose = !endsInSplit && fitWidth >= 0 && currentIndex == lastFittingBreak &&
                               std::max(0, effectivePageWidth - fitWidth) / std::max(1, fitGaps) <= looseStretch;
    if (balance && !knownNotLoose && !lineBreakIndices.empty() && !previousEndsInSplit &&
        currentIndex < wordWidths.size()) {
      for (int moved = 0; moved < 2; ++moved) {
        const size_t boundary = lineBreakIndices.back();  // the first word of this line
        // The line above keeps two words at least, and both the gap that becomes its end and the gap that joins the
        // moved word to this line are ordinary spaces.
        if (boundary < previousStart + 3 || boundary >= currentIndex || continuesVec[boundary - 1] ||
            noSpaceBeforeVec[boundary - 1] || continuesVec[boundary] || noSpaceBeforeVec[boundary]) {
          break;
        }
        int natural = 0;
        const int stretch = gapStretch(boundary, currentIndex, effectivePageWidth, natural);
        if (stretch <= looseStretch) break;
        const int movedStretch = gapStretch(boundary - 1, currentIndex, effectivePageWidth, natural);
        if (natural > effectivePageWidth) break;  // the word does not fit here
        const int aboveStretch = gapStretch(previousStart, boundary, previousPageWidth, natural);
        const int aboveMovedStretch = gapStretch(previousStart, boundary - 1, previousPageWidth, natural);
        if (std::max(movedStretch, aboveMovedStretch) >= std::max(stretch, aboveStretch)) break;
        lineBreakIndices.back() = boundary - 1;
      }
    }

    previousStart = lineBreakIndices.empty() ? 0 : lineBreakIndices.back();
    previousPageWidth = effectivePageWidth;
    previousEndsInSplit = endsInSplit && currentIndex == lastFittingBreak;
    lineBreakIndices.push_back(currentIndex);
    isFirstLine = false;
  }

  return lineBreakIndices;
}

// Total-fit line breaking for justified text with hyphenation on (kos25k #30), after Knuth and Plass. The breaker
// above fills each line as far as it goes and splits the word that runs over, so it hyphenates whenever a part of a
// word fits. This one weighs the whole paragraph: a line may end at a word gap or at a hyphenation point inside a
// word, every line gets a badness from how far its gaps have to stretch or shrink, a line that ends in a hyphen
// costs extra, a second such line in a row a little more, and the breaks with the lowest sum win. A line is never
// wider than its gaps can give up (the same allowance as the other breakers: Space condensing down to
// typoengine::gapFloor), and the last line never wider than the page.
//
// The paragraph is also kept as short as filling line by line makes it: the breaks are compared by the number of
// lines first and by the sum of the costs only among those with the same number, so no line is added to even the
// gaps out, however loose a line is. The last line costs by its width, so the text is not pushed down into it. The
// costs were set on simulator pages (Greek and English prose, Fira Sans 16 pt, a 480 px page, with and without Letter
// spacing on loose lines) so that the mean gap is not wider than line by line. That is also why there is no limit on
// hyphenated lines in a row: with a limit of two (or four) such a paragraph took one more line, or a longer last line,
// and its gaps grew.
//
// The parts of a word are measured without touching the word list; only the splits of the chosen breaks are made,
// after the choice. Memory is two tables from one allocation each, taken without an abort when it fails: 16 bytes a
// word and 16 bytes a break (a word gap or a hyphenation point). The table of breaks starts with room for one
// hyphenation point a word and grows by half as the points are found (realloc: in place when the heap allows, no
// abort), up to 1600 points: 32 kB for the largest paragraph of 400 words with four points a word, 16 kB for 400
// words with 600 points. A paragraph gets every point of every word or is not laid out here at all: when the
// table can not grow, or the points pass 1600, it goes to the other breaker, so the end of a long paragraph is
// never left without its hyphenation points.
//
// What the standard library allocates aborts the firmware when it fails (no exceptions), so each such step is only
// taken while a block of the size it needs, and more, can still be had: before the paragraph (4 kB and 16 bytes a
// word), before the hyphenator runs on each word (4 kB for its small lists), and before the splits (the list of
// breaks and the growth of the word lists, which are then reserved in one step). The check and the allocation are
// two steps, so this is a margin and not a promise. Left as it is in the line-by-line breaker too: the word deque
// (and the ruby one) takes a block of about 512 bytes now and then as words are inserted.
//
// Returns no breaks, and leaves the paragraph to computeHyphenatedLineBreaks, when the paragraph has more than
// TOTAL_FIT_MAX_WORDS words, when a table can not be allocated or the room is not there, when no set of breaks
// fits (a word wider than the line with no hyphenation point that helps: the other breaker splits it anywhere), or
// when the prefix of a chosen split can not be stored. In each case the word list is exactly as it was.
std::vector<size_t> ParsedText::computeTotalFitLineBreaks(const GfxRenderer& renderer, const int fontId,
                                                          const int pageWidth, std::vector<uint16_t>& wordWidths,
                                                          const std::vector<bool>& continuesVec,
                                                          const std::vector<bool>& noSpaceBeforeVec) {
  const size_t wordCount = words.size();
  if (wordCount == 0 || wordCount > TOTAL_FIT_MAX_WORDS) return {};

  // sums[i]: the words before word i, and the gaps before each of them.
  struct Sums {
    int32_t width;     // the words
    int32_t gap;       // the gaps at their natural width
    uint32_t letters;  // the places between two letters (Letter spacing on loose lines)
    uint16_t shrink;   // what the gaps can give up
    uint16_t gaps;     // the gaps that stretch when the line is justified
  };
  constexpr uint8_t SPLIT = 1;          // inside a word; else at the gap before word `word`
  constexpr uint8_t HYPHEN = 2;         // the line that ends here shows a hyphen at its end
  constexpr uint32_t MAX_LINES = 1023;  // what Node::lines holds; a paragraph of more lines counts as this many
  struct Node {
    uint32_t word : 9;  // SPLIT: the word that is split. Else the first word of the next line.
    uint32_t flags : 2;
    uint32_t previous : 11;  // the break before this one on the best way here; after the search, the one after
    uint32_t lines : 10;     // the lines on the best way here
    uint16_t prefixWidth;
    uint16_t suffixWidth;
    uint8_t offset;
    uint8_t prefixLetters;  // the places between two letters in the prefix with its hyphen, and in the suffix
    uint8_t suffixLetters;
    float cost;  // the sum of the lines' costs on the best way here
  };
  constexpr size_t MAX_SPLITS = 1600;
  static_assert(sizeof(Node) == 16, "a break is 16 bytes");
  static_assert(TOTAL_FIT_MAX_WORDS < (1u << 9), "Node::word");
  static_assert(TOTAL_FIT_MAX_WORDS + 1 + MAX_SPLITS <= (1u << 11), "Node::previous");
  // In the unit of a line's cost (below): a hyphen costs as much as a line whose gaps each grow by a whole space.
  // Measured on the simulator pages: with 300 there were more hyphens than line by line, with 500 wider gaps.
  constexpr float HYPHEN_DEMERITS = 400.0f;
  constexpr float CONSECUTIVE_DEMERITS = 100.0f;  // more for each further hyphenated line in a row
  constexpr float LOOSE_DEMERITS = 400.0f;        // more for a line whose gaps each grow by more than a space
  constexpr float ORPHAN_DEMERITS = 400.0f;       // the last line is only the end of a hyphenated word
  // The most a line's gaps cost: gaps that each grow by ten spaces. Past that every line is as bad, and a float
  // keeps the sum of a paragraph exact to a whole number up to 16.7 million: 400 lines at this cost.
  constexpr float MAX_BADNESS = 40000.0f;
  constexpr float LAST_LINE_DEMERITS = 1000.0f;  // the last line, by the part of the page width it fills
  const float unreached = std::numeric_limits<float>::infinity();

  const std::unique_ptr<Sums[]> sums(new (std::nothrow) Sums[wordCount + 1]);
  if (!sums) return {};
  const typoengine::Config typo = typoengine::current();
  const bool lockedSpaces = enginePlainText && typo.lockedSpace;
  sums[0] = Sums{0, 0, 0, 0, 0};
  // The search stops looking back from a break once the line is too wide, since a line that starts earlier is
  // wider still. That holds while every gap is wider than what it can give up; a gap that is not (kerning under
  // zero between two parts of a word, negative letter spacing) is summed here, and the search looks back that far
  // past the first line that is too wide. 0 for nearly every paragraph.
  int backSlack = 0;
  for (size_t i = 0; i < wordCount; ++i) {
    Sums next = sums[i];
    const int32_t gapBefore = next.gap;
    const uint16_t shrinkBefore = next.shrink;
    next.width += wordWidths[i];
    if (i > 0 && continuesVec[i]) {
      next.gap += renderer.getKerning(fontId, lastCodepoint(wordAt(i - 1)), firstCodepoint(wordAt(i)),
                                      getWordStyleAt(i - 1), blockStyle.characterSpacing);
    } else if (i > 0 && noSpaceBeforeVec[i]) {
      next.gap += blockStyle.characterSpacing;
    } else if (i > 0) {
      int fontSpace = 0;
      const int gap =
          spaceGap(renderer, fontId, wordAt(i - 1), wordAt(i), getWordStyleAt(i - 1), getWordStyleAt(i), &fontSpace);
      next.gap += gap;
      next.shrink = static_cast<uint16_t>(next.shrink + condensableShrink(i, gap, fontSpace));
      if (!narrowedAfter(wordAt(i - 1)) && !(lockedSpaces && typoengine::isLockedSpaceWord(wordAt(i - 1)))) {
        ++next.gaps;
      }
    }
    const uint32_t letters = countCodepoints(wordAt(i));
    if (letters > 1) next.letters += letters - 1;
    backSlack += std::min(0, static_cast<int>(next.gap - gapBefore) - (next.shrink - shrinkBefore));
    sums[i + 1] = next;
  }

  const int firstWidth = pageWidth - resolveFirstLineIndent(true, renderer, fontId);
  if (sums[wordCount].width + sums[wordCount].gap <= firstWidth) {
    ++totalFitParagraphs;
    return {wordCount};  // one line: nothing to weigh, and no word to hyphenate
  }

  // Is a block of this size still there? Given back at once: the room is for the small allocations that follow.
  const auto roomFor = [](const size_t bytes) {
    const std::unique_ptr<uint8_t[]> probe(new (std::nothrow) uint8_t[bytes]);
    return probe != nullptr;
  };
  constexpr size_t HYPHENATION_HEADROOM = 4096;
  // cppcheck-suppress knownConditionTrueFalse // set by the host tests
  if (failAllocationForTest == 1) return {};

  // The table of breaks. Grown with realloc, which gives nullptr and keeps the block when it fails.
  struct NodeTable {
    Node* at = nullptr;
    size_t capacity = 0;
    NodeTable() = default;
    NodeTable(const NodeTable&) = delete;
    NodeTable& operator=(const NodeTable&) = delete;
    ~NodeTable() { free(at); }
    bool grow(const size_t wanted) {
      void* const grown = realloc(at, wanted * sizeof(Node));
      if (grown == nullptr) return false;
      at = static_cast<Node*>(grown);
      capacity = wanted;
      return true;
    }
  } table;
  const size_t maxNodes = wordCount + 1 + MAX_SPLITS;
  if (!table.grow(std::min(maxNodes, 2 * wordCount + 1))) return {};
  // With the tables taken: the room for the hyphenator, and for the result (a break and the entries of a split
  // word in the word lists are under 16 bytes a word together).
  if (!roomFor(HYPHENATION_HEADROOM + wordCount * 16)) return {};
  size_t nodeCount = 0;
  // False when the table is full and can not grow.
  const auto addNode = [&](const size_t word, const size_t offset, const uint8_t flags, const int prefixWidth,
                           const int suffixWidth) {
    if (nodeCount == table.capacity &&
        (nodeCount == maxNodes || !table.grow(std::min(maxNodes, nodeCount + nodeCount / 2 + 16)))) {
      return false;
    }
    Node& node = table.at[nodeCount++];
    node.prefixLetters = node.suffixLetters = 0;
    node.word = static_cast<uint32_t>(word);
    node.offset = static_cast<uint8_t>(offset);
    node.flags = flags;
    node.prefixWidth = static_cast<uint16_t>(prefixWidth);
    node.suffixWidth = static_cast<uint16_t>(suffixWidth);
    node.cost = unreached;
    node.lines = MAX_LINES;
    node.previous = 0;
    return true;
  };
  if (!addNode(0, 0, 0, 0, 0)) return {};  // the start of the paragraph
  table.at[0].cost = 0.0f;
  table.at[0].lines = 0;
  // The hyphenator works on a std::string and returns vectors, whose allocations abort the firmware when they
  // fail. So a word is only hyphenated while a block of HYPHENATION_HEADROOM bytes can still be had. The string is
  // one for every word and holds the longest word the parser makes from the start (a longer one is left whole);
  // the lists are new ones for each word (a few entries, given back before the next word).
  std::string word;
  word.reserve(208);
  for (size_t w = 0; w < wordCount; ++w) {
    // The hyphenation points of the word, as hyphenateWordAtIndex finds them (without the every-N-letters fallback).
    if (words[w].len >= 4 && words[w].len <= 200) {
      if (!roomFor(HYPHENATION_HEADROOM)) return {};
      word.assign(wordAt(w));
      std::vector<Hyphenator::BreakInfo> breakInfos = hangulLineEndBreaks(word);
      {
        const auto hyphenBreaks = Hyphenator::breakOffsets(word, /*includeFallback=*/false);
        breakInfos.insert(breakInfos.end(), hyphenBreaks.begin(), hyphenBreaks.end());
      }
      // Hangul breaks stay first at the same offset, as in hyphenateWordAtIndex. (Not std::stable_sort, which
      // takes a buffer from the heap: the list is a few entries, sorted by insertion.)
      for (size_t i = 1; i < breakInfos.size(); ++i) {
        for (size_t k = i; k > 0 && breakInfos[k].byteOffset < breakInfos[k - 1].byteOffset; --k) {
          std::swap(breakInfos[k], breakInfos[k - 1]);
        }
      }
      const auto style = getWordStyleAt(w);
      const uint8_t focusBoundary = wordFocusBoundary[w];
      const int wordFont = wordFontId(w, fontId);
      const std::string_view view{word};
      size_t lastOffset = 0;
      int widthAfter = wordWidths[w];  // of the word from the point before: it only gets narrower
      for (const auto& info : breakInfos) {
        const size_t offset = info.byteOffset;
        // An offset over 200 bytes is past what a split can store (see storeSplitPrefix).
        if (offset <= lastOffset || offset >= word.size() || offset > 200) continue;
        lastOffset = offset;
        const int prefixWidth = measureFocusWordWidth(
            renderer, wordFont, view.substr(0, offset), style, focusBoundaryBefore(focusBoundary, offset),
            blockStyle.characterSpacing, wordSpacingPercent, info.requiresInsertedHyphen);
        const int suffixWidth = measureFocusWordWidth(renderer, wordFont, view.substr(offset), style,
                                                      focusBoundaryAfter(focusBoundary, offset),
                                                      blockStyle.characterSpacing, wordSpacingPercent);
        if (!addNode(w, offset, info.requiresInsertedHyphen ? SPLIT | HYPHEN : SPLIT, prefixWidth, suffixWidth)) {
          return {};
        }
        backSlack += std::min(0, widthAfter - suffixWidth);
        widthAfter = suffixWidth;
        // As extractLine counts them: one place less than the letters of each part, the hyphen being a letter.
        const uint32_t prefixLetters = countCodepoints(view.substr(0, offset)) + (info.requiresInsertedHyphen ? 1 : 0);
        const uint32_t suffixLetters = countCodepoints(view.substr(offset));
        table.at[nodeCount - 1].prefixLetters = static_cast<uint8_t>(std::min<uint32_t>(prefixLetters - 1, UINT8_MAX));
        table.at[nodeCount - 1].suffixLetters = static_cast<uint8_t>(std::min<uint32_t>(suffixLetters - 1, UINT8_MAX));
      }
    }
    if (w + 1 == wordCount) {
      if (!addNode(wordCount, 0, 0, 0, 0)) return {};  // the end of the paragraph
    } else if (TokenBoundary::allowsBreak(continuesVec[w + 1], noSpaceBeforeVec[w + 1])) {
      // A break after a hyphen the word already has ("well-" | "known") shows a hyphen at the line end too.
      const bool afterHyphen = continuesVec[w + 1] && noSpaceBeforeVec[w + 1] && endsWithBreakableHyphen(wordAt(w));
      if (!addNode(w + 1, 0, afterHyphen ? HYPHEN : 0, 0, 0)) return {};
    }
  }
  Node* const nodes = table.at;

  const int space =
      std::max(1, scaleSpace(renderer.getSpaceAdvance(fontId, 'a', 'a', EpdFontFamily::REGULAR), wordSpacingPercent));
  const int halfSpace = std::max(1, space / 2);  // what one gap stretches by at a badness of 100
  const int letterCap =
      enginePlainText && typo.letterSpacing > 0 ? typoengine::letterSpacingCap(typo.letterSpacing, space) : 0;

  const int widestTarget = std::max(firstWidth, pageWidth);  // the first line is the wider one with a hanging indent
  for (size_t b = 1; b < nodeCount; ++b) {
    Node& to = nodes[b];
    const bool lastLine = b + 1 == nodeCount;
    const bool toSplit = (to.flags & SPLIT) != 0;
    const bool toHyphen = (to.flags & HYPHEN) != 0;
    const size_t toWord = to.word;
    const Sums& lineEnd = sums[toSplit ? toWord + 1 : toWord];
    for (size_t a = b; a-- > 0;) {
      const Node& from = nodes[a];
      const bool fromSplit = (from.flags & SPLIT) != 0;
      const size_t fromWord = from.word;
      if (fromSplit && toSplit && fromWord == toWord) continue;  // the middle of a word is not measured
      const size_t firstWhole = fromSplit ? fromWord + 1 : fromWord;
      const Sums& lineStart = sums[fromWord + 1];
      int width = (fromSplit ? from.suffixWidth : 0) + (toSplit ? to.prefixWidth : 0) + (lineEnd.gap - lineStart.gap);
      if (toWord > firstWhole) width += sums[toWord].width - sums[firstWhole].width;
      // The last line is not justified, so it has to fit as it is.
      const int shrink = lastLine ? 0 : lineEnd.shrink - lineStart.shrink;
      const int target = a == 0 ? firstWidth : pageWidth;
      if (width - shrink > target) {
        // Too wide. A line from an earlier break is wider still, but for backSlack and a wider first line.
        if (width - shrink + backSlack > widestTarget) break;
        continue;
      }

      // The line's cost: 100 for gaps that each stretch by half a space or shrink all the way, growing with the
      // square (up to MAX_BADNESS), so the sum is lowest when the lines are evenly loose.
      float demerits = toHyphen ? HYPHEN_DEMERITS : 0.0f;
      const int spare = target - width;
      if (lastLine) {
        demerits += LAST_LINE_DEMERITS * static_cast<float>(width) / static_cast<float>(target);
        if (fromSplit && firstWhole == wordCount) demerits += ORPHAN_DEMERITS;
      } else if (spare < 0) {
        const float ratio = static_cast<float>(-spare) / static_cast<float>(shrink);
        demerits += 100.0f * ratio * ratio;
      } else if (spare > 0) {
        // A line with no gap that stretches counts as one gap, as in the loose line balancing.
        const int gaps = std::max(1, lineEnd.gaps - lineStart.gaps);
        // The places between two letters of the line's words, whole and split, as extractLine counts them.
        int letterSlots = (fromSplit ? from.suffixLetters : 0) + (toSplit ? to.prefixLetters : 0);
        if (toWord > firstWhole) letterSlots += static_cast<int>(sums[toWord].letters - sums[firstWhole].letters);
        const int letterRoom = letterCap * letterSlots;
        const float ratio = static_cast<float>(spare) / static_cast<float>(gaps * halfSpace + letterRoom);
        demerits += std::min(100.0f * ratio * ratio, MAX_BADNESS);
        if (spare - letterRoom > gaps * space) demerits += LOOSE_DEMERITS;
      }

      if (from.cost == unreached) continue;
      if (toHyphen && (from.flags & HYPHEN) != 0) demerits += CONSECUTIVE_DEMERITS;
      // Fewer lines first, then the lower cost: no badness, however large, buys a line.
      const uint32_t lines = std::min<uint32_t>(from.lines + 1u, MAX_LINES);
      const float cost = from.cost + demerits;
      if (lines < to.lines || (lines == to.lines && cost < to.cost)) {
        to.lines = lines;
        to.cost = cost;
        to.previous = static_cast<uint32_t>(a);
      }
    }
  }

  if (nodes[nodeCount - 1].cost == unreached) return {};

  // The chosen breaks: the way back from the end is turned round in the table itself (`previous` becomes the break
  // after), so no list of them is allocated.
  size_t lineCount = 0;
  size_t chosenSplits = 0;
  for (size_t at = nodeCount - 1, after = 0; at != 0;) {
    const size_t before = nodes[at].previous;
    nodes[at].previous = static_cast<uint32_t>(after);
    ++lineCount;
    chosenSplits += (nodes[at].flags & SPLIT) != 0 ? 1 : 0;
    after = at;
    at = before;
    if (at == 0) nodes[0].previous = static_cast<uint32_t>(after);
  }
  // Every prefix of a chosen split is stored first: that is the one step of a split that can fail, and when one
  // does the prefixes stored so far are given back and the word list is as it was. Nothing is split by halves.
  std::unique_ptr<WordStore::StoredWord[]> prefixes;
  const auto releasePrefixes = [&](size_t stored) {
    while (stored > 0) wordStore.release(prefixes[--stored]);
  };
  size_t stored = 0;
  if (chosenSplits > 0) {
    prefixes.reset(new (std::nothrow) WordStore::StoredWord[chosenSplits]);
    if (!prefixes) return {};
    for (size_t at = nodes[0].previous; at != 0; at = nodes[at].previous) {
      const Node& node = nodes[at];
      if ((node.flags & SPLIT) == 0) continue;
      // cppcheck-suppress knownConditionTrueFalse // not so: a test sets failSplitForTest
      if (failSplitForTest == stored + 1 ||
          !storeSplitPrefix(node.word, node.offset, (node.flags & HYPHEN) != 0, prefixes[stored])) {
        releasePrefixes(stored);
        return {};
      }
      ++stored;
    }
  }
  // The list of breaks and the word lists grow next, and that can not fail softly. So first: is there room for all
  // of it at once, and as much again? Then the lists that are one block each are grown in one step, and a split
  // moves their entries without allocating. (The word deque and the ruby deque grow by a block of about 512 bytes
  // now and then; that is as in the line-by-line breaker.)
  const size_t wordsAfterSplits = wordCount + chosenSplits;
  const size_t growth = lineCount * sizeof(size_t) +
                        wordsAfterSplits * (sizeof(uint16_t) + sizeof(EpdFontFamily::Style) + 3 * sizeof(uint8_t) + 1) +
                        chosenSplits * (sizeof(WordStore::StoredWord) + sizeof(std::string));
  // cppcheck-suppress knownConditionTrueFalse // not so: a test sets failAllocationForTest
  if (failAllocationForTest == 2 || !roomFor(2 * growth + 1024)) {
    releasePrefixes(stored);
    return {};
  }
  std::vector<size_t> lineBreakIndices;
  lineBreakIndices.reserve(lineCount);
  if (chosenSplits > 0) {
    wordWidths.reserve(wordWidths.size() + chosenSplits);
    wordStyles.reserve(wordStyles.size() + chosenSplits);
    wordFocusBoundary.reserve(wordFocusBoundary.size() + chosenSplits);
    wordLinkIds.reserve(wordLinkIds.size() + chosenSplits);
    wordSizeSlots.reserve(wordSizeSlots.size() + chosenSplits);
    wordContinues.reserve(wordContinues.size() + chosenSplits);
    wordNoSpaceBefore.reserve(wordNoSpaceBefore.size() + chosenSplits);
  }
  // Make the splits in reading order, as the other breaker does. Each one adds a word before the breaks after it.
  size_t added = 0;
  for (size_t at = nodes[0].previous; at != 0; at = nodes[at].previous) {
    const Node& node = nodes[at];
    if ((node.flags & SPLIT) == 0) {
      lineBreakIndices.push_back(node.word + added);
      continue;
    }
    const size_t wordIndex = node.word + added;
    splitWordAt(wordIndex, node.offset, (node.flags & HYPHEN) != 0, prefixes[added], node.prefixWidth, renderer, fontId,
                wordWidths);
    ++added;
    lineBreakIndices.push_back(wordIndex + 1);
  }
  ++totalFitParagraphs;
  return lineBreakIndices;
}

// Splits words[wordIndex] into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint fits the
// available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  // Stable copy: Hyphenator and the prefix measurements below want std::string
  // semantics. Bounded by one word.
  const std::string word{wordAt(wordIndex)};  // brace-init: `word(` is an Arduino macro
  const auto style = getWordStyleAt(wordIndex);

  const uint8_t focusBoundary = wordFocusBoundary[wordIndex];

  // Collect candidate breakpoints (byte offsets and hyphen requirements). Focus emphasis is a byte
  // annotation, so the hyphenator sees the whole word and every legal break is reachable.
  // Hangul breaks come first so they win a tie against a hyphenated break at the same width.
  auto breakInfos = hangulLineEndBreaks(word);
  const auto hyphenBreaks = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  breakInfos.insert(breakInfos.end(), hyphenBreaks.begin(), hyphenBreaks.end());
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  // Iterate over each legal breakpoint and retain the widest prefix that still fits.
  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth = measureFocusWordWidth(renderer, wordFontId(wordIndex, fontId), word.substr(0, offset),
                                                  style, focusBoundaryBefore(focusBoundary, offset),
                                                  blockStyle.characterSpacing, wordSpacingPercent, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;  // Skip if too wide or not an improvement
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    // No hyphenation point produced a prefix that fits in the remaining space.
    return false;
  }
  WordStore::StoredWord prefixStored;
  if (!storeSplitPrefix(wordIndex, chosenOffset, chosenNeedsHyphen, prefixStored)) {
    // OOM: skip the split; the word stays whole and the line breaks without it.
    return false;
  }
  splitWordAt(wordIndex, chosenOffset, chosenNeedsHyphen, prefixStored, chosenWidth, renderer, fontId, wordWidths);
  return true;
}

// The first half of a split, and the only part that can fail: the prefix of words[wordIndex] up to a byte offset
// (with its visible hyphen, so it stays NUL-terminated) as a fresh arena entry. Nothing else changes, so a caller
// that needs several splits can store every prefix first and give them back (wordStore.release) when one fails.
bool ParsedText::storeSplitPrefix(const size_t wordIndex, const size_t offset, const bool needsHyphen,
                                  WordStore::StoredWord& prefixStored) {
  if (wordIndex >= words.size() || offset == 0 || offset >= words[wordIndex].len) {
    return false;
  }
  // Stack buffer, not std::string: this runs mid-pagination, exactly when the
  // heap is under section-build pressure. The parser caps words at
  // MAX_WORD_SIZE (200) bytes; anything larger skips the split rather than
  // overflow (the word then breaks whole, as when no breakpoint fits).
  char prefixBuf[208];
  if (offset + 1 > sizeof(prefixBuf)) {
    return false;
  }
  memcpy(prefixBuf, wordAt(wordIndex).data(), offset);
  size_t prefixLen = offset;
  if (needsHyphen) {
    prefixBuf[prefixLen++] = '-';
  }
  return wordStore.append(prefixBuf, prefixLen, prefixStored);
}

// The second half: words[wordIndex] becomes the stored prefix (`chosenWidth` wide) and the rest of the word a new
// word after it. The remainder aliases the original word's tail bytes and inherits the original entry's release
// obligation via WordStore::suffix().
void ParsedText::splitWordAt(const size_t wordIndex, const size_t chosenOffset, const bool chosenNeedsHyphen,
                             const WordStore::StoredWord& prefixStored, const int chosenWidth,
                             const GfxRenderer& renderer, const int fontId, std::vector<uint16_t>& wordWidths) {
  const auto style = getWordStyleAt(wordIndex);
  const uint8_t focusBoundary = wordFocusBoundary[wordIndex];

  uint32_t prefixLength = 0;
  const unsigned char* offsetPtr = reinterpret_cast<const unsigned char*>(wordAt(wordIndex).data());
  const unsigned char* splitPtr = offsetPtr + chosenOffset;
  while (offsetPtr < splitPtr) {
    utf8NextCodepoint(&offsetPtr);
    prefixLength++;
  }
  const uint32_t remainderOffset = sourceOffsetAfter(visibleOffsetAt(wordIndex), prefixLength);

  const WordStore::StoredWord remainderStored = WordStore::suffix(words[wordIndex], chosenOffset);
  words[wordIndex] = prefixStored;
  const auto storedStyle = static_cast<EpdFontFamily::Style>(style & ~EpdFontFamily::SMALL_CAPS);
  wordStyles[wordIndex] =
      static_cast<EpdFontFamily::Style>(storedStyle | (chosenNeedsHyphen ? TextBlock::DISCRETIONARY_HYPHEN_FLAG : 0));

  // Insert the remainder word (with matching style and continuation flag) directly after the prefix.
  words.insert(words.begin() + wordIndex + 1, remainderStored);
  wordStyles.insert(wordStyles.begin() + wordIndex + 1, storedStyle);
  insertVisibleOffset(wordIndex + 1, remainderOffset);
  // Emphasis follows the text across the split, so a break at or after the boundary leaves the
  // remainder fully regular.
  wordFocusBoundary.insert(wordFocusBoundary.begin() + wordIndex + 1, focusBoundaryAfter(focusBoundary, chosenOffset));
  wordLinkIds.insert(wordLinkIds.begin() + wordIndex + 1, wordLinkIds[wordIndex]);
  wordSizeSlots.insert(wordSizeSlots.begin() + wordIndex + 1, wordSizeSlots[wordIndex]);
  wordFocusBoundary[wordIndex] = focusBoundaryBefore(focusBoundary, chosenOffset);
  // Invariant: a boundary is always strictly inside its token, so an all-bold part carries BOLD in
  // its style with boundary 0 and nothing downstream special-cases boundary == size.
  // An inserted '-' counts as covered: measureFocusWordWidth() measured the
  // hyphen bold whenever the boundary spans the whole text before it, so the
  // rendered styling must match or justified spacing drifts by the bold/
  // regular hyphen advance delta.
  if (wordFocusBoundary[wordIndex] + (chosenNeedsHyphen ? 1u : 0u) >= words[wordIndex].len) {
    wordStyles[wordIndex] = static_cast<EpdFontFamily::Style>(wordStyles[wordIndex] | EpdFontFamily::BOLD);
    wordFocusBoundary[wordIndex] = 0;
  }
  if (wordIndex + 1 <= rubyTexts.size()) {
    rubyTexts.insert(rubyTexts.begin() + wordIndex + 1, "");
  }

  // Continuation flag handling after splitting a word into prefix + remainder.
  //
  // The prefix keeps the original word's continuation flag so that no-break-space groups
  // stay linked. The remainder always gets continues=false because it starts on the next
  // line and is not attached to the prefix.
  //
  // Example: "200&#xA0;Quadratkilometer" produces tokens:
  //   [0] "200"               continues=false
  //   [1] " "                 continues=true
  //   [2] "Quadratkilometer"  continues=true   <-- the word being split
  //
  // After splitting "Quadratkilometer" at "Quadrat-" / "kilometer":
  //   [0] "200"         continues=false
  //   [1] " "           continues=true
  //   [2] "Quadrat-"    continues=true   (KEPT — still attached to the no-break group)
  //   [3] "kilometer"   continues=false  (NEW — starts fresh on the next line)
  //
  // This lets the backtracking loop keep the entire prefix group ("200 Quadrat-") on one
  // line, while "kilometer" moves to the next line.
  // wordContinues[wordIndex] is intentionally left unchanged — the prefix keeps its original attachment.
  wordContinues.insert(wordContinues.begin() + wordIndex + 1, false);
  wordNoSpaceBefore.insert(wordNoSpaceBefore.begin() + wordIndex + 1, false);

  // Update cached widths to reflect the new prefix/remainder pairing.
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth =
      measureFocusWordWidth(renderer, wordFontId(wordIndex, fontId), wordStore.view(remainderStored), style,
                            wordFocusBoundary[wordIndex + 1], blockStyle.characterSpacing, wordSpacingPercent);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const std::vector<uint16_t>& wordWidths,
                             const std::vector<bool>& continuesVec, const std::vector<bool>& noSpaceBeforeVec,
                             const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& processLine,
                             const GfxRenderer& renderer, const int fontId) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  // A no-break space carried across a soft wrap must not indent the next line.
  if (breakIndex > 0 || paragraphStartEmitted) {
    while (lastBreakAt + 1 < lineBreak && continuesVec[lastBreakAt] && wordAt(lastBreakAt) == " ") {
      ++lastBreakAt;
    }
  }
  const size_t lineWordCount = lineBreak - lastBreakAt;
  const uint32_t lineVisibleOffset = visibleOffsetAt(lastBreakAt);

  const int firstLineIndent = resolveFirstLineIndent(breakIndex == 0, renderer, fontId);

  std::vector<std::string> lineRubyTexts(lineWordCount);
  if (!rubyTexts.empty() && lastBreakAt < rubyTexts.size()) {
    const size_t copyCount = std::min(lineBreak, rubyTexts.size()) - lastBreakAt;
    std::copy(rubyTexts.begin() + lastBreakAt, rubyTexts.begin() + lastBreakAt + copyCount, lineRubyTexts.begin());
  }

  const int extraStartOffset = calculateRubyExtraStartOffset(lastBreakAt, lineBreak, renderer, fontId);
  const int extraEndOffset = calculateRubyExtraEndOffset(lastBreakAt, lineBreak, renderer, fontId);

  std::vector<std::string> lineWords;
  lineWords.reserve(lineWordCount);
  std::vector<EpdFontFamily::Style> lineWordStyles;
  lineWordStyles.reserve(lineWordCount);

  for (size_t i = 0; i < lineWordCount; ++i) {
    // Copy out of the arena: TextBlock construction and bidi reorder below
    // want owning strings. Bounded by one line; the arena bytes are released
    // by the consumed-prefix pass in layoutAndExtractLines.
    std::string word{wordAt(lastBreakAt + i)};  // brace-init: `word(` is an Arduino macro
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
    lineWords.push_back(std::move(word));
    // TextBlock keeps small caps with its word fonts (see attachWordFonts).
    lineWordStyles.push_back(
        static_cast<EpdFontFamily::Style>(getWordStyleAt(lastBreakAt + i) & ~EpdFontFamily::SMALL_CAPS));
  }

  // Calculate total word width for this line, count actual word gaps,
  // and accumulate total natural gap widths (including space kerning adjustments).
  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;
  int totalNaturalGaps = 0;

  // Typography Engine. Its line-level parts apply to plain left-to-right text only (see layoutAndExtractLines).
  const typoengine::Config typo = typoengine::current();
  const bool plainLtr = enginePlainText;
  // Locked space: lockedGap[i] is set when the gap before word i (a space, or a no-break space token) follows a
  // word made of opening quotes or dashes. Such a gap keeps its width: it is not counted as a gap to justify.
  std::vector<bool> lockedGap;
  if ((typo.lockedSpace || punctSpace != 0) && plainLtr) lockedGap.assign(lineWordCount, false);
  const auto isLockedGap = [&lockedGap](const size_t idx) { return idx < lockedGap.size() && lockedGap[idx]; };
  // Letter spacing on a loose line swaps in its own widths and tracking for this line (below).
  const uint16_t* lineWidths = wordWidths.data() + lastBreakAt;
  int8_t lineSpacing = blockStyle.characterSpacing;
  int maxGapShrink = 0;  // the most one ordinary gap of this line may give up when condensed
  // What each ordinary gap can give up, in line order, when this line may be condensed: the shrink is shared out so
  // that no gap gives up more than its own share (a narrow gap next to a wide one must never go below its limit).
  std::vector<int> gapAllowances;
  if (condensePercent < 100 && plainLtr) gapAllowances.reserve(lineWordCount);

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineWordWidthSum += lineWidths[wordIdx];
    if (wordIdx == 0) continue;
    const size_t boundaryIdx = lastBreakAt + wordIdx;
    const bool isStretchableSpace =
        lineWords[wordIdx] == " " && wordIdx + 1 < lineWordCount && continuesVec[boundaryIdx + 1];
    const bool justifiable = TokenBoundary::isJustifiableGap(continuesVec[boundaryIdx], isStretchableSpace);
    if (justifiable && !lockedGap.empty() && (continuesVec[boundaryIdx] || !noSpaceBeforeVec[boundaryIdx]) &&
        typo.lockedSpace && typoengine::isLockedSpaceWord(lineWords[wordIdx - 1])) {
      lockedGap[wordIdx] = true;
    }
    // Space after punctuation: the narrowed (or removed) space keeps its width too.
    if (justifiable && !lockedGap.empty() && !continuesVec[boundaryIdx] && !noSpaceBeforeVec[boundaryIdx] &&
        narrowedAfter(lineWords[wordIdx - 1])) {
      lockedGap[wordIdx] = true;
    }
    if (justifiable && !isLockedGap(wordIdx)) {
      actualGapCount++;
    }
    if (continuesVec[boundaryIdx]) {
      totalNaturalGaps +=
          renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx - 1]), firstCodepoint(lineWords[wordIdx]),
                              lineWordStyles[wordIdx - 1], blockStyle.characterSpacing);
    } else if (noSpaceBeforeVec[boundaryIdx]) {
      totalNaturalGaps += blockStyle.characterSpacing;
    } else {
      int fontSpace = 0;
      const int gap = spaceGap(renderer, fontId, lineWords[wordIdx - 1], lineWords[wordIdx],
                               lineWordStyles[wordIdx - 1], lineWordStyles[wordIdx], &fontSpace);
      totalNaturalGaps += gap;
      if (!isLockedGap(wordIdx)) {
        const int allowance = typoengine::gapAllowance(gap, fontSpace, condensePercent);
        maxGapShrink = std::max(maxGapShrink, allowance);
        if (condensePercent < 100 && plainLtr) gapAllowances.push_back(allowance);
      }
    }
  }

  // Calculate spacing (account for indent reducing effective page width on first line)
  const int effectivePageWidth = pageWidth - firstLineIndent;
  const bool isLastLine = lineBreak == words.size();

  // For RTL, implicit/default Left alignment becomes Right alignment.
  // Explicit text-align:left must remain left for CSS correctness.
  const CssTextAlign effectiveAlignment =
      (blockStyle.isRtl && !blockStyle.textAlignDefined && blockStyle.alignment == CssTextAlign::Left)
          ? CssTextAlign::Right
          : blockStyle.alignment;

  // Hanging punctuation. LTR, left-aligned or justified lines only: a centered or right-aligned line has no fixed
  // left text edge to align to, and RTL/bidi lines mirror the margins (left as is). Line breaking is unchanged;
  // the hang moves the line left by hangLeft and, when justifying, stretches it to end hangRight past the right
  // edge. The hang lands in the cached word positions, so the draw path needs no change. On a left-aligned line a
  // trailing hang would move nothing, so only the left hang applies there.
  int hangLeft = 0;
  int hangRight = 0;
  if (textfeatures::has(textfeatures::HANGING) && lineWordCount > 0 && !blockStyle.isRtl && !hasRtlWord &&
      (effectiveAlignment == CssTextAlign::Left || effectiveAlignment == CssTextAlign::Justify)) {
    const uint32_t firstCp = firstCodepoint(lineWords.front());
    if (isHangingOpener(firstCp)) hangLeft = hangWidth(renderer, fontId, firstCp, lineWordStyles.front());
    const uint32_t lastCp = lastCodepoint(lineWords.back());
    if (effectiveAlignment == CssTextAlign::Justify && !isLastLine && isHangingCloser(lastCp)) {
      hangRight = hangWidth(renderer, fontId, lastCp, lineWordStyles.back());
    }
  }

  // For justified text, compute per-gap extra to distribute remaining space evenly.
  // extraEndOffset reserves space for any ruby group at the right edge of the line.
  int spareSpace = effectivePageWidth + hangLeft + hangRight - extraStartOffset - extraEndOffset - lineWordWidthSum -
                   totalNaturalGaps;
  // Even word gaps: a line that would need more stretch than half a space (Slight) or one space
  // (Medium) per gap stays ragged instead.
  const int naturalSpace =
      scaleSpace(renderer.getSpaceAdvance(fontId, 'a', 'a', EpdFontFamily::REGULAR), wordSpacingPercent);
  const int maxJustifyExtra = evenWordGaps == 0   ? INT_MAX
                              : evenWordGaps == 1 ? std::max(1, naturalSpace / 2)
                                                  : std::max(1, naturalSpace);

  // Typography Engine, letter spacing on a loose line: when the gaps of a justified line would each grow by more
  // than half a space, the spare room goes between the letters first (as many whole pixels each as fit, at most
  // typoengine::letterSpacingCap) and the gaps take the rest. Not on the paragraph's last line, or in text with ruby,
  // CJK gaps, RTL or mixed direction. The words are measured again with the wider tracking, and a spacing that would
  // not fit is stepped down, so the line never passes the right edge.
  std::vector<uint16_t> spacedWidths;
  if (typo.letterSpacing > 0 && plainLtr && effectiveAlignment == CssTextAlign::Justify && !isLastLine &&
      actualGapCount > 0 && spareSpace > 0) {
    size_t letterSlots = 0;
    for (const auto& lineWord : lineWords) {
      const uint32_t letters = countCodepoints(lineWord);
      if (letters > 1) letterSlots += letters - 1;
    }
    const auto split =
        typoengine::splitLooseLine(spareSpace, actualGapCount, letterSlots, std::max(1, naturalSpace / 2),
                                   typoengine::letterSpacingCap(typo.letterSpacing, naturalSpace));
    for (int extra = split.perLetter; extra > 0; --extra) {
      const int spacing = blockStyle.characterSpacing + extra;
      if (spacing > INT8_MAX) continue;
      spacedWidths.resize(lineWordCount);
      int widthSum = 0;
      int gaps = 0;
      for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
        const size_t logical = lastBreakAt + wordIdx;
        spacedWidths[wordIdx] =
            measureFocusWordWidth(renderer, wordFontId(logical, fontId), wordAt(logical), getWordStyleAt(logical),
                                  wordFocusBoundary[logical], static_cast<int8_t>(spacing), wordSpacingPercent);
        widthSum += spacedWidths[wordIdx];
        if (wordIdx == 0) continue;
        if (continuesVec[logical]) {
          gaps += renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx - 1]), firstCodepoint(lineWords[wordIdx]),
                                      lineWordStyles[wordIdx - 1], static_cast<int8_t>(spacing));
        } else if (noSpaceBeforeVec[logical]) {
          gaps += spacing;
        } else {
          gaps += spaceGap(renderer, fontId, lineWords[wordIdx - 1], lineWords[wordIdx], lineWordStyles[wordIdx - 1],
                           lineWordStyles[wordIdx]);
        }
      }
      const int spacedSpare =
          effectivePageWidth + hangLeft + hangRight - extraStartOffset - extraEndOffset - widthSum - gaps;
      if (spacedSpare < 0) continue;  // too wide with this much: try one pixel less
      // Even word gaps would still leave the line ragged: keep its letters as they are.
      if (computeJustifyExtra(spacedSpare, actualGapCount) > maxJustifyExtra) break;
      lineWidths = spacedWidths.data();
      lineSpacing = static_cast<int8_t>(spacing);
      lineWordWidthSum = widthSum;
      totalNaturalGaps = gaps;
      spareSpace = spacedSpare;
      break;
    }
  }

  int justifyExtra = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                         ? computeJustifyExtra(spareSpace, actualGapCount)
                         : 0;
  if (justifyExtra > maxJustifyExtra) justifyExtra = 0;
  // Typography Engine, space condensing: the line breaker let this line run over by no more than its gaps can give
  // up, so take the overrun back out of them. Every ordinary gap gives up the same number of pixels, but never more
  // than its own allowance (which stops at typoengine::gapFloor): condenseLevel is the smallest such number that
  // takes the whole overrun out.
  int condenseLevel = 0;
  if (condensePercent < 100 && effectiveAlignment == CssTextAlign::Justify && !isLastLine && spareSpace < 0 &&
      actualGapCount > 0) {
    const int deficit = -spareSpace;
    condenseLevel = 1;
    while (condenseLevel < maxGapShrink) {
      int taken = 0;
      for (const int allowance : gapAllowances) taken += std::min(condenseLevel, allowance);
      if (taken >= deficit) break;
      ++condenseLevel;
    }
    justifyExtra = 0;
  }
  // The extra (negative when condensing) one gap of natural width `gap` gets. Gaps that are not ordinary spaces
  // (a no-break space token, a locked gap) keep their width in a condensed line.
  const auto condensedExtra = [&](const int gap, const int fontSpace) {
    return -std::min(condenseLevel, typoengine::gapAllowance(gap, fontSpace, condensePercent));
  };

  // BiDi processing: reorder words with UAX#9 in full-line context.
  visualOrderScratch.clear();
  visualOrderScratch.reserve(lineWordCount);
  // Skip expensive visual-order resolution for pure LTR paragraphs that have no RTL words.
  const bool shouldResolveVisualOrder = blockStyle.isRtl || hasRtlWord;
  const bool willReorder =
      shouldResolveVisualOrder && BidiUtils::computeVisualWordOrder(lineWords, blockStyle.isRtl, visualOrderScratch);

  std::vector<int16_t> lineXPos;
  lineXPos.reserve(lineWordCount);

  if (willReorder) {
    reorderedWordsScratch.clear();
    reorderedStylesScratch.clear();
    reorderedWidthsScratch.clear();
    reorderedContinuesScratch.clear();
    reorderedNoSpaceBeforeScratch.clear();
    reorderedFocusBoundaryScratch.clear();
    reorderedWordsScratch.reserve(visualOrderScratch.size());
    reorderedStylesScratch.reserve(visualOrderScratch.size());
    reorderedWidthsScratch.reserve(visualOrderScratch.size());
    reorderedContinuesScratch.reserve(visualOrderScratch.size());
    reorderedNoSpaceBeforeScratch.reserve(visualOrderScratch.size());
    reorderedFocusBoundaryScratch.reserve(visualOrderScratch.size());

    for (size_t i = 0; i < visualOrderScratch.size(); ++i) {
      const uint16_t src = visualOrderScratch[i];
      reorderedWordsScratch.push_back(std::move(lineWords[src]));
      reorderedStylesScratch.push_back(lineWordStyles[src]);
      reorderedWidthsScratch.push_back(wordWidths[lastBreakAt + src]);
      reorderedFocusBoundaryScratch.push_back(wordFocusBoundary[lastBreakAt + src]);

      // Continuation means "no break/gap between two adjacent logical tokens".
      // After visual reordering (common in RTL), an adjacent logical pair can appear
      // as either (prev -> curr) or (curr -> prev) in visual order; preserve both.
      bool continues = false;
      if (i > 0) {
        const size_t prevSrc = visualOrderScratch[i - 1];
        const size_t currSrc = src;
        const bool forwardAdjacent = currSrc == prevSrc + 1;
        const bool reverseAdjacent = prevSrc == currSrc + 1;

        if (forwardAdjacent && continuesVec[lastBreakAt + currSrc]) {
          continues = true;
        } else if (reverseAdjacent && continuesVec[lastBreakAt + prevSrc]) {
          continues = true;
        }
      }
      reorderedContinuesScratch.push_back(continues);
      reorderedNoSpaceBeforeScratch.push_back(!continues && noSpaceBeforeVec[lastBreakAt + src]);
    }

    int reorderedWordWidthSum = 0;
    size_t reorderedGapCount = 0;
    int reorderedNaturalGaps = 0;
    for (size_t wordIdx = 0; wordIdx < reorderedWidthsScratch.size(); wordIdx++) {
      reorderedWordWidthSum += reorderedWidthsScratch[wordIdx];
      if (wordIdx > 0 && reorderedNoSpaceBeforeScratch[wordIdx]) {
        // Unicode break opportunity with no inserted Latin-style space. It is still
        // a stretchable gap for justified CJK/Korean text.
        reorderedGapCount++;
        reorderedNaturalGaps += blockStyle.characterSpacing;
      } else if (wordIdx > 0 && !reorderedContinuesScratch[wordIdx]) {
        reorderedGapCount++;
        reorderedNaturalGaps +=
            scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(reorderedWordsScratch[wordIdx - 1]),
                                                firstCodepoint(reorderedWordsScratch[wordIdx]),
                                                reorderedStylesScratch[wordIdx - 1]),
                       wordSpacingPercent);
      } else if (wordIdx > 0 && reorderedContinuesScratch[wordIdx]) {
        if (reorderedWordsScratch[wordIdx] == " " && wordIdx + 1 < reorderedWidthsScratch.size() &&
            reorderedContinuesScratch[wordIdx + 1]) {
          reorderedGapCount++;
        }
        reorderedNaturalGaps += renderer.getKerning(fontId, lastCodepoint(reorderedWordsScratch[wordIdx - 1]),
                                                    firstCodepoint(reorderedWordsScratch[wordIdx]),
                                                    reorderedStylesScratch[wordIdx - 1], blockStyle.characterSpacing);
      }
    }

    const int reorderedSpare =
        effectivePageWidth - extraStartOffset - extraEndOffset - reorderedWordWidthSum - reorderedNaturalGaps;
    int reorderedJustifyExtra = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                                    ? computeJustifyExtra(reorderedSpare, reorderedGapCount)
                                    : 0;
    if (reorderedJustifyExtra > maxJustifyExtra) reorderedJustifyExtra = 0;

    const int justifyContribution = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                                        ? reorderedJustifyExtra * static_cast<int>(reorderedGapCount)
                                        : 0;
    const int contentWidth = reorderedWordWidthSum + reorderedNaturalGaps + justifyContribution;

    int xpos = 0;
    if (blockStyle.isRtl) {
      if (effectiveAlignment == CssTextAlign::Right || effectiveAlignment == CssTextAlign::Justify) {
        xpos = effectivePageWidth - contentWidth;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - contentWidth) / 2;
      }
    } else {
      xpos = firstLineIndent;
      if (effectiveAlignment == CssTextAlign::Right) {
        xpos = effectivePageWidth - contentWidth;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - contentWidth) / 2;
      }
    }

    for (size_t wordIdx = 0; wordIdx < reorderedWidthsScratch.size(); wordIdx++) {
      lineXPos.push_back(static_cast<int16_t>(xpos));
      xpos += reorderedWidthsScratch[wordIdx];

      const bool nextIsContinuation =
          wordIdx + 1 < reorderedWidthsScratch.size() && reorderedContinuesScratch[wordIdx + 1];
      if (nextIsContinuation) {
        int advance = renderer.getKerning(fontId, lastCodepoint(reorderedWordsScratch[wordIdx]),
                                          firstCodepoint(reorderedWordsScratch[wordIdx + 1]),
                                          reorderedStylesScratch[wordIdx], blockStyle.characterSpacing);
        // wordIdx > 0 mirrors the gap accounting above (which skips index 0): a leading
        // no-break space must not receive justifyExtra, or the line over-stretches by one
        // gap and the last word is pushed past the right margin (issue #2185).
        if (wordIdx > 0 && reorderedWordsScratch[wordIdx] == " " && reorderedContinuesScratch[wordIdx] &&
            effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
          advance += reorderedJustifyExtra;
        }
        xpos += advance;
      } else if (wordIdx + 1 < reorderedWidthsScratch.size()) {
        const bool nextNoSpace = reorderedNoSpaceBeforeScratch[wordIdx + 1];
        int gap = nextNoSpace
                      ? blockStyle.characterSpacing
                      : scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(reorderedWordsScratch[wordIdx]),
                                                            firstCodepoint(reorderedWordsScratch[wordIdx + 1]),
                                                            reorderedStylesScratch[wordIdx]),
                                   wordSpacingPercent);
        if (effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
          gap += reorderedJustifyExtra;
        }
        xpos += gap;
      }
    }

    lineWords.swap(reorderedWordsScratch);
    lineWordStyles.swap(reorderedStylesScratch);
  } else {
    // Standard LTR/RTL positioning loop when no visual reordering is needed
    if (blockStyle.isRtl) {
      // RTL: position words from right to left
      int xpos = effectivePageWidth;
      if (effectiveAlignment == CssTextAlign::Left) {
        // Explicit left alignment in RTL context
        xpos = lineWordWidthSum + totalNaturalGaps;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth + lineWordWidthSum + totalNaturalGaps) / 2;
      }
      // For Right and Justify, start from right edge (xpos = effectivePageWidth)

      for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
        xpos -= wordWidths[lastBreakAt + wordIdx];
        lineXPos.push_back(static_cast<int16_t>(xpos));

        const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];
        if (nextIsContinuation) {
          // Cross-boundary kerning for continuation words
          int advance =
              renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx]), firstCodepoint(lineWords[wordIdx + 1]),
                                  lineWordStyles[wordIdx], blockStyle.characterSpacing);
          // wordIdx > 0: see the LTR branch — a leading no-break space is not a justifiable gap.
          if (wordIdx > 0 && lineWords[wordIdx] == " " && continuesVec[lastBreakAt + wordIdx] &&
              effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            advance += justifyExtra;
          }
          xpos -= advance;
        } else {
          int gap = 0;
          bool nextNoSpace = false;
          if (wordIdx + 1 < lineWordCount) {
            nextNoSpace = noSpaceBeforeVec[lastBreakAt + wordIdx + 1];
            gap = nextNoSpace ? blockStyle.characterSpacing
                              : scaleSpace(renderer.getSpaceAdvance(fontId, lastCodepoint(lineWords[wordIdx]),
                                                                    firstCodepoint(lineWords[wordIdx + 1]),
                                                                    lineWordStyles[wordIdx]),
                                           wordSpacingPercent);
          }
          if (wordIdx + 1 < lineWordCount && effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            gap += justifyExtra;
          }
          xpos -= gap;
        }
      }
    } else {
      // LTR: position words from left to right
      int xpos = firstLineIndent + extraStartOffset - hangLeft;
      if (effectiveAlignment == CssTextAlign::Right) {
        xpos = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - lineWordWidthSum - totalNaturalGaps) / 2;
      }

      for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
        lineXPos.push_back(static_cast<int16_t>(xpos));

        const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];
        if (nextIsContinuation) {
          int advance = lineWidths[wordIdx];
          advance += renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx]),
                                         firstCodepoint(lineWords[wordIdx + 1]), lineWordStyles[wordIdx], lineSpacing);
          // wordIdx > 0 mirrors the gap accounting above (which skips index 0): a leading
          // no-break space must not receive justifyExtra, or the line over-stretches by one
          // gap and the last word is pushed past the right margin (issue #2185).
          if (wordIdx > 0 && lineWords[wordIdx] == " " && continuesVec[lastBreakAt + wordIdx] &&
              effectiveAlignment == CssTextAlign::Justify && !isLastLine && !isLockedGap(wordIdx) &&
              condenseLevel == 0) {
            advance += justifyExtra;
          }
          xpos += advance;
        } else {
          int gap = 0;
          int fontSpace = 0;
          bool nextNoSpace = false;
          if (wordIdx + 1 < lineWordCount) {
            nextNoSpace = noSpaceBeforeVec[lastBreakAt + wordIdx + 1];
            gap = nextNoSpace ? lineSpacing
                              : spaceGap(renderer, fontId, lineWords[wordIdx], lineWords[wordIdx + 1],
                                         lineWordStyles[wordIdx], lineWordStyles[wordIdx + 1], &fontSpace);
          }
          if (wordIdx + 1 < lineWordCount && effectiveAlignment == CssTextAlign::Justify && !isLastLine &&
              !isLockedGap(wordIdx + 1)) {
            gap += condenseLevel > 0 ? (nextNoSpace ? 0 : condensedExtra(gap, fontSpace)) : justifyExtra;
          }
          xpos += lineWidths[wordIdx] + gap;
        }
      }
    }
  }

  const auto focusBoundaryAt = [&](const size_t idx) {
    return willReorder ? reorderedFocusBoundaryScratch[idx] : wordFocusBoundary[lastBreakAt + idx];
  };

  std::vector<TextBlock::LinkSpan> lineLinks;
  std::vector<uint8_t> lineLinkIdsSeen;
  for (size_t i = 0; i < lineWordCount; i++) {
    const uint8_t linkId = wordLinkIds[lastBreakAt + (willReorder ? visualOrderScratch[i] : i)];
    if (linkId == 0 || linkId > linkTargets.size()) continue;

    size_t spanIndex = 0;
    while (spanIndex < lineLinkIdsSeen.size() && lineLinkIdsSeen[spanIndex] != linkId) spanIndex++;
    int width = willReorder ? reorderedWidthsScratch[i] : lineWidths[i];
    const int right = lineXPos[i] + width;
    const int topLift =
        (lineWordStyles[i] & EpdFontFamily::SUP) != 0 ? renderer.getFontAscenderSize(fontId) * 2 / 5 : 0;

    if (spanIndex == lineLinkIdsSeen.size()) {
      lineLinks.emplace_back();
      auto& span = lineLinks.back();
      strncpy(span.href, linkTargets[linkId - 1].c_str(), sizeof(span.href) - 1);
      span.href[sizeof(span.href) - 1] = '\0';
      span.x = lineXPos[i];
      span.width = static_cast<int16_t>(width);
      span.topLift = static_cast<int16_t>(topLift);
      lineLinkIdsSeen.push_back(linkId);
    } else {
      auto& span = lineLinks[spanIndex];
      const int left = std::min<int>(span.x, lineXPos[i]);
      const int mergedRight = std::max<int>(span.x + span.width, right);
      span.x = static_cast<int16_t>(left);
      span.width = static_cast<int16_t>(mergedRight - left);
      span.topLift = std::max<int16_t>(span.topLift, static_cast<int16_t>(topLift));
    }
  }

  // Lines mixing sizes or using small caps carry per-word fonts; slots resolving to the block
  // font collapse to 0.
  const auto attachWordFonts = [&](TextBlock& block) {
    int32_t fonts[TextBlock::MAX_WORD_FONTS] = {};
    uint8_t slots[128];
    if (lineWordCount > sizeof(slots)) return true;  // pathological line: keep the block font
    bool mixed = false;
    for (size_t i = 0; i < lineWordCount; ++i) {
      const size_t logical = lastBreakAt + (willReorder ? visualOrderScratch[i] : i);
      const int wordFont = wordFontId(logical, fontId);
      uint8_t slot = 0;
      if (wordFont != fontId) {
        for (slot = 1; slot <= TextBlock::MAX_WORD_FONTS; ++slot) {
          if (fonts[slot - 1] == wordFont || fonts[slot - 1] == 0) break;
        }
        if (slot > TextBlock::MAX_WORD_FONTS) slot = 0;
        if (slot > 0) fonts[slot - 1] = wordFont;
      }
      slots[i] = static_cast<uint8_t>(slot | (wordSizeSlots[logical] & TextBlock::SLOT_FLAGS));
      mixed = mixed || slots[i] != 0;
    }
    return !mixed || block.setWordFonts(slots, fonts);
  };

  // Fast path: no word on this line carries focus emphasis, so pass empty boundary/suffixX
  // vectors. TextBlock pays zero per-word RAM cost for these annotations when they are empty.
  bool lineHasFocusSplit = false;
  for (size_t i = 0; i < lineWordCount; i++) {
    if (focusBoundaryAt(i) != 0) {
      lineHasFocusSplit = true;
      break;
    }
  }

  // Preserve source coordinates through visual reordering and discretionary hyphens.
  std::vector<TextBlock::SourceRange> sourceRanges;
  sourceRanges.reserve(lineWordCount);
  uint16_t paragraphStartWord = UINT16_MAX;
  for (size_t i = 0; i < lineWordCount; ++i) {
    const size_t logical = lastBreakAt + (willReorder ? visualOrderScratch[i] : i);
    if (!paragraphStartEmitted && logical == 0) paragraphStartWord = static_cast<uint16_t>(i);
    const uint32_t start = visibleOffsetAt(logical);
    const bool discretionaryHyphen = (wordStyles[logical] & TextBlock::DISCRETIONARY_HYPHEN_FLAG) != 0;
    uint32_t end = sourceOffsetAfter(start, countCodepoints(wordAt(logical)) - discretionaryHyphen);
    if (discretionaryHyphen) {
      lineWordStyles[i] = static_cast<EpdFontFamily::Style>(lineWordStyles[i] | TextBlock::DISCRETIONARY_HYPHEN_FLAG);
    }
    if (logical + 1 < words.size() && visibleOffsetAt(logical + 1) > start) {
      end = std::min(end, visibleOffsetAt(logical + 1));
    }
    sourceRanges.push_back({start, end});
  }

  // A letter-spaced line carries its own tracking, which TextBlock draws and stores with the line.
  BlockStyle lineStyle = blockStyle;
  lineStyle.characterSpacing = lineSpacing;

  if (!lineHasFocusSplit) {
    // TextBlock flattens the vectors into its arena; they stay owned here and die at return.
    auto block = makeUniqueNoThrow<TextBlock>(lineWords, lineXPos, lineWordStyles, std::vector<uint8_t>{},
                                              std::vector<uint16_t>{}, lineStyle, std::move(lineRubyTexts),
                                              std::move(lineLinks), sourceRanges, paragraphStartWord);
    if (!block || !block->valid() || !attachWordFonts(*block)) {
      LOG_ERR("PTX", "Dropping line: TextBlock or arena allocation failed");
      // Latch through the same flag as addWord() OOM: the caller releases the
      // consumed words right after this returns, so without it the section
      // would commit with this line silently missing.
      droppedWords = true;
      return;
    }
    processLine(std::move(block), lineVisibleOffset);
    return;
  }

  // Each word is one TextBlock entry carrying its own boundary; all that remains is the suffix x
  // offset the renderer needs to resume in regular weight, i.e. the bold prefix's advance.
  std::vector<uint8_t> outBoundaries;
  std::vector<uint16_t> outSuffixX;
  outBoundaries.reserve(lineWordCount);
  outSuffixX.reserve(lineWordCount);
  for (size_t i = 0; i < lineWordCount; i++) {
    const uint8_t boundary = focusBoundaryAt(i);
    outBoundaries.push_back(boundary);
    const int wordFont = wordFontId(lastBreakAt + (willReorder ? visualOrderScratch[i] : i), fontId);
    outSuffixX.push_back(boundary == 0 ? 0
                                       : measureFocusPrefixAdvance(
                                             renderer, wordFont, lineWords[i],
                                             getWordStyleAt(lastBreakAt + (willReorder ? visualOrderScratch[i] : i)),
                                             boundary, lineSpacing));
  }

  auto block =
      makeUniqueNoThrow<TextBlock>(lineWords, lineXPos, lineWordStyles, outBoundaries, outSuffixX, lineStyle,
                                   std::move(lineRubyTexts), std::move(lineLinks), sourceRanges, paragraphStartWord);
  if (!block || !block->valid() || !attachWordFonts(*block)) {
    LOG_ERR("PTX", "Dropping line: TextBlock or arena allocation failed");
    droppedWords = true;  // see the non-focus branch above
    return;
  }
  processLine(std::move(block), lineVisibleOffset);
}
