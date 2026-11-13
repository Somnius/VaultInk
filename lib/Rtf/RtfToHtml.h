#pragma once

// RTF -> XHTML for the reader's text-book path (like lib/Mobi). Platform-free, host-tested (test/markup_books).
//
// A subset: paragraphs (\par, \sect, \page, \row), line breaks, bold, italic, hidden text, headings from
// \outlinelevelN, centred and right-aligned paragraphs, \uN with the \ucN skip, \'hh through the code page of
// \ansicpgN or of the font (\fN with \fcharset161 Greek / 204 Cyrillic in the font table), the quote, dash and
// bullet words. Table cells become text with " | " between them. Pictures, objects, shapes, headers, footers,
// footnotes, the style sheet and every \* group are skipped.
//
// The file is read as a stream with fixed memory. Caps and what happens past them:
//  - MAX_DEPTH nested groups: a deeper group shares its parent's formatting;
//  - MAX_FONTS font table entries: a later font reads with the document's code page;
//  - MAX_WORD letters in a control word: the word is ignored;
//  - MAX_INFO_TEXT bytes of title or author: cut at a character.

#include <CodePage.h>
#include <XhtmlOut.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rtf {

inline constexpr const char* CACHE_VERSION_TAG = "<!-- RTF_CACHE_VERSION: 1 -->";
// readInfo() stops here: Word writes its style sheet and theme (tens of KB) before the title.
inline constexpr size_t INFO_SCAN_LIMIT = 256 * 1024;
inline constexpr size_t MAX_INFO_TEXT = 200;

enum class Status : uint8_t { Ok, IoError, Oom, NotRtf, WriteError, TooBig };
const char* statusText(Status status);

struct Info {
  std::string title;
  std::string author;
  std::string language;  // "el", "en", "ru"... from \deflangN; empty when not one of the few known here
};

using Read = int (*)(void* ctx, uint8_t* buf, size_t size);  // bytes read, 0 at the end, < 0 on an error

// True when the bytes start an RTF file ("{\rtf").
bool isRtf(std::string_view head);

class Converter {
 public:
  // `out` may be null: nothing is written and only the Info is collected.
  explicit Converter(xhtml::Out* out) : out_(out) {}

  // False once the sink refused a write.
  bool feed(const char* data, size_t size);
  // Closes the open paragraph. False once the sink refused a write.
  bool finish();
  // True once the {\info} group has been read and closed, or body text has started.
  bool infoDone() const { return infoDone_; }
  const Info& info() const { return info_; }

  static constexpr size_t MAX_DEPTH = 48;
  static constexpr size_t MAX_FONTS = 64;
  static constexpr size_t MAX_WORD = 32;

 private:
  enum class Dest : uint8_t { Body, Skip, FontTable, Info, Title, Author };
  enum class Mode : uint8_t { Text, Escape, Word, Param, HexHigh, HexLow, Bin };
  struct Group {
    Dest dest = Dest::Body;
    bool bold = false;
    bool italic = false;
    bool hidden = false;
    uint8_t unicodeSkip = 1;  // \ucN
    int16_t font = -1;        // -1: the default font
  };
  struct Font {
    int16_t number;
    codepage::Id page;
  };

  void byte(char c);
  void controlWord();
  void controlSymbol(char c);
  void openGroup();
  void closeGroup();
  void literal(uint8_t b);
  void hexByte(uint8_t b);
  void character(uint32_t cp);
  void unicode(int32_t value);
  void ensureParagraph();
  void closeParagraph();
  void closeInline();
  void lineBreak();
  void write(std::string_view s);
  codepage::Id page();
  void setFontPage(int font, codepage::Id page);
  bool wordIs(const char* name) const;

  xhtml::Out* out_;
  Info info_;
  bool infoDone_ = false;
  bool done_ = false;  // the outermost group closed: the rest of the file is ignored

  Mode mode_ = Mode::Text;
  char word_[MAX_WORD + 1] = {};
  size_t wordLen_ = 0;
  bool wordTooLong_ = false;
  bool hasParam_ = false;
  bool negative_ = false;
  int32_t param_ = 0;
  uint8_t hexHigh_ = 0;
  uint32_t binLeft_ = 0;
  bool star_ = false;  // \* seen: the group's destination is skipped

  Group cur_;
  Group stack_[MAX_DEPTH];
  size_t depth_ = 0;
  size_t overflow_ = 0;   // groups open past MAX_DEPTH
  uint8_t skipLeft_ = 0;  // fallback characters still to drop after a \uN
  uint32_t highSurrogate_ = 0;

  codepage::Id docPage_ = codepage::Id::Cp1252;
  int16_t defaultFont_ = -1;
  int16_t tableFont_ = -1;  // the font being defined in the font table
  Font fonts_[MAX_FONTS] = {};
  size_t fontCount_ = 0;

  bool paraOpen_ = false;
  bool boldOpen_ = false;
  bool italicOpen_ = false;
  bool cellPending_ = false;
  uint8_t align_ = 0;        // 0 none, 1 centre, 2 right
  int8_t outline_ = -1;      // \outlinelevelN of the paragraph, -1 none
  uint8_t paraHeading_ = 0;  // the hN the open paragraph was written as, 0 for <p>
};

// Title, author and language from the head of the file; reads at most INFO_SCAN_LIMIT bytes. NotRtf when the file
// does not start with "{\rtf".
Status readInfo(void* ctx, Read read, Info& info);
// The whole book as one XHTML page. `title` goes in <title>.
Status streamHtml(void* ctx, Read read, std::string_view title, xhtml::Emit emit, void* emitCtx);

}  // namespace rtf
