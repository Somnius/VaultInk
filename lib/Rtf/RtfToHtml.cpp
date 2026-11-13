#include "RtfToHtml.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>

namespace rtf {
namespace {

bool isLetter(const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isDigit(const char c) { return c >= '0' && c <= '9'; }

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Groups whose text is not the book's: tables of the header, pictures, shapes, page furniture, notes, fields'
// instructions, and the {\info} entries other than the title and the author.
constexpr const char* SKIPPED[] = {
    "colortbl",
    "stylesheet",
    "pict",
    "object",
    "shp",
    "shpinst",
    "nonshppict",
    "header",
    "headerl",
    "headerr",
    "headerf",
    "footer",
    "footerl",
    "footerr",
    "footerf",
    "footnote",
    "annotation",
    "atnid",
    "atnauthor",
    "atndate",
    "themedata",
    "datastore",
    "latentstyles",
    "listtable",
    "rsidtbl",
    "generator",
    "xmlnstbl",
    "operator",
    "creatim",
    "revtim",
    "printim",
    "buptim",
    "subject",
    "keywords",
    "comment",
    "doccomm",
    "company",
    "manager",
    "category",
    "hlinkbase",
    "fldinst",
    "bkmkstart",
    "bkmkend",
    "template",
    "filetbl",
    "revtbl",
    "pgdsctbl",
    "mmathPr",
    "wgrffmtfilter",
    "falt",
    "panose",
    "listoverridetable",
    "colorschememapping",
    "passwordhash",
};

struct CharWord {
  const char* name;
  uint16_t codePoint;
};
constexpr CharWord CHAR_WORDS[] = {
    {"emdash", 0x2014},    {"endash", 0x2013},    {"lquote", 0x2018}, {"rquote", 0x2019},
    {"ldblquote", 0x201C}, {"rdblquote", 0x201D}, {"bullet", 0x2022}, {"emspace", ' '},
    {"enspace", ' '},      {"qmspace", ' '},      {"tab", ' '},
};

struct Language {
  uint16_t lcid;
  const char* code;
};
constexpr Language LANGUAGES[] = {
    {1032, "el"}, {1033, "en"}, {2057, "en"}, {1049, "ru"}, {1031, "de"}, {1036, "fr"}, {1040, "it"}, {3082, "es"},
};

// Fills `buf` (up to `cap`) from the reader; short only at the end of the file. < 0 on a read error.
int readFully(void* ctx, const Read read, uint8_t* buf, const size_t cap) {
  size_t got = 0;
  while (got < cap) {
    const int n = read(ctx, buf + got, cap - got);
    if (n < 0) return -1;
    if (n == 0) break;
    got += static_cast<size_t>(n);
  }
  return static_cast<int>(got);
}

constexpr size_t CHUNK = 1024;

}  // namespace

const char* statusText(const Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::IoError:
      return "read error";
    case Status::Oom:
      return "out of memory";
    case Status::NotRtf:
      return "not an RTF file";
    case Status::WriteError:
      return "write error";
    case Status::TooBig:
      return "too big";
  }
  return "?";
}

bool isRtf(std::string_view head) {
  if (head.size() >= 3 && head.substr(0, 3) == "\xEF\xBB\xBF") head.remove_prefix(3);
  while (!head.empty() && (head[0] == ' ' || head[0] == '\t' || head[0] == '\r' || head[0] == '\n')) {
    head.remove_prefix(1);
  }
  return head.size() >= 5 && head.substr(0, 5) == "{\\rtf";
}

bool Converter::wordIs(const char* name) const { return strcmp(word_, name) == 0; }

void Converter::write(const std::string_view s) {
  if (out_) out_->write(s);
}

codepage::Id Converter::page() {
  const int font = cur_.font >= 0 ? cur_.font : defaultFont_;
  for (size_t i = 0; i < fontCount_; i++) {
    if (fonts_[i].number == font) return fonts_[i].page;
  }
  return docPage_;
}

void Converter::setFontPage(const int font, const codepage::Id page) {
  if (font < 0 || font > INT16_MAX) return;
  for (size_t i = 0; i < fontCount_; i++) {
    if (fonts_[i].number == font) {
      fonts_[i].page = page;
      return;
    }
  }
  if (fontCount_ == MAX_FONTS) return;  // a font past the cap reads with the document's code page
  fonts_[fontCount_++] = Font{static_cast<int16_t>(font), page};
}

void Converter::closeInline() {
  if (italicOpen_) write("</i>");
  if (boldOpen_) write("</b>");
  italicOpen_ = false;
  boldOpen_ = false;
}

void Converter::ensureParagraph() {
  if (paraOpen_) return;
  paraHeading_ = outline_ < 0 ? 0 : outline_ < 3 ? static_cast<uint8_t>(outline_ + 1) : 4;
  if (paraHeading_ != 0) {
    const char tag[4] = {'<', 'h', static_cast<char>('0' + paraHeading_), '\0'};
    write(tag);
  } else {
    write("<p");
  }
  if (align_ == 1) write(" style=\"text-align:center\"");
  if (align_ == 2) write(" style=\"text-align:right\"");
  write(">");
  paraOpen_ = true;
}

void Converter::closeParagraph() {
  cellPending_ = false;
  if (!paraOpen_) return;  // an empty paragraph is not written
  closeInline();
  if (paraHeading_ != 0) {
    const char tag[7] = {'<', '/', 'h', static_cast<char>('0' + paraHeading_), '>', '\n', '\0'};
    write(tag);
  } else {
    write("</p>\n");
  }
  paraOpen_ = false;
}

void Converter::lineBreak() {
  if (paraOpen_ && !cur_.hidden) write("<br />");
}

void Converter::character(const uint32_t cp) {
  if (cp < 0x20 || !codepage::xmlChar(cp)) return;
  char utf8[4];
  const size_t n = codepage::encodeUtf8(cp, utf8);
  if (n == 0) return;
  switch (cur_.dest) {
    case Dest::Skip:
    case Dest::FontTable:
    case Dest::Info:
      return;
    case Dest::Title:
    case Dest::Author: {
      std::string& text = cur_.dest == Dest::Title ? info_.title : info_.author;
      if (text.size() + n <= MAX_INFO_TEXT) text.append(utf8, n);
      return;
    }
    case Dest::Body:
      break;
  }
  if (cur_.hidden) return;
  infoDone_ = true;  // the book's text has begun: the header is behind us
  if (!out_) return;
  ensureParagraph();
  if (cellPending_) {
    closeInline();
    write(" | ");
    cellPending_ = false;
  }
  if (boldOpen_ != cur_.bold || italicOpen_ != cur_.italic) {
    closeInline();
    if (cur_.bold) {
      write("<b>");
      boldOpen_ = true;
    }
    if (cur_.italic) {
      write("<i>");
      italicOpen_ = true;
    }
  }
  out_->escaped(std::string_view(utf8, n));
}

void Converter::unicode(const int32_t value) {
  uint32_t cp = value < 0 ? static_cast<uint32_t>(value + 65536) : static_cast<uint32_t>(value);
  skipLeft_ = cur_.unicodeSkip;
  if (cp >= 0xD800 && cp <= 0xDBFF) {
    highSurrogate_ = cp;  // the low half follows in the next \uN
    return;
  }
  if (cp >= 0xDC00 && cp <= 0xDFFF) {
    if (highSurrogate_ == 0) return;  // a low half on its own: dropped
    cp = 0x10000 + ((highSurrogate_ - 0xD800) << 10) + (cp - 0xDC00);
  }
  highSurrogate_ = 0;
  character(cp);
}

void Converter::hexByte(const uint8_t b) {
  if (skipLeft_ > 0) {
    skipLeft_--;
    return;
  }
  if (b < 0x80) {
    character(b == '\t' ? ' ' : b);
    return;
  }
  character(codepage::toUnicode(page(), b));  // 0 (undefined in the page) is dropped
}

void Converter::literal(const uint8_t b) { hexByte(b); }

void Converter::openGroup() {
  skipLeft_ = 0;
  star_ = false;
  if (depth_ < MAX_DEPTH) {
    stack_[depth_++] = cur_;
  } else {
    overflow_++;
  }
}

void Converter::closeGroup() {
  skipLeft_ = 0;
  star_ = false;
  if (overflow_ > 0) {
    overflow_--;
    return;
  }
  if (depth_ == 0) {
    done_ = true;
    return;
  }
  const Dest was = cur_.dest;
  cur_ = stack_[--depth_];
  if (was == Dest::Info && cur_.dest != Dest::Info) infoDone_ = true;
  if (depth_ == 0) done_ = true;
}

void Converter::controlSymbol(const char c) {
  switch (c) {
    case '\'':
      mode_ = Mode::HexHigh;
      return;
    case '*':
      star_ = true;
      return;
    case '\\':
    case '{':
    case '}':
      literal(static_cast<uint8_t>(c));
      return;
    case '~':
      if (skipLeft_ > 0) {
        skipLeft_--;
        return;
      }
      character(0xA0);
      return;
    case '_':
      literal('-');
      return;
    case '\r':
    case '\n':
      if (cur_.dest == Dest::Body) closeParagraph();  // a backslash before a line end is \par
      return;
    default:
      return;  // \- (optional hyphen), \| , \: and anything unknown
  }
}

void Converter::controlWord() {
  word_[wordLen_] = '\0';
  const int32_t n = negative_ ? -param_ : param_;
  if (!wordTooLong_ && wordIs("bin")) {
    // Raw bytes follow (inside a picture): they are not RTF and may hold braces.
    if (hasParam_ && n > 0) {
      binLeft_ = static_cast<uint32_t>(n);
      mode_ = Mode::Bin;
    }
    return;
  }
  if (star_) {  // {\*\word ...}: a destination a reader may ignore, and this one does
    star_ = false;
    cur_.dest = Dest::Skip;
    return;
  }
  if (wordTooLong_ || cur_.dest == Dest::Skip) return;
  if (wordIs("u")) {
    if (hasParam_) unicode(n);
    return;
  }
  skipLeft_ = 0;
  if (wordIs("uc")) {
    cur_.unicodeSkip = static_cast<uint8_t>(n < 0 ? 0 : n > 255 ? 255 : n);
    return;
  }
  if (wordIs("ansicpg")) {
    codepage::Id id;
    if (codepage::fromWindowsNumber(n, id)) docPage_ = id;
    return;
  }
  if (wordIs("deff")) {
    if (n >= 0 && n <= INT16_MAX) defaultFont_ = static_cast<int16_t>(n);
    return;
  }
  if (wordIs("deflang")) {
    for (const Language& language : LANGUAGES) {
      if (language.lcid == n) info_.language = language.code;
    }
    return;
  }
  if (wordIs("fonttbl")) {
    cur_.dest = Dest::FontTable;
    tableFont_ = -1;
    return;
  }
  if (wordIs("info")) {
    cur_.dest = Dest::Info;
    return;
  }
  if (cur_.dest == Dest::FontTable) {
    codepage::Id id;
    if (wordIs("f")) {
      tableFont_ = n >= 0 && n <= INT16_MAX ? static_cast<int16_t>(n) : -1;
    } else if (wordIs("fcharset")) {
      if (codepage::fromRtfCharset(n, id)) setFontPage(tableFont_, id);
    } else if (wordIs("cpg")) {
      if (codepage::fromWindowsNumber(n, id)) setFontPage(tableFont_, id);
    } else if (wordIs("falt") || wordIs("panose")) {
      cur_.dest = Dest::Skip;
    }
    return;
  }
  if (cur_.dest == Dest::Info) {
    if (wordIs("title")) {
      cur_.dest = Dest::Title;
    } else if (wordIs("author")) {
      cur_.dest = Dest::Author;
    }
    // every other entry stays Info, whose text is dropped
    return;
  }
  if (std::any_of(std::begin(SKIPPED), std::end(SKIPPED), [this](const char* skipped) { return wordIs(skipped); })) {
    cur_.dest = Dest::Skip;
    return;
  }
  const CharWord* const charWord = std::find_if(std::begin(CHAR_WORDS), std::end(CHAR_WORDS),
                                                [this](const CharWord& entry) { return wordIs(entry.name); });
  if (charWord != std::end(CHAR_WORDS)) {
    character(charWord->codePoint);
    return;
  }
  if (cur_.dest != Dest::Body) return;

  const bool on = !hasParam_ || n != 0;
  if (wordIs("par") || wordIs("sect") || wordIs("page") || wordIs("row")) {
    closeParagraph();
  } else if (wordIs("line")) {
    lineBreak();
  } else if (wordIs("cell") || wordIs("nestcell")) {
    cellPending_ = paraOpen_;
  } else if (wordIs("pard")) {
    align_ = 0;
    outline_ = -1;
  } else if (wordIs("plain")) {
    cur_.bold = false;
    cur_.italic = false;
    cur_.hidden = false;
    cur_.font = -1;
  } else if (wordIs("b")) {
    cur_.bold = on;
  } else if (wordIs("i")) {
    cur_.italic = on;
  } else if (wordIs("v")) {
    cur_.hidden = on;
  } else if (wordIs("f")) {
    cur_.font = n >= 0 && n <= INT16_MAX ? static_cast<int16_t>(n) : -1;
  } else if (wordIs("qc")) {
    align_ = 1;
  } else if (wordIs("qr")) {
    align_ = 2;
  } else if (wordIs("ql") || wordIs("qj")) {
    align_ = 0;
  } else if (wordIs("outlinelevel")) {
    outline_ = static_cast<int8_t>(n < 0 ? -1 : n > 8 ? 8 : n);
  }
}

void Converter::byte(const char c) {
  switch (mode_) {
    case Mode::Bin:
      if (--binLeft_ == 0) mode_ = Mode::Text;
      return;
    case Mode::HexHigh: {
      const int v = hexValue(c);
      if (v < 0) {  // not \'hh after all
        mode_ = Mode::Text;
        byte(c);
        return;
      }
      hexHigh_ = static_cast<uint8_t>(v);
      mode_ = Mode::HexLow;
      return;
    }
    case Mode::HexLow: {
      const int v = hexValue(c);
      mode_ = Mode::Text;
      if (v < 0) {
        byte(c);
        return;
      }
      hexByte(static_cast<uint8_t>((hexHigh_ << 4) | v));
      return;
    }
    case Mode::Escape:
      if (isLetter(c)) {
        mode_ = Mode::Word;
        word_[0] = c;
        wordLen_ = 1;
        wordTooLong_ = false;
        hasParam_ = false;
        negative_ = false;
        param_ = 0;
        return;
      }
      mode_ = Mode::Text;
      controlSymbol(c);
      return;
    case Mode::Word:
      if (isLetter(c)) {
        if (wordLen_ < MAX_WORD) {
          word_[wordLen_++] = c;
        } else {
          wordTooLong_ = true;
        }
        return;
      }
      if (c == '-') {
        negative_ = true;
        mode_ = Mode::Param;
        return;
      }
      [[fallthrough]];
    case Mode::Param:
      if (isDigit(c)) {
        mode_ = Mode::Param;
        hasParam_ = true;
        if (param_ < 100000000) param_ = param_ * 10 + (c - '0');  // saturates: no number in RTF is this long
        return;
      }
      mode_ = Mode::Text;
      controlWord();
      if (c != ' ') byte(c);  // one space ends the word and is not text; anything else is looked at afresh
      return;
    case Mode::Text:
      if (c == '\\') {
        mode_ = Mode::Escape;
      } else if (c == '{') {
        openGroup();
      } else if (c == '}') {
        closeGroup();
      } else if (c != '\r' && c != '\n' && c != '\0') {
        literal(static_cast<uint8_t>(c));
      }
      return;
  }
}

bool Converter::feed(const char* data, const size_t size) {
  for (size_t i = 0; i < size && !done_; i++) byte(data[i]);
  return !out_ || out_->ok();
}

bool Converter::finish() {
  closeParagraph();
  return !out_ || out_->ok();
}

Status readInfo(void* ctx, const Read read, Info& info) {
  std::unique_ptr<Converter> converter(new (std::nothrow) Converter(nullptr));
  std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[CHUNK]);  // not on the stack: the reader's is small
  if (!converter || !chunk) return Status::Oom;
  uint8_t* const buf = chunk.get();
  size_t total = 0;
  bool first = true;
  while (total < INFO_SCAN_LIMIT && !converter->infoDone()) {
    const int n = first ? readFully(ctx, read, buf, CHUNK) : read(ctx, buf, CHUNK);
    if (n < 0) return Status::IoError;
    if (first && !isRtf(std::string_view(reinterpret_cast<const char*>(buf), static_cast<size_t>(n)))) {
      return Status::NotRtf;
    }
    first = false;
    if (n == 0) break;
    converter->feed(reinterpret_cast<const char*>(buf), static_cast<size_t>(n));
    total += static_cast<size_t>(n);
  }
  info = converter->info();
  return Status::Ok;
}

Status streamHtml(void* ctx, const Read read, const std::string_view title, const xhtml::Emit emit, void* emitCtx) {
  std::unique_ptr<xhtml::Out> out(new (std::nothrow) xhtml::Out(emit, emitCtx));
  if (!out) return Status::Oom;
  std::unique_ptr<Converter> converter(new (std::nothrow) Converter(out.get()));
  std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[CHUNK]);  // not on the stack: the reader's is small
  if (!converter || !chunk) return Status::Oom;
  uint8_t* const buf = chunk.get();
  bool first = true;
  for (;;) {
    const int n = first ? readFully(ctx, read, buf, CHUNK) : read(ctx, buf, CHUNK);
    if (n < 0) return Status::IoError;
    if (first) {
      if (!isRtf(std::string_view(reinterpret_cast<const char*>(buf), static_cast<size_t>(n)))) return Status::NotRtf;
      out->head(CACHE_VERSION_TAG, title);
      first = false;
    }
    if (n == 0) break;
    if (!converter->feed(reinterpret_cast<const char*>(buf), static_cast<size_t>(n))) {
      return out->tooBig() ? Status::TooBig : Status::WriteError;
    }
  }
  converter->finish();
  if (out->tail()) return Status::Ok;
  return out->tooBig() ? Status::TooBig : Status::WriteError;
}

}  // namespace rtf
