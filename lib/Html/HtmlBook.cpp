#include "HtmlBook.h"

#include <Epub/htmlEntities.h>
#include <MobiHtmlFilter.h>

#include <memory>
#include <new>

namespace htmlbook {
namespace {

constexpr size_t CHUNK = 1024;
constexpr size_t MAX_SNIFF_TAG = 256;  // a longer tag in the head is not looked into

char lower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

// The value after `key` (which ends in '=') inside a lowercased tag: quotes skipped, ends at a quote, a space, ';'
// or the end.
std::string_view valueAfter(const std::string_view tag, const std::string_view key) {
  const size_t at = tag.find(key);
  if (at == std::string_view::npos) return {};
  size_t pos = at + key.size();
  while (pos < tag.size() && (isSpace(tag[pos]) || tag[pos] == '"' || tag[pos] == '\'')) pos++;
  const size_t start = pos;
  while (pos < tag.size() && !isSpace(tag[pos]) && tag[pos] != '"' && tag[pos] != '\'' && tag[pos] != ';' &&
         tag[pos] != '/' && tag[pos] != '?') {
    pos++;
  }
  return tag.substr(start, pos - start);
}

void appendCapped(std::string& text, const std::string_view piece) {
  if (text.size() + piece.size() <= MAX_TITLE) text.append(piece);
}

// The raw bytes of a <title> -> UTF-8 with entities decoded and white space folded.
std::string decodeTitle(std::string_view raw, const codepage::Id encoding) {
  if (raw.size() > MAX_TITLE) raw = raw.substr(0, MAX_TITLE);
  char utf8[MAX_TITLE * codepage::Decoder::MAX_OUT_PER_BYTE + codepage::Decoder::MAX_PENDING_OUT] = {};
  codepage::Decoder decoder(encoding);
  // No finish(): a title cut at MAX_TITLE inside a character loses that character.
  const std::string_view text(utf8, decoder.feed(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), utf8));
  std::string title;
  bool space = false;
  size_t i = 0;
  while (i < text.size()) {
    const char c = text[i];
    if (isSpace(c)) {
      space = !title.empty();
      i++;
      continue;
    }
    if (space) appendCapped(title, " ");
    space = false;
    if (c == '&') {
      const size_t end = text.find(';', i);
      if (end != std::string_view::npos && end - i <= 10) {
        const std::string_view entity = text.substr(i, end - i + 1);
        if (entity.size() > 3 && entity[1] == '#') {
          char one[4];
          size_t n = 0;
          const bool hex = lower(entity[2]) == 'x';
          uint32_t value = 0;
          bool valid = true;
          for (size_t k = hex ? 3 : 2; k + 1 < entity.size() && valid; k++) {
            const char d = lower(entity[k]);
            if (d >= '0' && d <= '9') {
              value = value * (hex ? 16 : 10) + static_cast<uint32_t>(d - '0');
            } else if (hex && d >= 'a' && d <= 'f') {
              value = value * 16 + static_cast<uint32_t>(d - 'a' + 10);
            } else {
              valid = false;
            }
            if (value > 0x10FFFF) valid = false;
          }
          if (valid && value >= 0x20 && codepage::xmlChar(value)) n = codepage::encodeUtf8(value, one);
          if (n > 0) appendCapped(title, std::string_view(one, n));
          i = end + 1;
          continue;
        }
        if (const char* named = lookupHtmlEntity(entity.data(), entity.size())) {
          appendCapped(title, named);
          i = end + 1;
          continue;
        }
      }
    }
    appendCapped(title, std::string_view(&c, 1));
    i++;
  }
  // The cap may have cut the last character: drop a broken tail.
  size_t keep = title.size();
  while (keep > 0 && (static_cast<unsigned char>(title[keep - 1]) & 0xC0) == 0x80) keep--;
  if (keep > 0) {
    const unsigned char lead = static_cast<unsigned char>(title[keep - 1]);
    const size_t want = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    if (title.size() - (keep - 1) < want) title.resize(keep - 1);
  }
  return title;
}

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

}  // namespace

const char* statusText(const Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::IoError:
      return "read error";
    case Status::Oom:
      return "out of memory";
    case Status::Unsupported:
      return "UTF-16 is not read";
    case Status::WriteError:
      return "write error";
  }
  return "?";
}

void sniff(std::string_view head, Info& info) {
  info = Info{};
  if (head.size() > SNIFF_SIZE) head = head.substr(0, SNIFF_SIZE);
  if (head.size() >= 2 && ((head[0] == '\xFF' && head[1] == '\xFE') || (head[0] == '\xFE' && head[1] == '\xFF'))) {
    info.utf16 = true;
    return;
  }
  bool named = false;
  if (head.size() >= 3 && head.substr(0, 3) == "\xEF\xBB\xBF") {
    info.bomSize = 3;
    named = true;  // a BOM wins over what the page says
  }
  std::string_view rawTitle;
  size_t pos = info.bomSize;
  while (pos < head.size()) {
    const size_t open = head.find('<', pos);
    if (open == std::string_view::npos) break;
    if (head.substr(open, 4) == "<!--") {
      const size_t end = head.find("-->", open + 4);
      if (end == std::string_view::npos) break;
      pos = end + 3;
      continue;
    }
    const size_t close = head.find('>', open);
    if (close == std::string_view::npos) break;
    pos = close + 1;
    const std::string_view rawTag = head.substr(open + 1, close - open - 1);
    if (rawTag.size() > MAX_SNIFF_TAG) continue;
    char buf[MAX_SNIFF_TAG] = {};
    for (size_t i = 0; i < rawTag.size(); i++) buf[i] = lower(rawTag[i]);
    const std::string_view tag(buf, rawTag.size());
    size_t nameEnd = 0;
    while (nameEnd < tag.size() && !isSpace(tag[nameEnd]) && tag[nameEnd] != '/') nameEnd++;
    const std::string_view name = tag.substr(0, nameEnd);
    if (name == "?xml" || name == "meta") {
      const std::string_view value = valueAfter(tag, name == "meta" ? "charset=" : "encoding=");
      codepage::Id id;
      if (!named && !value.empty()) {
        if (value == "utf-16" || value == "utf16" || value == "utf-16le" || value == "utf-16be") {
          // Named UTF-16 yet readable as bytes: the name is wrong (a page re-saved as UTF-8). Read it as UTF-8.
          named = true;
        } else if (codepage::fromName(value, id)) {
          info.encoding = id;
          named = true;
        }
      }
    } else if (name == "html") {
      std::string_view lang = valueAfter(tag, " lang=");
      if (lang.empty()) lang = valueAfter(tag, "xml:lang=");
      if (lang.size() >= 2 && lang[0] >= 'a' && lang[0] <= 'z' && lang[1] >= 'a' && lang[1] <= 'z') {
        info.language.assign(lang.substr(0, 2));
      }
    } else if (name == "title" && rawTitle.empty() && (tag.empty() || tag.back() != '/')) {
      const size_t end = head.find('<', pos);
      if (end == std::string_view::npos) break;  // the title runs past what is looked at: not used
      rawTitle = head.substr(pos, end - pos);
    } else if (name == "body") {
      break;
    }
  }
  info.title = decodeTitle(rawTitle, info.encoding);
}

Status readInfo(void* ctx, const Read read, Info& info) {
  std::unique_ptr<uint8_t[]> head(new (std::nothrow) uint8_t[SNIFF_SIZE]);
  if (!head) return Status::Oom;
  const int n = readFully(ctx, read, head.get(), SNIFF_SIZE);
  if (n < 0) return Status::IoError;
  sniff(std::string_view(reinterpret_cast<const char*>(head.get()), static_cast<size_t>(n)), info);
  return info.utf16 ? Status::Unsupported : Status::Ok;
}

Status streamHtml(void* ctx, const Read read, const std::string_view title, const xhtml::Emit emit, void* emitCtx) {
  constexpr size_t UTF8_SIZE = SNIFF_SIZE * codepage::Decoder::MAX_OUT_PER_BYTE + codepage::Decoder::MAX_PENDING_OUT;
  std::unique_ptr<uint8_t[]> in(new (std::nothrow) uint8_t[SNIFF_SIZE]);
  std::unique_ptr<char[]> utf8(new (std::nothrow) char[UTF8_SIZE]);
  std::unique_ptr<xhtml::Out> out(new (std::nothrow) xhtml::Out(emit, emitCtx));
  if (!in || !utf8 || !out) return Status::Oom;
  const auto toOut = [](void* c, const char* data, size_t size) {
    auto* o = static_cast<xhtml::Out*>(c);
    o->write(std::string_view(data, size));
    return o->ok();
  };
  std::unique_ptr<mobi::HtmlFilter> filter(new (std::nothrow) mobi::HtmlFilter(toOut, out.get()));
  if (!filter) return Status::Oom;

  int n = readFully(ctx, read, in.get(), SNIFF_SIZE);
  if (n < 0) return Status::IoError;
  Info info;
  sniff(std::string_view(reinterpret_cast<const char*>(in.get()), static_cast<size_t>(n)), info);
  if (info.utf16) return Status::Unsupported;
  out->head(CACHE_VERSION_TAG, title.empty() ? std::string_view(info.title) : title);

  codepage::Decoder decoder(info.encoding);
  size_t skip = info.bomSize;
  while (n > 0) {
    const size_t len = decoder.feed(in.get() + skip, static_cast<size_t>(n) - skip, utf8.get());
    skip = 0;
    if (!filter->feed(std::string_view(utf8.get(), len))) return Status::WriteError;
    n = read(ctx, in.get(), CHUNK);
    if (n < 0) return Status::IoError;
  }
  filter->feed(std::string_view(utf8.get(), decoder.finish(utf8.get())));
  filter->finish();
  return out->tail() ? Status::Ok : Status::WriteError;
}

}  // namespace htmlbook
