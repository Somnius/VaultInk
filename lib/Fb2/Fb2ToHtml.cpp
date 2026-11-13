#include "Fb2ToHtml.h"

#include <CodePage.h>
#include <XmlParserUtils.h>
#include <expat.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace fb2 {
namespace {

constexpr size_t CHUNK = 1024;

// ---- expat's memory, counted: nothing in a file can make the parser take more than MAX_PARSER_MEMORY ----
// The suite has no context pointer, so the count is a global: the sum over every FB2 parser alive.
size_t parserMemory = 0;
constexpr size_t MEM_HEADER = alignof(std::max_align_t) > sizeof(size_t) ? alignof(std::max_align_t) : sizeof(size_t);

void* cappedMalloc(const size_t size) {
  if (size > MAX_PARSER_MEMORY || parserMemory > MAX_PARSER_MEMORY - size) return nullptr;
  auto* block = static_cast<uint8_t*>(malloc(size + MEM_HEADER));
  if (!block) return nullptr;
  memcpy(block, &size, sizeof(size));
  parserMemory += size;
  return block + MEM_HEADER;
}

void cappedFree(void* ptr) {
  if (!ptr) return;
  uint8_t* block = static_cast<uint8_t*>(ptr) - MEM_HEADER;
  size_t size;
  memcpy(&size, block, sizeof(size));
  parserMemory -= size;
  free(block);
}

void* cappedRealloc(void* ptr, const size_t size) {
  if (!ptr) return cappedMalloc(size);
  uint8_t* block = static_cast<uint8_t*>(ptr) - MEM_HEADER;
  size_t old;
  memcpy(&old, block, sizeof(old));
  if (size > MAX_PARSER_MEMORY || (size > old && parserMemory > MAX_PARSER_MEMORY - (size - old))) return nullptr;
  block = static_cast<uint8_t*>(realloc(block, size + MEM_HEADER));
  if (!block) return nullptr;
  memcpy(block, &size, sizeof(size));
  parserMemory = parserMemory - old + size;
  return block + MEM_HEADER;
}

// Windows-1251 / 1252 / 1253 and ISO-8859-7, which expat does not know itself.
int unknownEncoding(void*, const XML_Char* name, XML_Encoding* info) {
  codepage::Id id;
  if (!name || !codepage::fromName(name, id) || id == codepage::Id::Utf8) return XML_STATUS_ERROR;
  for (int i = 0; i < 256; i++) {
    const uint16_t cp = codepage::toUnicode(id, static_cast<uint8_t>(i));
    info->map[i] = i < 0x80 ? i : cp != 0 ? cp : -1;
  }
  info->data = nullptr;
  info->convert = nullptr;
  info->release = nullptr;
  return XML_STATUS_OK;
}

enum Kind : uint8_t {
  K_NONE,  // an element that writes nothing itself; what is inside it is still read
  K_ROOT,
  K_SKIP,
  K_BODY,
  K_SECTION,
  K_TITLE,
  K_TITLE_P,
  K_P,
  K_SUBTITLE,
  K_EMPHASIS,
  K_STRONG,
  K_STRIKE,
  K_SUB,
  K_SUP,
  K_CODE,
  K_QUOTE,
  K_STANZA,
  K_V,
  K_TEXT_AUTHOR,
  K_TABLE,
  K_TR,
  K_TD,
  // readInfo() only
  K_DESCRIPTION,
  K_TITLE_INFO,
  K_AUTHOR,
  K_CAPTURE,
};

class Parser {
 public:
  // `out` null: the info pass.
  explicit Parser(xhtml::Out* out) : out_(out) {}
  ~Parser() { destroyXmlParser(parser_); }
  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

  bool create() {
    static const XML_Memory_Handling_Suite suite = {cappedMalloc, cappedRealloc, cappedFree};
    parser_ = XML_ParserCreate_MM(nullptr, &suite, nullptr);
    if (!parser_) return false;
    XML_SetUserData(parser_, this);
    XML_SetElementHandler(parser_, onStart, onEnd);
    XML_SetCharacterDataHandler(parser_, onText);
    XML_SetUnknownEncodingHandler(parser_, unknownEncoding, nullptr);
    return true;
  }

  // Reads until the end, an error, or (the info pass) the end of <title-info> or `limit` bytes.
  Status run(void* ctx, const Read read, const size_t limit) {
    size_t total = 0;
    for (;;) {
      void* buf = XML_GetBuffer(parser_, static_cast<int>(CHUNK));
      if (!buf) return Status::Oom;
      const int n = read(ctx, static_cast<uint8_t*>(buf), CHUNK);
      if (n < 0) return Status::IoError;
      total += static_cast<size_t>(n);
      const XML_Status parsed = XML_ParseBuffer(parser_, n, n == 0);
      if (status_ != Status::Ok) return status_;  // a handler stopped the parser and said why
      if (finished_) return Status::Ok;
      if (parsed == XML_STATUS_ERROR) {
        if (XML_GetErrorCode(parser_) == XML_ERROR_NO_MEMORY) return Status::Oom;
        return wroteText_ ? Status::Truncated : Status::Malformed;
      }
      if (n == 0) return sawRoot_ ? Status::Ok : Status::NotFb2;
      if (limit != 0 && total >= limit) return Status::Ok;
    }
  }

  // After Truncated: closes what was left open, innermost first.
  void closeAll() {
    while (depth_ > 0) close(stack_[--depth_]);
  }

  const Info& info() const { return info_; }

 private:
  static void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
    static_cast<Parser*>(userData)->start(name, atts);
  }
  static void XMLCALL onEnd(void* userData, const XML_Char*) { static_cast<Parser*>(userData)->end(); }
  static void XMLCALL onText(void* userData, const XML_Char* text, int len) {
    static_cast<Parser*>(userData)->text(std::string_view(text, static_cast<size_t>(len)));
  }

  void fail(const Status status) {
    status_ = status;
    XML_StopParser(parser_, XML_FALSE);
  }

  void write(const std::string_view s) { out_->write(s); }

  static bool is(const char* name, const char* expected) { return strcmp(name, expected) == 0; }

  static const char* attribute(const XML_Char** atts, const char* wanted) {
    for (size_t i = 0; atts && atts[i]; i += 2) {
      if (xmlLocalNameEquals(atts[i], wanted)) return atts[i + 1];
    }
    return nullptr;
  }

  Kind parent() const { return depth_ > 0 ? static_cast<Kind>(stack_[depth_ - 1]) : K_NONE; }

  void capture(std::string* target) {
    target->clear();
    capture_ = target;
    captureFull_ = false;
  }

  Kind startInfo(const char* name) {
    switch (parent()) {
      case K_ROOT:
        if (is(name, "description")) return K_DESCRIPTION;
        if (is(name, "body")) {  // no title-info before the text: there is none
          finished_ = true;
          XML_StopParser(parser_, XML_FALSE);
        }
        return K_SKIP;
      case K_DESCRIPTION:
        return is(name, "title-info") ? K_TITLE_INFO : K_SKIP;
      case K_TITLE_INFO:
        if (is(name, "book-title")) {
          capture(&info_.title);
          return K_CAPTURE;
        }
        if (is(name, "lang")) {
          capture(&info_.language);
          return K_CAPTURE;
        }
        if (is(name, "author") && !haveAuthor_) {
          for (std::string& part : nameParts_) part.clear();
          return K_AUTHOR;
        }
        return K_SKIP;
      case K_AUTHOR: {
        static constexpr const char* PARTS[] = {"first-name", "middle-name", "last-name", "nickname"};
        for (size_t i = 0; i < 4; i++) {
          if (is(name, PARTS[i])) {
            capture(&nameParts_[i]);
            return K_CAPTURE;
          }
        }
        return K_SKIP;
      }
      default:
        return K_SKIP;
    }
  }

  // A verse paragraph left open by the lines before a title inside a stanza.
  void closeStanzaLines() {
    if (stanzaLines_ > 0) write("</p>\n");
    stanzaLines_ = 0;
  }

  Kind startBody(const char* name, const XML_Char** atts) {
    const Kind up = parent();
    if (up == K_ROOT) return is(name, "body") ? K_BODY : K_SKIP;  // description, binary, stylesheet
    if (is(name, "p")) {
      textDepth_++;
      if (up == K_TITLE) {
        if (titleParas_++ > 0) write("<br />");
        return K_TITLE_P;
      }
      write("<p>");
      return K_P;
    }
    if (is(name, "v")) {
      textDepth_++;
      if (up != K_STANZA) {
        write("<p>");
        return K_P;
      }
      write(stanzaLines_++ > 0 ? "<br />\n" : "<p>");
      return K_V;
    }
    if (is(name, "empty-line")) {
      if (up == K_TITLE || textDepth_ == 0) write("<br />\n");
      return K_NONE;
    }
    if (textDepth_ > 0) {  // inside a paragraph: the inline elements
      if (is(name, "emphasis")) {
        write("<i>");
        return K_EMPHASIS;
      }
      if (is(name, "strong")) {
        write("<b>");
        return K_STRONG;
      }
      if (is(name, "strikethrough")) {
        write("<s>");
        return K_STRIKE;
      }
      if (is(name, "sub")) {
        write("<sub>");
        return K_SUB;
      }
      if (is(name, "sup")) {
        write("<sup>");
        return K_SUP;
      }
      if (is(name, "code")) {
        write("<code>");
        return K_CODE;
      }
      if (is(name, "a")) {
        const char* type = attribute(atts, "type");
        if (type && is(type, "note")) {
          write("<sup>");
          return K_SUP;
        }
      }
      return K_NONE;  // <a>, <style>, <image>: the text inside is kept
    }
    if (is(name, "section")) {
      sectionDepth_++;
      return K_SECTION;
    }
    if (is(name, "title")) {
      if (up == K_STANZA) closeStanzaLines();
      const int level = up == K_BODY ? 1 : up == K_SECTION ? (sectionDepth_ < 3 ? sectionDepth_ + 1 : 4) : 4;
      titleLevel_ = static_cast<char>('0' + level);
      titleParas_ = 0;
      const char tag[5] = {'<', 'h', titleLevel_, '>', '\0'};
      write(tag);
      return K_TITLE;
    }
    if (is(name, "subtitle")) {
      if (up == K_STANZA) closeStanzaLines();
      textDepth_++;
      write("<h4>");
      return K_SUBTITLE;
    }
    if (is(name, "epigraph") || is(name, "cite") || is(name, "annotation") || is(name, "poem")) {
      write("<blockquote>\n");
      return K_QUOTE;
    }
    if (is(name, "stanza")) {
      stanzaLines_ = 0;
      return K_STANZA;
    }
    if (is(name, "text-author") || is(name, "date")) {
      if (up == K_STANZA) closeStanzaLines();
      textDepth_++;
      write("<p style=\"text-align:right\"><i>");
      return K_TEXT_AUTHOR;
    }
    if (is(name, "table")) {
      write("<table>\n");
      return K_TABLE;
    }
    if (is(name, "tr")) {
      write("<tr>");
      return K_TR;
    }
    if (is(name, "td") || is(name, "th")) {
      textDepth_++;
      write("<td>");
      return K_TD;
    }
    return K_NONE;  // <image> (pictures are not shown) and anything unknown
  }

  void start(const XML_Char* qName, const XML_Char** atts) {
    if (depth_ == MAX_DEPTH) {
      fail(Status::TooDeep);
      return;
    }
    const char* name = xmlLocalName(qName);
    Kind kind;
    if (skip_ > 0) {
      kind = K_SKIP;
    } else if (depth_ == 0) {
      if (!is(name, "FictionBook")) {
        fail(Status::NotFb2);
        return;
      }
      sawRoot_ = true;
      kind = K_ROOT;
    } else {
      kind = out_ ? startBody(name, atts) : startInfo(name);
    }
    if (kind == K_SKIP) skip_++;
    stack_[depth_++] = kind;
    if (out_ && !out_->ok()) fail(out_->tooBig() ? Status::TooBig : Status::WriteError);
  }

  void close(const uint8_t kind) {
    switch (kind) {
      case K_SKIP:
        skip_--;
        break;
      case K_SECTION:
        sectionDepth_--;
        break;
      case K_TITLE: {
        const char tag[7] = {'<', '/', 'h', titleLevel_, '>', '\n', '\0'};
        write(tag);
        break;
      }
      case K_TITLE_P:
      case K_V:
        textDepth_--;
        break;
      case K_P:
        textDepth_--;
        write("</p>\n");
        break;
      case K_SUBTITLE:
        textDepth_--;
        write("</h4>\n");
        break;
      case K_EMPHASIS:
        write("</i>");
        break;
      case K_STRONG:
        write("</b>");
        break;
      case K_STRIKE:
        write("</s>");
        break;
      case K_SUB:
        write("</sub>");
        break;
      case K_SUP:
        write("</sup>");
        break;
      case K_CODE:
        write("</code>");
        break;
      case K_QUOTE:
        write("</blockquote>\n");
        break;
      case K_STANZA:
        closeStanzaLines();
        break;
      case K_TEXT_AUTHOR:
        textDepth_--;
        write("</i></p>\n");
        break;
      case K_TABLE:
        write("</table>\n");
        break;
      case K_TR:
        write("</tr>\n");
        break;
      case K_TD:
        textDepth_--;
        write("</td>");
        break;
      case K_CAPTURE:
        capture_ = nullptr;
        break;
      case K_AUTHOR:
        for (size_t i = 0; i < 3; i++) {
          if (nameParts_[i].empty()) continue;
          if (!info_.author.empty()) info_.author += ' ';
          info_.author += nameParts_[i];
        }
        if (info_.author.empty()) info_.author = nameParts_[3];
        if (info_.author.size() > MAX_INFO_TEXT) cutAtCharacter(info_.author, MAX_INFO_TEXT);
        haveAuthor_ = !info_.author.empty();
        break;
      case K_TITLE_INFO:
        finished_ = true;  // all the info pass wanted
        XML_StopParser(parser_, XML_FALSE);
        break;
      default:
        break;
    }
  }

  void end() {
    if (depth_ == 0) return;
    close(stack_[--depth_]);
    if (out_ && !out_->ok()) fail(out_->tooBig() ? Status::TooBig : Status::WriteError);
  }

  static void cutAtCharacter(std::string& text, size_t size) {
    while (size > 0 && (static_cast<unsigned char>(text[size]) & 0xC0) == 0x80) size--;
    text.resize(size);
  }

  void text(const std::string_view chunk) {
    if (skip_ > 0) return;
    if (!out_) {
      if (!capture_ || captureFull_) return;
      capture_->append(chunk.substr(0, MAX_INFO_TEXT + 1 - capture_->size()));
      if (capture_->size() > MAX_INFO_TEXT) {
        cutAtCharacter(*capture_, MAX_INFO_TEXT);
        captureFull_ = true;  // the rest of the element is dropped
      }
      return;
    }
    if (textDepth_ == 0) return;  // white space between blocks
    wroteText_ = true;
    out_->escaped(chunk);
    if (!out_->ok()) fail(out_->tooBig() ? Status::TooBig : Status::WriteError);
  }

  xhtml::Out* out_;
  XML_Parser parser_ = nullptr;
  Status status_ = Status::Ok;
  bool finished_ = false;
  bool sawRoot_ = false;
  bool wroteText_ = false;
  bool haveAuthor_ = false;
  uint8_t stack_[MAX_DEPTH] = {};
  size_t depth_ = 0;
  int skip_ = 0;
  int textDepth_ = 0;
  int sectionDepth_ = 0;
  int titleParas_ = 0;
  int stanzaLines_ = 0;
  char titleLevel_ = '1';
  std::string* capture_ = nullptr;
  bool captureFull_ = false;
  std::string nameParts_[4];
  Info info_;
};

}  // namespace

const char* statusText(const Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::Truncated:
      return "cut short";
    case Status::IoError:
      return "read error";
    case Status::Oom:
      return "out of memory";
    case Status::NotFb2:
      return "not an FB2 file";
    case Status::Malformed:
      return "not well-formed XML, or an encoding that is not read";
    case Status::TooDeep:
      return "nested too deep";
    case Status::WriteError:
      return "write error";
    case Status::TooBig:
      return "too big";
  }
  return "?";
}

Status readInfo(void* ctx, const Read read, Info& info) {
  std::unique_ptr<Parser> parser(new (std::nothrow) Parser(nullptr));
  if (!parser || !parser->create()) return Status::Oom;
  const Status status = parser->run(ctx, read, INFO_SCAN_LIMIT);
  if (status == Status::Ok) info = parser->info();
  return status;
}

Status streamHtml(void* ctx, const Read read, const std::string_view title, const xhtml::Emit emit, void* emitCtx) {
  std::unique_ptr<xhtml::Out> out(new (std::nothrow) xhtml::Out(emit, emitCtx));
  if (!out) return Status::Oom;
  std::unique_ptr<Parser> parser(new (std::nothrow) Parser(out.get()));
  if (!parser || !parser->create()) return Status::Oom;
  out->head(CACHE_VERSION_TAG, title);
  const Status status = parser->run(ctx, read, 0);
  if (status != Status::Ok && status != Status::Truncated) return status;
  if (status == Status::Truncated) parser->closeAll();
  if (out->tail()) return status;
  return out->tooBig() ? Status::TooBig : Status::WriteError;
}

}  // namespace fb2
