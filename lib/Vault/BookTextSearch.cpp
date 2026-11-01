#include "BookTextSearch.h"

#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>
#include <Utf8.h>
#include <expat.h>
#include <strings.h>

#include <algorithm>
#include <cstring>

#include "TextFold.h"
#include "WordSearch.h"

namespace {
constexpr size_t kRawKeep = 240;       // bytes of raw text kept behind the reader head
constexpr size_t kSnippetAfter = 110;  // bytes of raw text kept after the start of a match
constexpr size_t kSnippetBefore = 22;  // and before it

bool isBlockTag(const char* name) {
  static const char* const kBlocks[] = {"p",       "div",     "br",    "li",     "tr",         "td",     "th",
                                        "h1",      "h2",      "h3",    "h4",     "h5",         "h6",     "blockquote",
                                        "ul",      "ol",      "table", "hr",     "dt",         "dd",     "pre",
                                        "section", "article", "aside", "figure", "figcaption", "caption"};
  return std::any_of(std::begin(kBlocks), std::end(kBlocks),
                     [name](const char* b) { return VisibleTextUtils::equalsTag(name, b); });
}

bool isSpace(const uint32_t cp) { return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0; }
bool isIgnorable(const uint32_t cp) { return cp == 0xAD || cp == 0x200B || cp == 0x200C || cp == 0x200D; }
}  // namespace

ChapterTextSearcher::ChapterTextSearcher(std::string preparedQuery, const size_t maxHits,
                                         std::function<bool()> shouldAbort, const size_t hitsPerBucket,
                                         const bool exactWords)
    : query_(std::move(preparedQuery)),
      exact_(exactWords),
      maxHits_(maxHits),
      hitsPerBucket_(hitsPerBucket),
      shouldAbort_(std::move(shouldAbort)) {
  parser_ = XML_ParserCreate(nullptr);
  if (!parser_ || query_.empty()) {
    ok_ = false;
    return;
  }
  XML_SetDefaultHandlerExpand(parser_, defaultHandlerExpand);
  XML_SetUserData(parser_, this);
  XML_SetElementHandler(parser_, startElement, endElement);
  XML_SetCharacterDataHandler(parser_, characterData);
}

ChapterTextSearcher::~ChapterTextSearcher() {
  if (parser_) XML_ParserFree(parser_);
}

size_t ChapterTextSearcher::write(const uint8_t* data, const size_t size) {
  if (!ok_ || done() || aborted_ || finished_) return 0;
  if (shouldAbort_ && shouldAbort_()) {
    aborted_ = true;
    return 0;
  }
  if (size == 0) return 0;
  if (XML_Parse(parser_, reinterpret_cast<const char*>(data), static_cast<int>(size), 0) == XML_STATUS_ERROR) {
    ok_ = false;
    return 0;
  }
  return done() ? 0 : size;
}

bool ChapterTextSearcher::finish() {
  if (finished_) return ok_;
  finished_ = true;
  if (parser_ && ok_ && !done() && !aborted_) {
    if (XML_Parse(parser_, "", 0, 1) == XML_STATUS_ERROR) ok_ = false;
  }
  resolveAwaiting(true);
  completeReady(true);
  return ok_;
}

void XMLCALL ChapterTextSearcher::startElement(void* userData, const char* name, const char**) {
  static_cast<ChapterTextSearcher*>(userData)->onStart(name);
}

void XMLCALL ChapterTextSearcher::endElement(void* userData, const char* name) {
  static_cast<ChapterTextSearcher*>(userData)->onEnd(name);
}

void XMLCALL ChapterTextSearcher::characterData(void* userData, const char* s, const int len) {
  static_cast<ChapterTextSearcher*>(userData)->onText(s, len);
}

// Same as ChapterHtmlSlimParser::defaultHandlerExpand: known entities are expanded, unknown ones are
// kept as the raw "&name;" text.
void XMLCALL ChapterTextSearcher::defaultHandlerExpand(void* userData, const char* s, const int len) {
  auto* self = static_cast<ChapterTextSearcher*>(userData);
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (value) {
      self->onText(value, static_cast<int>(strlen(value)));
    } else {
      self->onText(s, len);
    }
  }
}

void ChapterTextSearcher::onStart(const char* name) {
  if (strcasecmp(name, "body") == 0) insideBody_ = true;
  if (insideBody_ && (nonVisibleDepth_ > 0 || VisibleTextUtils::isNonVisibleElement(name))) nonVisibleDepth_++;
  if (insideBody_ && isBlockTag(name)) breakWord();
}

void ChapterTextSearcher::onEnd(const char* name) {
  if (nonVisibleDepth_ > 0) nonVisibleDepth_--;
  if (insideBody_ && isBlockTag(name)) breakWord();
  if (VisibleTextUtils::equalsTag(name, "body")) insideBody_ = false;
}

void ChapterTextSearcher::onText(const char* s, const int len) {
  if (done()) return;
  const bool visible = insideBody_ && nonVisibleDepth_ == 0;
  if (!visible) return;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(s);
  const unsigned char* end = ptr + len;
  while (ptr < end) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    addCodepoint(cp);
    visibleOffset_++;
  }
}

void ChapterTextSearcher::breakWord() {
  resolveAwaiting(true);
  pendingSpace_ = true;
}

void ChapterTextSearcher::appendRaw(const uint32_t cp) {
  if (isIgnorable(cp)) return;
  if (isSpace(cp)) {
    if (rawLastSpace_) return;
    raw_ += ' ';
    rawLastSpace_ = true;
  } else {
    utf8AppendCodepoint(cp, raw_);
    rawLastSpace_ = false;
  }
  if (raw_.size() > kRawKeep + kSnippetAfter) {
    size_t cut = raw_.size() - kRawKeep;
    while (cut < raw_.size() && (static_cast<unsigned char>(raw_[cut]) & 0xC0) == 0x80) cut++;
    raw_.erase(0, cut);
    rawBase_ += cut;
  }
}

void ChapterTextSearcher::addCodepoint(const uint32_t cp) {
  if (isIgnorable(cp)) return;
  if (cp >= 0x0300 && cp <= 0x036F) return;  // separate accents fold away (textfold::fold)
  if (awaiting_) resolveAwaiting(!wordsearch::isWordChar(cp));
  if (isSpace(cp)) {
    pendingSpace_ = true;
    appendRaw(cp);
    completeReady(false);
    return;
  }
  if (pendingSpace_) {
    pendingSpace_ = false;
    if (!lastWasSpace_) {
      window_ += ' ';
      marks_.push_back({visibleOffset_, rawBase_ + raw_.size()});
      lastWasSpace_ = true;
      appendRaw(' ');
    }
  }
  const size_t rawAt = rawBase_ + raw_.size();
  std::string folded;
  utf8AppendCodepoint(textfold::foldCodepoint(cp), folded);
  window_ += folded;
  for (size_t i = 0; i < folded.size(); i++) marks_.push_back({visibleOffset_, rawAt});
  lastWasSpace_ = false;
  appendRaw(cp);
  if (!stopped_) checkMatch();
  completeReady(false);
}

void ChapterTextSearcher::checkMatch() {
  const size_t q = query_.size();
  if (window_.size() >= q && window_.compare(window_.size() - q, q, query_) == 0) {
    const Mark start = marks_[window_.size() - q];
    if (exact_) {
      // The character before the match (kept behind the window's tail) must not be part of a word.
      if (startsAtWordEdge(window_.size() - q)) {
        awaiting_ = true;  // its end is judged by the next character
        awaitingMatch_ = {start.offset, start.raw};
        return;
      }
    } else {
      acceptMatch({start.offset, start.raw});
      return;
    }
  }
  const size_t keep = q + (exact_ ? kLookBehind : 0);
  if (window_.size() > keep) {
    window_.erase(0, window_.size() - keep);
    marks_.erase(marks_.begin(), marks_.end() - static_cast<std::ptrdiff_t>(keep));
  }
}

// True when the folded window has no word character right before byte `pos` (the start of a match).
bool ChapterTextSearcher::startsAtWordEdge(const size_t pos) const {
  if (pos == 0) return true;
  size_t i = pos - 1;
  while (i > 0 && (static_cast<unsigned char>(window_[i]) & 0xC0) == 0x80) i--;
  if ((static_cast<unsigned char>(window_[i]) & 0xC0) == 0x80) return false;  // cut mid-character: a letter
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(window_.data()) + i;
  return !wordsearch::isWordChar(utf8NextCodepoint(&ptr));
}

// Exact mode: the character after a match arrived (or the text ended). A letter or digit means the match
// was only part of a longer word.
void ChapterTextSearcher::resolveAwaiting(const bool accept) {
  if (!awaiting_) return;
  awaiting_ = false;
  if (accept) {
    acceptMatch(awaitingMatch_);
  } else {
    const size_t keep = query_.size() + kLookBehind;
    if (window_.size() > keep) {
      window_.erase(0, window_.size() - keep);
      marks_.erase(marks_.begin(), marks_.end() - static_cast<std::ptrdiff_t>(keep));
    }
  }
}

void ChapterTextSearcher::acceptMatch(const Pending& p) {
  const uint32_t bucket = p.offset / kHitBucket;
  if (bucket != bucket_) {
    bucket_ = bucket;
    bucketCount_ = 0;
  }
  if (bucketCount_ < hitsPerBucket_) {
    bucketCount_++;
    pending_.push_back(p);
  }
  // No overlaps: the next match starts after this one.
  window_.clear();
  marks_.clear();
  if (hits_.size() + pending_.size() >= maxHits_) stopped_ = true;
}

// A hit is complete once enough text followed it.
void ChapterTextSearcher::completeReady(const bool all) {
  const size_t head = rawBase_ + raw_.size();
  size_t ready = 0;
  while (ready < pending_.size() && (all || head >= pending_[ready].rawStart + kSnippetAfter)) ready++;
  for (size_t i = 0; i < ready; i++) completePending(pending_[i]);
  pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(ready));
}

void ChapterTextSearcher::completePending(const Pending& p) {
  Hit hit;
  hit.offset = p.offset;
  hit.snippet = makeSnippet(p.rawStart);
  hits_.push_back(std::move(hit));
}

std::string ChapterTextSearcher::makeSnippet(const size_t rawStart) const {
  const size_t headEnd = rawBase_ + raw_.size();
  size_t from = rawStart > rawBase_ + kSnippetBefore ? rawStart - kSnippetBefore : rawBase_;
  const size_t wantTo = rawStart + kSnippetAfter + query_.size();
  size_t to = wantTo < headEnd ? wantTo : headEnd;
  // Whole words: move to a word start / end unless the edge is the start or end of the text.
  const bool leading = from > 0;
  const bool trailing = to < headEnd;
  auto at = [&](size_t abs) { return raw_[abs - rawBase_]; };
  if (leading && from > rawBase_) {
    while (from < rawStart && at(from - 1) != ' ') from++;
  }
  if (trailing) {
    const size_t cut = to;
    while (to > rawStart && at(to) != ' ') to--;
    if (to == rawStart) to = cut;  // one long word (a URL): keep the match and its tail, cut where it falls
  }
  // Keep UTF-8 whole at the edges.
  while (from < to && (static_cast<unsigned char>(at(from)) & 0xC0) == 0x80) from++;
  while (to > from && to < headEnd && (static_cast<unsigned char>(at(to)) & 0xC0) == 0x80) to--;
  while (from < to && at(from) == ' ') from++;
  std::string out = raw_.substr(from - rawBase_, to - from);
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (leading) out.insert(0, "... ");
  if (trailing) out += " ...";
  return out;
}
