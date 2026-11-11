#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The pure parts of sending a book's highlights (clippings) to Readwise and to a Memos server:
// the request bodies, the batches, what an HTTP answer means, and the list of what was already
// sent. No network, card or platform code here; src/network/HighlightExportClient sends the
// bodies and src/activities/reader/SendHighlightsActivity drives the job.
//
// The API shapes below were written from memory of the public documentation, with no network
// access, and have not been tried against the real services. docs/exports.md lists every field
// that is unverified.
//
//  * Readwise: POST https://readwise.io/api/v2/highlights/ with `Authorization: Token <token>`,
//    body {"highlights":[{...}, ...]}, answer 200 (or 201).
//  * Memos: POST {server}/api/v1/memos with `Authorization: Bearer <token>`, body
//    {"content": "<markdown>", "visibility": "PRIVATE"}, answer 200.
//  * Joplin was cut: its API is the desktop app's local Web Clipper service, not a server.
namespace highlightexport {

enum class Destination : uint8_t { Readwise = 0, Memos = 1 };

inline constexpr char READWISE_URL[] = "https://readwise.io/api/v2/highlights/";
// No more than this many highlights in one Readwise request (our own limit, kept small for the heap).
inline constexpr size_t READWISE_MAX_BATCH = 20;
// A Readwise request body is closed once it has grown past this, so a batch of long highlights
// goes out as several requests. One highlight alone may be bigger (4 KiB of text, escaped).
inline constexpr size_t READWISE_SOFT_BODY_BYTES = 12 * 1024;
// Readwise's documented field limits, in characters (unverified, see the docs).
inline constexpr size_t READWISE_TEXT_MAX = 8191;
inline constexpr size_t READWISE_TITLE_MAX = 511;
inline constexpr size_t READWISE_AUTHOR_MAX = 1024;
inline constexpr size_t READWISE_NOTE_MAX = 8191;
inline constexpr size_t READWISE_URL_MAX = 4095;

// A clipping time before this (2020-01-01) was taken with no real date and is not sent.
inline constexpr uint32_t MIN_TRUSTED_EPOCH = 1577836800;

struct Book {
  std::string_view title;
  std::string_view author;
};

struct Highlight {
  std::string_view text;
  std::string_view chapter;  // Memos only: shown after the book
  std::string_view note;     // optional
  std::string_view url;      // optional: Readwise highlight_url
  uint32_t order = 0;        // 1-based place in reading order; 0 = leave the location out
  uint32_t timestamp = 0;    // epoch seconds from a trusted clock, else 0
};

// `text` as a JSON string with its quotes, appended to `out`. Control characters become escapes,
// bytes that are not UTF-8 become U+FFFD, everything else (Greek too) stays as UTF-8. No more than
// `maxChars` characters are taken.
void appendJsonString(std::string& out, std::string_view text, size_t maxChars = SIZE_MAX);

// "2026-11-11T09:30:00+00:00" (UTC).
std::string iso8601(uint32_t epochSeconds);

// One Readwise request body, built a highlight at a time.
class ReadwiseBatch {
 public:
  explicit ReadwiseBatch(Book book) : book_(book) {}

  // False when the batch is full (READWISE_MAX_BATCH highlights, or the body is past
  // READWISE_SOFT_BODY_BYTES): send body(), reset(), then add again. The first highlight of a
  // batch is always taken. A highlight with no text is skipped (true, count unchanged).
  bool add(const Highlight& highlight);
  size_t count() const { return count_; }
  bool empty() const { return count_ == 0; }
  // The complete JSON body for what was added.
  std::string body() const;
  void reset();

 private:
  Book book_;
  std::string items_;
  size_t count_ = 0;
};

// How many requests `highlights` take when each has at most READWISE_MAX_BATCH (ignoring the byte limit).
size_t readwiseBatchCount(size_t highlights);

// The Markdown of one memo: the quote as a block quote, then a line with the book, the author and
// the chapter, then the tag line "#highlights #vaultink".
std::string memosContent(Book book, const Highlight& highlight);
// The JSON body for that memo. Always private.
std::string memosBody(Book book, const Highlight& highlight);
// The address to post to for the server the user typed: "memos.example.com/" gives
// "https://memos.example.com/api/v1/memos". No scheme means https. Empty when there is no host.
std::string memosUrl(std::string_view server);

enum class Result : uint8_t {
  Ok,
  NotConnected,  // no answer: DNS, connect, the certificate check, a timeout
  BadToken,      // 401 or 403
  RateLimited,   // 429; retryAfterSeconds says how long, when the server said
  NotFound,      // 404: for Memos, the wrong address or a server without /api/v1/memos
  Rejected,      // another 4xx or a redirect: the server did not take the request
  ServerError,   // 5xx or anything else
};

struct Outcome {
  Result result = Result::NotConnected;
  uint32_t retryAfterSeconds = 0;  // only with RateLimited; 0 = not given
  bool ok() const { return result == Result::Ok; }
};

// What an HTTP status means. `status` <= 0 is a transport failure. `retryAfter` is the value of
// the Retry-After header (seconds; the date form is not read).
Outcome mapResponse(int status, std::string_view retryAfter);

// The mark for "this clipping went to this destination": a 64-bit FNV-1a hash of the clipping id
// and the destination. Never 0.
uint64_t sentKey(std::string_view clippingId, Destination destination);

// The marks for one book, kept in a small file. A file that does not check out reads as empty, so
// the worst case is sending a highlight twice.
class SentMarkers {
 public:
  // Oldest marks are dropped past this (two destinations for every clipping a book can have).
  static constexpr size_t MAX_ENTRIES = 2048;

  // False when the bytes are not a marker file (wrong magic, size or checksum); the set is then empty.
  bool parse(const uint8_t* data, size_t size);
  std::string serialize() const;

  bool contains(uint64_t key) const;
  // True when it was new.
  bool add(uint64_t key);
  size_t size() const { return keys_.size(); }
  void clear() { keys_.clear(); }

 private:
  std::vector<uint64_t> keys_;
};

// The name of a book's marker file: "<16 hex digits of the path's hash>.sent".
std::string sentFileName(std::string_view bookPath);

}  // namespace highlightexport
