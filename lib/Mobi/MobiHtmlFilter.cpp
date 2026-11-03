#include "MobiHtmlFilter.h"

#include <Epub/htmlEntities.h>

#include <cstring>

#include "MobiFormat.h"

namespace mobi {
namespace {

enum Kind : uint8_t { DROP = 0, BLOCK = 1, INLINE = 2, VOID = 4, SKIP = 8, PAGEBREAK = 16 };

struct TagDef {
  const char* in;   // Mobipocket name (lowercase)
  const char* out;  // emitted name
  uint8_t kind;
};

// Index = id (0 is "unknown"). Only the output name matters for pairing: <center> and <div> share a
// close.
constexpr TagDef TAGS[] = {
    {"", "", DROP},
    {"p", "p", BLOCK},
    {"div", "div", BLOCK},
    {"h1", "h1", BLOCK},
    {"h2", "h2", BLOCK},
    {"h3", "h3", BLOCK},
    {"h4", "h4", BLOCK},
    {"h5", "h5", BLOCK},
    {"h6", "h6", BLOCK},
    {"blockquote", "blockquote", BLOCK},
    {"ul", "ul", BLOCK},
    {"ol", "ol", BLOCK},
    {"li", "li", BLOCK},
    {"pre", "pre", BLOCK},
    {"center", "div", BLOCK},
    {"table", "table", BLOCK},
    {"tr", "tr", BLOCK},
    {"td", "td", BLOCK},
    {"th", "th", BLOCK},
    {"b", "b", INLINE},
    {"i", "i", INLINE},
    {"em", "em", INLINE},
    {"strong", "strong", INLINE},
    {"u", "u", INLINE},
    {"s", "s", INLINE},
    {"strike", "s", INLINE},
    {"sup", "sup", INLINE},
    {"sub", "sub", INLINE},
    {"span", "span", INLINE},
    {"small", "small", INLINE},
    {"big", "big", INLINE},
    {"font", "span", INLINE},
    {"tt", "tt", INLINE},
    {"code", "code", INLINE},
    {"a", "span", INLINE},
    {"br", "br", VOID},
    {"hr", "hr", VOID},
    {"img", "img", VOID},
    {"mbp:pagebreak", "", PAGEBREAK},
    {"head", "", SKIP},
    {"style", "", SKIP},
    {"script", "", SKIP},
    {"title", "", SKIP},
};
constexpr uint8_t TAG_P = 1;
constexpr uint8_t TAG_UL = 10;
constexpr uint8_t TAG_OL = 11;
constexpr uint8_t TAG_LI = 12;
constexpr uint8_t TAG_IMG = 37;

uint8_t lookup(std::string_view name) {
  for (size_t i = 1; i < sizeof(TAGS) / sizeof(TAGS[0]); i++) {
    if (name == TAGS[i].in) return static_cast<uint8_t>(i);
  }
  return 0;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

}  // namespace

void HtmlFilter::out(std::string_view s) {
  if (ok_ && !s.empty() && !emit_(emitCtx_, s.data(), s.size())) ok_ = false;
}

int HtmlFilter::find(uint8_t id) const {
  for (size_t i = depth_; i-- > 0;) {
    if (stack_[i] == id) return static_cast<int>(i);
  }
  return -1;
}

void HtmlFilter::closeTo(size_t index) {
  while (depth_ > index) {
    depth_--;
    out("</");
    out(TAGS[stack_[depth_]].out);
    out(">");
  }
}

std::string_view HtmlFilter::attrValue(std::string_view attrs, std::string_view name) {
  size_t pos = 0;
  while (pos < attrs.size()) {
    while (pos < attrs.size() && isSpace(attrs[pos])) pos++;
    const size_t nameStart = pos;
    while (pos < attrs.size() && !isSpace(attrs[pos]) && attrs[pos] != '=' && attrs[pos] != '/') pos++;
    std::string_view key = attrs.substr(nameStart, pos - nameStart);
    while (pos < attrs.size() && isSpace(attrs[pos])) pos++;
    std::string_view value;
    if (pos < attrs.size() && attrs[pos] == '=') {
      pos++;
      while (pos < attrs.size() && isSpace(attrs[pos])) pos++;
      if (pos < attrs.size() && (attrs[pos] == '"' || attrs[pos] == '\'')) {
        const char quote = attrs[pos++];
        const size_t end = attrs.find(quote, pos);
        value = attrs.substr(pos, end == std::string_view::npos ? attrs.size() - pos : end - pos);
        pos = end == std::string_view::npos ? attrs.size() : end + 1;
      } else {
        const size_t start = pos;
        while (pos < attrs.size() && !isSpace(attrs[pos])) pos++;
        value = attrs.substr(start, pos - start);
      }
    } else if (pos < attrs.size() && attrs[pos] == '/') {
      pos++;
    }
    if (key.size() == name.size()) {
      bool same = true;
      for (size_t i = 0; i < key.size() && same; i++) same = lower(key[i]) == name[i];
      if (same) return value;
    }
    if (key.empty() && value.empty()) pos++;  // stray character: keep moving
  }
  return {};
}

void HtmlFilter::openTag(uint8_t id, std::string_view attrs) {
  const TagDef& def = TAGS[id];
  if (def.kind & BLOCK) {
    // HTML closes an open paragraph when a block starts, and an open item when a sibling starts.
    if (id == TAG_LI) {
      const int li = find(TAG_LI);
      const int ul = find(TAG_UL), ol = find(TAG_OL);
      const int list = ul > ol ? ul : ol;
      if (li >= 0 && li > list) closeTo(static_cast<size_t>(li));
    } else if (id != TAG_UL && id != TAG_OL) {
      const int p = find(TAG_P);
      if (p >= 0) {
        // A block inside a paragraph is only legal for inline-ish content; Mobipocket books nest
        // <p> inside <p> or <div> inside <p> freely, so close the paragraph first.
        closeTo(static_cast<size_t>(p));
      }
    }
  }
  if (def.kind & VOID) {
    if (id == TAG_IMG) {
      const std::string_view rec = attrValue(attrs, "recindex");
      if (rec.empty() || !resolveImage_) return;
      uint32_t index = 0;
      for (const char c : rec) {
        if (c < '0' || c > '9') return;
        index = index * 10 + static_cast<uint32_t>(c - '0');
      }
      std::string path;
      if (!resolveImage_(imageCtx_, index, path) || path.empty()) return;
      out("<img src=\"");
      for (const char c : path) {
        if (c == '"')
          out("&quot;");
        else if (c == '&')
          out("&amp;");
        else if (c == '<')
          out("&lt;");
        else
          out(std::string_view(&c, 1));
      }
      out("\" />");
      return;
    }
    out("<");
    out(def.out);
    out(" />");
    return;
  }
  if (depth_ >= MAX_DEPTH) return;  // too deep: drop the tag, keep its text
  out("<");
  out(def.out);
  if (def.kind & BLOCK) {
    const std::string_view align = attrValue(attrs, "align");
    if (align == "center" || align == "right" || align == "justify" || align == "left") {
      out(" style=\"text-align:");
      out(align);
      out("\"");
    }
  }
  out(">");
  stack_[depth_++] = id;
}

void HtmlFilter::closeTag(uint8_t id) {
  // Pair on the emitted name so </center> closes a <div> and </font> a <span>.
  for (size_t i = depth_; i-- > 0;) {
    if (strcmp(TAGS[stack_[i]].out, TAGS[id].out) == 0) {
      closeTo(i);
      return;
    }
  }
}

void HtmlFilter::handleTag() {
  const std::string_view raw(tag_, tagLen_);
  if (raw.empty() || raw[0] == '!' || raw[0] == '?') return;  // doctype, processing instruction
  size_t pos = 0;
  const bool closing = raw[0] == '/';
  if (closing) pos++;
  while (pos < raw.size() && isSpace(raw[pos])) pos++;
  const size_t nameStart = pos;
  char name[16];
  size_t nameLen = 0;
  while (pos < raw.size() && !isSpace(raw[pos]) && raw[pos] != '/' && raw[pos] != '>') {
    if (nameLen < sizeof(name)) name[nameLen++] = lower(raw[pos]);
    pos++;
  }
  if (pos - nameStart > sizeof(name)) return;  // no tag we know is that long
  const uint8_t id = lookup(std::string_view(name, nameLen));
  if (skipId_ != 0) {
    // Inside <head>/<style>/...: only its own open/close nesting matters.
    if (id == skipId_) {
      if (closing) {
        if (--skipNest_ == 0) skipId_ = 0;
      } else if (raw.back() != '/') {
        skipNest_++;
      }
    }
    return;
  }
  if (id == 0) return;
  const TagDef& def = TAGS[id];
  if (def.kind & SKIP) {
    if (!closing && raw.back() != '/') {
      skipId_ = id;
      skipNest_ = 1;
    }
    return;
  }
  if (def.kind & PAGEBREAK) {
    const int p = find(TAG_P);
    if (p >= 0) closeTo(static_cast<size_t>(p));
    out("\n");
    return;
  }
  if (closing) {
    closeTag(id);
    return;
  }
  std::string_view attrs = raw.substr(pos);
  const bool selfClosing = !attrs.empty() && attrs.back() == '/';
  if (selfClosing) attrs.remove_suffix(1);
  openTag(id, attrs);
  if (selfClosing && !(def.kind & VOID)) closeTag(id);
}

void HtmlFilter::flushEntity(bool complete) {
  const std::string_view ent(entity_, entityLen_);
  entityLen_ = 0;
  if (complete) {
    if (ent.size() >= 3 && ent[1] == '#') {
      // Numeric: expat expands these itself; reject the control range it refuses.
      uint32_t value = 0;
      bool hex = ent[2] == 'x' || ent[2] == 'X';
      bool valid = ent.size() > (hex ? 4u : 3u);
      for (size_t i = hex ? 3 : 2; i + 1 < ent.size() && valid; i++) {
        const char c = ent[i];
        uint32_t d;
        if (c >= '0' && c <= '9')
          d = static_cast<uint32_t>(c - '0');
        else if (hex && lower(c) >= 'a' && lower(c) <= 'f')
          d = static_cast<uint32_t>(lower(c) - 'a' + 10);
        else {
          valid = false;
          break;
        }
        value = value * (hex ? 16 : 10) + d;
        if (value > 0x10FFFF) valid = false;
      }
      if (valid && value >= 0x80 && value <= 0x9F) {  // cp1252 "smart quotes" written as numeric refs
        char utf8[3];
        const size_t n = cp1252ToUtf8(static_cast<uint8_t>(value), utf8);
        out(std::string_view(utf8, n));
        return;
      }
      if (valid && (value == 0x9 || value == 0xA || value == 0xD ||
                    (value >= 0x20 && value != 0xFFFE && value != 0xFFFF && !(value >= 0xD800 && value <= 0xDFFF)))) {
        out(ent);
        return;
      }
      return;  // control character or malformed: drop
    }
    if (ent == "&amp;" || ent == "&lt;" || ent == "&gt;" || ent == "&quot;" || ent == "&apos;") {
      out(ent);
      return;
    }
    const char* utf8 = lookupHtmlEntity(ent.data(), ent.size());
    if (utf8) {
      out(utf8);
      return;
    }
  }
  // Not an entity: a literal ampersand followed by text.
  out("&amp;");
  out(ent.substr(1));
}

void HtmlFilter::text(char c) {
  if (skipId_ != 0) return;
  switch (c) {
    case '>':
      out("&gt;");
      break;
    case '\r':
      break;
    default:
      out(std::string_view(&c, 1));
      break;
  }
}

bool HtmlFilter::feed(std::string_view input) {
  for (const char c : input) {
    if (!ok_) return false;
    switch (mode_) {
      case Mode::Text:
        if (c == '<') {
          mode_ = Mode::Tag;
          tagLen_ = 0;
          tagOverflow_ = false;
        } else if (c == '&') {
          mode_ = Mode::Entity;
          entity_[0] = '&';
          entityLen_ = 1;
        } else {
          text(c);
        }
        break;
      case Mode::Entity:
        if (c == ';') {
          if (entityLen_ < MAX_ENTITY) entity_[entityLen_++] = ';';
          flushEntity(entityLen_ < MAX_ENTITY || entity_[MAX_ENTITY - 1] == ';');
          mode_ = Mode::Text;
        } else if (c == '#' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
          if (entityLen_ < MAX_ENTITY) {
            entity_[entityLen_++] = c;
          } else {
            flushEntity(false);
            mode_ = Mode::Text;
          }
        } else {
          flushEntity(false);
          mode_ = Mode::Text;
          if (c == '<') {
            mode_ = Mode::Tag;
            tagLen_ = 0;
            tagOverflow_ = false;
          } else if (c == '&') {
            mode_ = Mode::Entity;
            entity_[0] = '&';
            entityLen_ = 1;
          } else {
            text(c);
          }
        }
        break;
      case Mode::Tag:
        if (tagLen_ == 3 && tag_[0] == '!' && tag_[1] == '-' && tag_[2] == '-') {
          mode_ = Mode::Comment;
          commentDashes_ = c == '-' ? 1 : 0;
          break;
        }
        if (tagLen_ == 0 && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '/' || c == '!' || c == '?')) {
          // "a < b": a bare less-than is text, not a tag that would swallow everything to the next '>'
          out("&lt;");
          mode_ = Mode::Text;
          if (c == '<') {
            mode_ = Mode::Tag;
          } else if (c == '&') {
            mode_ = Mode::Entity;
            entity_[0] = '&';
            entityLen_ = 1;
          } else {
            text(c);
          }
          break;
        }
        if (c == '>') {
          if (!tagOverflow_) handleTag();
          mode_ = Mode::Text;
        } else if (tagLen_ < MAX_TAG) {
          tag_[tagLen_++] = c;
        } else {
          tagOverflow_ = true;
        }
        break;
      case Mode::Comment:
        if (c == '-') {
          commentDashes_++;
        } else if (c == '>' && commentDashes_ >= 2) {
          mode_ = Mode::Text;
        } else {
          commentDashes_ = 0;
        }
        break;
    }
  }
  return ok_;
}

bool HtmlFilter::finish() {
  if (mode_ == Mode::Entity) flushEntity(false);
  mode_ = Mode::Text;
  closeTo(0);
  return ok_;
}

}  // namespace mobi
