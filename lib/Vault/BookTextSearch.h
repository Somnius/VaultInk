#pragma once

#include <Print.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct XML_ParserStruct;

// Searches one chapter's XHTML for a word or short phrase without laying it out (kos25k #24).
// The chapter is streamed through expat: write() takes the bytes in any chunking (it is a Print, so
// Epub::readItemContentsToStream can feed it straight from the zip), finish() ends the document.
//
// Matching is the same as wordsearch::find / textfold: case and accents don't matter, a part of a
// word matches, a space in the query matches any run of white space (also across tags and
// paragraphs). Soft hyphens inside a word are ignored.
//
// Offsets are the reader's "visible text offset": one per UTF-8 code point of the text inside
// <body>, outside head/style/script/title/rp, counted exactly like ChapterHtmlSlimParser does
// (white space, soft hyphens and table text included; &nbsp; and the other named entities expanded).
// So a hit's offset goes straight into Section::getPageForVisibleTextOffset.
class ChapterTextSearcher final : public Print {
 public:
  struct Hit {
    uint32_t offset = 0;  // visible text offset of the first character of the match
    std::string snippet;  // a few words around it, as written in the book
  };

  // At most this many hits per kHitBucket visible code points (about a page): a page full of the word
  // does not flood the list.
  static constexpr size_t kHitsPerBucket = 3;
  static constexpr uint32_t kHitBucket = 2000;

  // `preparedQuery` comes from wordsearch::prepareQuery (folded, spaces collapsed, not empty).
  // `shouldAbort` is polled once per write(); when it answers true the stream is cut short.
  ChapterTextSearcher(std::string preparedQuery, size_t maxHits, std::function<bool()> shouldAbort = nullptr,
                      size_t hitsPerBucket = kHitsPerBucket);
  ~ChapterTextSearcher() override;
  ChapterTextSearcher(const ChapterTextSearcher&) = delete;
  ChapterTextSearcher& operator=(const ChapterTextSearcher&) = delete;

  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t size) override;

  // Ends the document; the snippets of the last hits are completed. False when the XML was broken
  // (the hits found before the error are kept).
  bool finish();

  const std::vector<Hit>& hits() const { return hits_; }
  bool ok() const { return ok_; }            // no parse error so far
  bool aborted() const { return aborted_; }  // shouldAbort() cut the stream
  bool full() const { return stopped_; }     // maxHits reached; the rest of the chapter was skipped
  // Visible code points seen so far (the chapter's total once finish() was called).
  uint32_t visibleTotal() const { return visibleOffset_; }

 private:
  struct Pending {
    uint32_t offset;
    size_t rawStart;  // absolute index of the match in the raw text
  };

  static void startElement(void* userData, const char* name, const char** atts);
  static void endElement(void* userData, const char* name);
  static void characterData(void* userData, const char* s, int len);
  static void defaultHandlerExpand(void* userData, const char* s, int len);

  bool done() const { return stopped_ && pending_.empty(); }
  void onStart(const char* name);
  void onEnd(const char* name);
  void onText(const char* s, int len);
  void addCodepoint(uint32_t cp);
  void breakWord();
  void appendRaw(uint32_t cp);
  void checkMatch();
  void completePending(const Pending& p);
  void completeReady(bool all);
  std::string makeSnippet(size_t rawStart) const;

  std::string query_;
  size_t maxHits_;
  size_t hitsPerBucket_;
  std::function<bool()> shouldAbort_;
  XML_ParserStruct* parser_ = nullptr;
  bool ok_ = true;
  bool aborted_ = false;
  bool stopped_ = false;
  bool finished_ = false;

  // Visible-offset state, the same three rules as ChapterHtmlSlimParser.
  bool insideBody_ = false;
  int nonVisibleDepth_ = 0;
  uint32_t visibleOffset_ = 0;

  // Matcher: the folded tail of the text, with where each byte came from.
  struct Mark {
    uint32_t offset;
    size_t raw;
  };
  std::string window_;
  std::vector<Mark> marks_;
  bool lastWasSpace_ = true;
  bool pendingSpace_ = false;

  // The raw (unfolded, white space collapsed) tail, for the snippets.
  std::string raw_;
  size_t rawBase_ = 0;  // absolute index of raw_[0]
  bool rawLastSpace_ = true;

  std::vector<Pending> pending_;
  std::vector<Hit> hits_;
  uint32_t bucket_ = UINT32_MAX;
  size_t bucketCount_ = 0;
};
