#include "MarkdownToHtml.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "VaultLink.h"

namespace {

bool isSpace(const char c) { return c == ' ' || c == '\t'; }

std::string_view trimLeft(std::string_view s) {
  size_t i = 0;
  while (i < s.size() && isSpace(s[i])) ++i;
  return s.substr(i);
}

std::string_view trim(std::string_view s) {
  s = trimLeft(s);
  while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
  return s;
}

// Footnote ids become anchor ids: letters, digits and non-ASCII (Greek) kept, ASCII punctuation
// and spaces as '-'.
std::string footnoteId(std::string_view id) {
  std::string out;
  for (const char c : id) {
    const auto u = static_cast<unsigned char>(c);
    out += u >= 0x80 || std::isalnum(u) ? c : '-';
  }
  return out;
}

// Leading indentation in columns (a tab counts as 4).
int indentOf(std::string_view s) {
  int cols = 0;
  for (const char c : s) {
    if (c == ' ') {
      ++cols;
    } else if (c == '\t') {
      cols += 4;
    } else {
      break;
    }
  }
  return cols;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

void appendEscaped(std::string& out, const std::string_view text) {
  for (const char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        // XML 1.0 forbids C0 controls other than tab and newline.
        out += (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n') ? ' ' : c;
    }
  }
}

bool isImageName(std::string_view name) {
  const size_t dot = name.rfind('.');
  if (dot == std::string_view::npos) return false;
  std::string ext(name.substr(dot + 1));
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "gif" || ext == "bmp" || ext == "webp" || ext == "svg";
}

bool isAsciiPunct(const char c) { return std::ispunct(static_cast<unsigned char>(c)) != 0; }

bool isWordByte(const char c) {
  const auto u = static_cast<unsigned char>(c);
  return u >= 0x80 || std::isalnum(u) || c == '_';
}

// "Note#Heading" is shown as "Note > Heading", like Obsidian; a block reference shows the note.
std::string linkDisplay(std::string_view target) {
  const size_t hash = target.find('#');
  if (hash == std::string_view::npos) return std::string(target);
  const std::string_view note = target.substr(0, hash);
  const std::string_view part = target.substr(hash + 1);
  if (!part.empty() && part.front() == '^') return std::string(note);
  if (note.empty()) return std::string(part);
  return std::string(note) + " > " + std::string(part);
}

// Split "target|alias" (the pipe may be escaped as \| inside tables).
void splitAlias(std::string_view inner, std::string_view& target, std::string_view& alias) {
  size_t bar = inner.find('|');
  target = inner.substr(0, bar);
  if (!target.empty() && target.back() == '\\') target.remove_suffix(1);
  alias = bar == std::string_view::npos ? std::string_view{} : inner.substr(bar + 1);
}

struct Emphasis {
  const char* marker;
  const char* tag;
};
constexpr Emphasis EMPHASES[] = {{"**", "b"}, {"__", "b"}, {"~~", "s"}, {"==", "mark"}, {"*", "i"}, {"_", "i"}};

}  // namespace

std::string MarkdownToHtml::headingId(std::string_view text) {
  std::string id;
  for (const char c : trim(text)) {
    if (isSpace(c)) {
      if (!id.empty() && id.back() != '-') id += '-';
    } else {
      id += c;  // case is kept; the link resolver compares case-insensitively for every script
    }
  }
  return id;
}

std::string_view MarkdownToHtml::section(const std::string_view note, const std::string_view anchor) {
  if (anchor.empty()) return note;
  const bool block = anchor.front() == '^';
  const std::string wanted = block ? std::string(anchor) : headingId(anchor);
  bool fence = false;
  size_t paraStart = 0;  // first line of the current paragraph
  size_t start = std::string_view::npos;
  int level = 0;
  for (size_t pos = 0; pos < note.size();) {
    const size_t nl = note.find('\n', pos);
    const size_t end = nl == std::string_view::npos ? note.size() : nl;
    const size_t next = nl == std::string_view::npos ? note.size() : nl + 1;
    std::string_view lineText = note.substr(pos, end - pos);
    if (!lineText.empty() && lineText.back() == '\r') lineText.remove_suffix(1);  // CRLF notes
    const std::string_view t = trim(lineText);
    const bool fenceLine = startsWith(t, "```") || startsWith(t, "~~~");
    if (fenceLine) fence = !fence;
    // ATX headings only (the renderer has no setext ones); 4+ spaces of indent is code.
    int hashes = 0;
    const bool indented = lineText.find_first_not_of(' ') >= 4;
    while (!fence && !indented && hashes < static_cast<int>(t.size()) && t[hashes] == '#') ++hashes;
    const bool heading = hashes >= 1 && hashes <= 6 && static_cast<size_t>(hashes) < t.size() && t[hashes] == ' ';
    if (start != std::string_view::npos) {
      if (heading && hashes <= level) return note.substr(start, pos - start);
    } else if (block) {
      if (t.empty() || heading || fence || fenceLine) {  // a paragraph starts after these
        paraStart = next;
      } else if (wanted.size() > 1 && t.size() > wanted.size() && t.substr(t.size() - wanted.size()) == wanted &&
                 t[t.size() - wanted.size() - 1] == ' ') {
        // A list item is embedded alone ("- ", "* ", "+ ", "1. ", "1) "); its sub-items are not.
        size_t digits = 0;
        while (digits < t.size() && std::isdigit(static_cast<unsigned char>(t[digits]))) ++digits;
        const bool item =
            (t.size() > 1 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') ||
            (digits > 0 && digits + 1 < t.size() && (t[digits] == '.' || t[digits] == ')') && t[digits + 1] == ' ');
        const size_t from = item ? pos : paraStart;
        return note.substr(from, pos + lineText.size() - from);
      }
    } else if (heading && vaultlink::sameName(headingId(t.substr(hashes + 1)), wanted)) {
      start = pos;
      level = hashes;
    }
    pos = next;
  }
  return start == std::string_view::npos ? note : note.substr(start);
}

void MarkdownToHtml::inlineToHtml(std::string_view text, std::string& out, const ImageResolver* images) {
  // Writes an image source: the SD path when the resolver finds it, else the vault: name.
  auto imageSrc = [&out, images](std::string_view name) {
    const std::string path = images ? (*images)(name) : std::string();
    if (path.empty()) out += "vault:";
    appendEscaped(out, path.empty() ? name : std::string_view(path));
  };
  std::vector<const Emphasis*> open;
  auto closeTag = [&out](const Emphasis* e) {
    out += "</";
    out += e->tag;
    out += '>';
  };
  auto openTag = [&out](const Emphasis* e) {
    out += '<';
    out += e->tag;
    out += '>';
  };

  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    const char c = text[i];
    const std::string_view rest = text.substr(i);

    if (c == '\\' && i + 1 < n && isAsciiPunct(text[i + 1])) {
      appendEscaped(out, text.substr(i + 1, 1));
      i += 2;
      continue;
    }
    if (c == '`') {
      size_t run = 0;
      while (i + run < n && text[i + run] == '`') ++run;
      const std::string fenceRun(run, '`');
      const size_t end = text.find(fenceRun, i + run);
      if (end != std::string_view::npos) {
        appendEscaped(out, trim(text.substr(i + run, end - i - run)));
        i = end + run;
        continue;
      }
    }
    if (startsWith(rest, "%%")) {
      const size_t end = text.find("%%", i + 2);
      i = end == std::string_view::npos ? n : end + 2;
      continue;
    }
    // Footnote reference [^id] -> a raised number linking to its definition.
    if (startsWith(rest, "[^")) {
      const size_t end = text.find(']', i + 2);
      if (end != std::string_view::npos && end > i + 2 && (end + 1 >= n || text[end + 1] != ':')) {
        const std::string_view id = text.substr(i + 2, end - i - 2);
        out += "<a href=\"#fn-";
        appendEscaped(out, footnoteId(id));
        out += "\"><sup>";
        appendEscaped(out, id);
        out += "</sup></a>";
        i = end + 1;
        continue;
      }
    }
    if (startsWith(rest, "![[") || startsWith(rest, "[[")) {
      const bool embed = c == '!';
      const size_t start = i + (embed ? 3 : 2);
      const size_t end = text.find("]]", start);
      if (end != std::string_view::npos) {
        std::string_view target, alias;
        splitAlias(text.substr(start, end - start), target, alias);
        if (embed && isImageName(target.substr(0, target.find('#')))) {
          out += "<img src=\"";
          imageSrc(target.substr(0, target.find('#')));
          out += "\" alt=\"";
          appendEscaped(out, target);
          out += '"';
          // "|300" or "|300x200" sets the width; "|x200" (height only) is ignored.
          const std::string_view width = alias.substr(0, alias.find('x'));
          if (!width.empty() && std::all_of(width.begin(), width.end(),
                                            [](char d) { return std::isdigit(static_cast<unsigned char>(d)); })) {
            out += " width=\"";
            appendEscaped(out, width);
            out += '"';
          }
          out += "/>";
        } else {
          // Note embeds are drawn as links until the reader can inline them. [[#Heading]] stays
          // inside the note, using the heading's anchor id.
          if (!target.empty() && target.front() == '#') {
            out += "<a href=\"#";
            appendEscaped(out, headingId(target.substr(1)));
          } else {
            out += "<a href=\"vault:";
            appendEscaped(out, target);
          }
          out += "\">";
          if (alias.empty()) {
            appendEscaped(out, linkDisplay(target));
          } else {
            appendEscaped(out, alias);
          }
          out += "</a>";
        }
        i = end + 2;
        continue;
      }
    }
    if (c == '[' || startsWith(rest, "![")) {
      const bool image = c == '!';
      const size_t labelStart = i + (image ? 2 : 1);
      const size_t labelEnd = text.find(']', labelStart);
      if (labelEnd != std::string_view::npos && labelEnd + 1 < n && text[labelEnd + 1] == '(') {
        const size_t urlEnd = text.find(')', labelEnd + 2);
        if (urlEnd != std::string_view::npos) {
          const std::string_view label = text.substr(labelStart, labelEnd - labelStart);
          std::string_view url = trim(text.substr(labelEnd + 2, urlEnd - labelEnd - 2));
          if (!url.empty() && url.front() == '<' && url.back() == '>') url = url.substr(1, url.size() - 2);
          const bool external = url.find("://") != std::string_view::npos || startsWith(url, "mailto:");
          if (image) {
            if (external) {
              out += "<i>";
              appendEscaped(out, label.empty() ? url : label);
              out += "</i>";
            } else {
              out += "<img src=\"";
              imageSrc(url);
              out += "\" alt=\"";
              appendEscaped(out, label);
              out += "\"/>";
            }
          } else {
            out += "<a href=\"";
            if (!external) out += "vault:";
            appendEscaped(out, url);
            out += "\">";
            std::string inner;
            inlineToHtml(label, inner, images);
            out += inner;
            out += "</a>";
          }
          i = urlEnd + 1;
          continue;
        }
      }
    }
    if (c == '<' && (startsWith(rest, "<http://") || startsWith(rest, "<https://"))) {
      const size_t end = text.find('>', i);
      if (end != std::string_view::npos) {
        const std::string_view url = text.substr(i + 1, end - i - 1);
        out += "<a href=\"";
        appendEscaped(out, url);
        out += "\">";
        appendEscaped(out, url);
        out += "</a>";
        i = end + 1;
        continue;
      }
    }
    if ((startsWith(rest, "http://") || startsWith(rest, "https://")) && (i == 0 || !isWordByte(text[i - 1]))) {
      size_t end = i;
      while (end < n && !isSpace(text[end]) && text[end] != '<') ++end;
      while (end > i && std::strchr(".,;:!?)]'\"", text[end - 1]) != nullptr) --end;
      const std::string_view url = text.substr(i, end - i);
      out += "<a href=\"";
      appendEscaped(out, url);
      out += "\">";
      appendEscaped(out, url);
      out += "</a>";
      i = end;
      continue;
    }
    if (c == '#' && i + 1 < n && (i == 0 || isSpace(text[i - 1])) && !isSpace(text[i + 1]) && text[i + 1] != '#') {
      size_t end = i + 1;
      while (end < n && (isWordByte(text[end]) || text[end] == '-' || text[end] == '/')) ++end;
      const std::string_view tag = text.substr(i, end - i);
      const bool allDigits =
          std::all_of(tag.begin() + 1, tag.end(), [](char d) { return std::isdigit(static_cast<unsigned char>(d)); });
      if (tag.size() > 1 && !allDigits) {
        out += "<i>";
        appendEscaped(out, tag);
        out += "</i>";
        i = end;
        continue;
      }
    }
    if (c == '<') {
      // Raw HTML: drop the tag, keep the text around it.
      const size_t end = text.find('>', i);
      if (end != std::string_view::npos && i + 1 < n &&
          (std::isalpha(static_cast<unsigned char>(text[i + 1])) || text[i + 1] == '/' || text[i + 1] == '!')) {
        i = end + 1;
        continue;
      }
    }

    // Emphasis. "***" is bold + italic. A marker closes if it is already open, else opens.
    if (startsWith(rest, "***") && open.size() >= 2 && open[open.size() - 1]->tag[0] == 'i' &&
        open[open.size() - 2]->tag[0] == 'b') {
      closeTag(open.back());
      open.pop_back();
      closeTag(open.back());
      open.pop_back();
      i += 3;
      continue;
    }
    if (startsWith(rest, "***") &&
        std::none_of(open.begin(), open.end(),
                     [](const Emphasis* e) { return e->tag[0] == 'b' || e->tag[0] == 'i'; }) &&
        i + 3 < n && !isSpace(text[i + 3])) {
      open.push_back(&EMPHASES[0]);
      openTag(&EMPHASES[0]);
      open.push_back(&EMPHASES[4]);
      openTag(&EMPHASES[4]);
      i += 3;
      continue;
    }
    const Emphasis* matched = nullptr;
    for (const auto& e : EMPHASES) {
      if (startsWith(rest, e.marker)) {
        matched = &e;
        break;
      }
    }
    if (matched != nullptr) {
      const size_t len = std::strlen(matched->marker);
      const bool underscore = matched->marker[0] == '_';
      const auto it = std::find_if(open.begin(), open.end(),
                                   [matched](const Emphasis* e) { return std::strcmp(e->tag, matched->tag) == 0; });
      const bool before = i > 0 && !isSpace(text[i - 1]);
      const bool after = i + len < n && !isSpace(text[i + len]);
      // Intraword underscores (snake_case) are literal, as in Obsidian.
      const bool intraword = underscore && i > 0 && isWordByte(text[i - 1]) && i + len < n && isWordByte(text[i + len]);
      if (!intraword && it != open.end() && before) {
        // Close it, closing and reopening anything opened after it, so tags stay nested.
        std::vector<const Emphasis*> reopen(it + 1, open.end());
        for (auto r = open.rbegin(); r != open.rend() && *r != *it; ++r) closeTag(*r);
        closeTag(*it);
        open.erase(it, open.end());
        for (const auto* e : reopen) {
          openTag(e);
          open.push_back(e);
        }
        i += len;
        continue;
      }
      if (!intraword && it == open.end() && after) {
        openTag(matched);
        open.push_back(matched);
        i += len;
        continue;
      }
    }

    appendEscaped(out, text.substr(i, 1));
    ++i;
  }
  for (auto r = open.rbegin(); r != open.rend(); ++r) closeTag(*r);
}

void MarkdownToHtml::writeEscaped(std::string_view text) {
  std::string out;
  appendEscaped(out, text);
  sink.write(out);
}

void MarkdownToHtml::begin(std::string_view title, std::string_view prolog) {
  sink.write("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
  if (!prolog.empty()) {
    sink.write(prolog);
    sink.write("\n");
  }
  sink.write("<!DOCTYPE html>\n<html>\n<head><title>");
  writeEscaped(title);
  sink.write("</title></head>\n<body>\n");
}

void MarkdownToHtml::feed(std::string_view chunk) {
  for (const char c : chunk) {
    if (c == '\n') {
      line(pendingLine);
      pendingLine.clear();
    } else if (c != '\r') {
      pendingLine += c;
      if (pendingLine.size() >= MAX_LINE) {
        // Cut at a UTF-8 boundary and pass the rest on as a continuation of the same line, so a
        // long line is never re-read as a new block.
        size_t cut = pendingLine.size();
        while (cut > 0 && (static_cast<unsigned char>(pendingLine[cut - 1]) & 0xC0) == 0x80) --cut;
        if (cut > 0 && static_cast<unsigned char>(pendingLine[cut - 1]) >= 0xC0) --cut;
        if (cut == 0) cut = pendingLine.size();
        const std::string tail = pendingLine.substr(cut);
        pendingLine.resize(cut);
        continuation(pendingLine);
        pendingLine = tail;
      }
    }
  }
}

void MarkdownToHtml::finish(const bool closeDocument) {
  if (!pendingLine.empty()) {
    line(pendingLine);
    pendingLine.clear();
  }
  if (block == Block::Code) sink.write("</pre>\n");
  block = Block::None;
  closeAll();
  if (closeDocument) sink.write("</body>\n</html>\n");
}

const char* const MarkdownToHtml::CALLOUT_OPEN =
    "<blockquote style=\"margin:0.5em 0;border:1px solid #000;padding:0.2em 0.5em\">";

// "![[Note]]" alone on a line: the note's text, indented under its linked title. One level only:
// the embedded note's own embeds stay links, so notes that embed each other can't loop.
bool MarkdownToHtml::embedNote(std::string_view trimmed) {
  if (!noteLoader || !startsWith(trimmed, "![[")) return false;
  trimmed = trim(trimmed);
  if (trimmed.size() < 6 || trimmed.substr(trimmed.size() - 2) != "]]") return false;
  std::string_view target, alias;
  splitAlias(trimmed.substr(3, trimmed.size() - 5), target, alias);
  const std::string_view name = target.substr(0, target.find('#'));
  if (name.empty() || isImageName(name) || name.find("]]") != std::string_view::npos) return false;
  // Transient: freed when this returns; capped at MAX_EMBED by the loader.
  std::string text;
  if (!noteLoader(name, text)) return false;
  const size_t hash = target.find('#');
  if (hash != std::string_view::npos) {
    const std::string_view part = section(text, target.substr(hash + 1));
    if (part.size() != text.size()) text = std::string(part);
  }
  closeAll();
  std::string html = "<blockquote style=\"margin-left:1em\"><p><b><a href=\"vault:";
  appendEscaped(html, target);
  html += "\">";
  appendEscaped(html, alias.empty() ? linkDisplay(name) : alias);
  html += "</a></b></p>\n";
  sink.write(html);
  MarkdownToHtml inner(sink);
  inner.imageResolver = imageResolver;
  inner.feed(text);
  inner.finish(false);
  sink.write("</blockquote>\n");
  return true;
}

void MarkdownToHtml::continuation(std::string_view text) {
  if (block == Block::Code) {
    writeEscaped(text);
    inContinuation = true;
    return;
  }
  if (block == Block::Frontmatter || block == Block::Comment) {
    inContinuation = true;
    return;
  }
  if (inContinuation) {
    paragraph += text;
    if (paragraph.size() >= MAX_PARAGRAPH) closeParagraph();
    return;
  }
  // First piece of a long line: classify it normally, the rest joins the same paragraph.
  line(text);
  inContinuation = true;
}

void MarkdownToHtml::closeParagraph() {
  if (paragraph.empty()) return;
  std::string text = paragraph;
  paragraph.clear();
  // A trailing block id (" ^id") becomes the element's anchor and is not shown.
  std::string id;
  const size_t caret = text.rfind(" ^");
  if (caret != std::string::npos && caret + 2 < text.size() &&
      std::all_of(text.begin() + static_cast<std::ptrdiff_t>(caret) + 2, text.end(),
                  [](char d) { return std::isalnum(static_cast<unsigned char>(d)) || d == '-'; })) {
    id = text.substr(caret + 2);
    text.resize(caret);
  }
  std::string html;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t nl = text.find('\n', start);
    if (start > 0) html += "<br/>";
    inlineToHtml(std::string_view(text).substr(start, nl == std::string::npos ? std::string::npos : nl - start), html,
                 images());
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  if (listItemOpen && !quoteOpen) {
    if (!id.empty()) html = "<span id=\"^" + id + "\">" + html + "</span>";
    sink.write(html);
    return;
  }
  sink.write(id.empty() ? std::string("<p>") : "<p id=\"^" + id + "\">");
  sink.write(html);
  sink.write("</p>\n");
}

void MarkdownToHtml::closeLists(const int toDepth) {
  closeParagraph();
  while (static_cast<int>(lists.size()) > toDepth) {
    sink.write(lists.back().ordered ? "</li></ol>\n" : "</li></ul>\n");
    lists.pop_back();
  }
  listItemOpen = !lists.empty();
}

void MarkdownToHtml::closeQuote() {
  if (!quoteOpen) return;
  closeParagraph();
  sink.write("</blockquote>\n");
  quoteOpen = false;
}

void MarkdownToHtml::closeAll() {
  flushTable();
  closeQuote();
  closeLists(0);
  closeParagraph();
}

void MarkdownToHtml::heading(const int level, std::string_view text) {
  closeAll();
  text = trim(text);
  while (!text.empty() && text.back() == '#') text.remove_suffix(1);
  text = trim(text);
  std::string html = "<h" + std::to_string(level) + " id=\"";
  appendEscaped(html, headingId(text));
  html += "\">";
  inlineToHtml(text, html, images());
  html += "</h" + std::to_string(level) + ">\n";
  sink.write(html);
}

void MarkdownToHtml::openList(const bool ordered, const int number) {
  // Nested lists step in; the reader adds the container margins up. A numbered list keeps the
  // number it starts with.
  std::string tag = ordered ? "<ol" : "<ul";
  if (!lists.empty()) tag += " style=\"margin-left:1.2em\"";
  if (ordered && number != 1) tag += " start=\"" + std::to_string(number) + "\"";
  tag += ">\n";
  sink.write(tag);
}

void MarkdownToHtml::listItem(const int indent, const bool ordered, const int number, std::string_view content) {
  closeQuote();
  flushTable();
  closeParagraph();
  while (!lists.empty() && indent < lists.back().indent) {
    sink.write(lists.back().ordered ? "</li></ol>\n" : "</li></ul>\n");
    lists.pop_back();
  }
  if (lists.empty() || indent > lists.back().indent) {
    openList(ordered, number);
    lists.push_back({ordered, indent});
  } else if (lists.back().ordered != ordered) {
    sink.write(lists.back().ordered ? "</li></ol>\n" : "</li></ul>\n");
    lists.pop_back();
    openList(ordered, number);
    lists.push_back({ordered, indent});
  } else {
    sink.write("</li>\n");
  }
  sink.write("<li>");
  listItemOpen = true;
  if (startsWith(content, "[ ] ") || content == "[ ]") {
    sink.write("[ ] ");
    content = content.substr(std::min<size_t>(4, content.size()));
  } else if (startsWith(content, "[x] ") || startsWith(content, "[X] ") || content == "[x]" || content == "[X]") {
    sink.write("[x] ");
    content = content.substr(std::min<size_t>(4, content.size()));
  }
  paragraph.assign(content);
}

void MarkdownToHtml::flushTable(const bool more) {
  if (tableRows.empty()) return;
  std::vector<std::string> rows;
  rows.swap(tableRows);
  tableBytes = 0;
  if (block == Block::Table && !more) block = Block::None;
  if (more) {
    // Keep the header and separator rows for the next part.
    for (size_t r = 0; r < rows.size() && r < 2; ++r) {
      tableRows.push_back(rows[r]);
      tableBytes += rows[r].size();
    }
  }

  auto isSeparator = [](std::string_view row) {
    row = trim(row);
    bool dash = false;
    for (const char c : row) {
      if (c == '-') {
        dash = true;
      } else if (c != '|' && c != ':' && !isSpace(c)) {
        return false;
      }
    }
    return dash;
  };
  const auto sep = std::find_if(rows.begin(), rows.end(), isSeparator);
  if (sep == rows.end()) {
    // Not a table after all: show the lines as a paragraph.
    for (const auto& row : rows) {
      if (!paragraph.empty()) paragraph += '\n';
      paragraph += row;
    }
    closeParagraph();
    return;
  }

  auto cells = [](std::string_view row) {
    row = trim(row);
    if (!row.empty() && row.front() == '|') row.remove_prefix(1);
    if (!row.empty() && row.back() == '|') row.remove_suffix(1);
    std::vector<std::string_view> out;
    size_t start = 0;
    int linkDepth = 0;
    for (size_t i = 0; i < row.size(); ++i) {
      if (row.compare(i, 2, "[[") == 0) ++linkDepth;
      if (row.compare(i, 2, "]]") == 0 && linkDepth > 0) --linkDepth;
      if (row[i] == '|' && (i == 0 || row[i - 1] != '\\') && linkDepth == 0) {
        out.push_back(trim(row.substr(start, i - start)));
        start = i + 1;
      }
    }
    out.push_back(trim(row.substr(start)));
    return out;
  };

  std::string html = "<table>\n";
  bool header = true;
  for (auto it = rows.begin(); it != rows.end(); ++it) {
    if (it == sep) {
      header = false;
      continue;
    }
    html += "<tr>";
    for (const auto cell : cells(*it)) {
      html += header ? "<th>" : "<td>";
      inlineToHtml(cell, html, images());
      html += header ? "</th>" : "</td>";
    }
    html += "</tr>\n";
  }
  html += "</table>\n";
  sink.write(html);
}

void MarkdownToHtml::line(std::string_view text) {
  if (inContinuation) {
    // The end of a long line: it belongs to the paragraph (or code block) it started.
    inContinuation = false;
    if (block == Block::Code) {
      writeEscaped(text);
      sink.write("\n");
    } else if (block != Block::Frontmatter && block != Block::Comment) {
      paragraph += text;
    }
    return;
  }
  if (firstLine) {
    firstLine = false;
    if (trim(text) == "---") {
      block = Block::Frontmatter;
      return;
    }
  }
  if (block == Block::Frontmatter) {
    if (trim(text) == "---" || trim(text) == "...") block = Block::None;
    return;
  }
  const std::string_view trimmed = trimLeft(text);
  const int indent = indentOf(text);

  if (block == Block::Code) {
    if (startsWith(trimmed, fence) && trim(trimmed).find_first_not_of(fence[0]) == std::string_view::npos) {
      sink.write("</pre>\n");
      block = Block::None;
    } else {
      writeEscaped(text);
      sink.write("\n");
    }
    return;
  }
  if (block == Block::Comment) {
    if (text.find("%%") != std::string_view::npos) block = Block::None;
    return;
  }

  if (startsWith(trimmed, "```") || startsWith(trimmed, "~~~")) {
    flushTable();
    closeQuote();
    closeParagraph();
    size_t run = 0;
    while (run < trimmed.size() && trimmed[run] == trimmed[0]) ++run;
    fence.assign(run, trimmed[0]);
    sink.write("<pre>");
    block = Block::Code;
    return;
  }
  if (startsWith(trimmed, "%%") && trimmed.find("%%", 2) == std::string_view::npos) {
    closeParagraph();
    block = Block::Comment;
    return;
  }

  if (trim(text).empty()) {
    flushTable();
    closeQuote();
    closeParagraph();
    return;
  }

  if (block == Block::Table || (startsWith(trimmed, "|") && paragraph.empty() && !quoteOpen)) {
    if (startsWith(trimmed, "|")) {
      if (block != Block::Table) {
        closeLists(0);
        block = Block::Table;
      }
      tableRows.emplace_back(trimmed);
      tableBytes += trimmed.size();
      // Big tables are written out in parts (each part keeps the header) to bound memory.
      if (tableRows.size() >= MAX_TABLE_ROWS || tableBytes >= MAX_PARAGRAPH) flushTable(true);
      return;
    }
    flushTable();
  }

  if (block == Block::None && embedNote(trimmed)) return;

  if (trimmed[0] == '#') {
    size_t level = 0;
    while (level < trimmed.size() && trimmed[level] == '#') ++level;
    if (level <= 6 && level < trimmed.size() && trimmed[level] == ' ') {
      heading(static_cast<int>(level), trimmed.substr(level + 1));
      return;
    }
  }

  {
    const std::string_view t = trim(text);
    const char r = t[0];
    if ((r == '-' || r == '*' || r == '_') && t.size() >= 3 &&
        std::all_of(t.begin(), t.end(), [r](char d) { return d == r || isSpace(d); }) &&
        std::count(t.begin(), t.end(), r) >= 3) {
      closeAll();
      sink.write("<hr/>\n");
      return;
    }
  }

  if (trimmed[0] == '>') {
    std::string_view content = trimmed;
    while (!content.empty() && content[0] == '>') content = trimLeft(content.substr(1));
    if (!quoteOpen) {
      closeLists(0);
      closeParagraph();
      quoteOpen = true;
      // "[!type]": a letter, then letters, digits or '-' up to ']' ("> [![alt](src)]" is no callout).
      size_t close = std::string_view::npos;
      if (startsWith(content, "[!") && content.size() > 3 && std::isalpha(static_cast<unsigned char>(content[2]))) {
        size_t end = 3;
        while (end < content.size() &&
               (std::isalnum(static_cast<unsigned char>(content[end])) || content[end] == '-')) {
          ++end;
        }
        if (end < content.size() && content[end] == ']') close = end;
      }
      // A callout is a framed box with its title in bold; a plain quote is indented.
      sink.write(close != std::string_view::npos ? CALLOUT_OPEN : "<blockquote style=\"margin-left:1em\">");
      if (close != std::string_view::npos) {
        std::string_view type = content.substr(2, close - 2);
        std::string_view title = content.substr(close + 1);
        if (!title.empty() && (title[0] == '+' || title[0] == '-')) title.remove_prefix(1);
        title = trim(title);
        std::string html = "<p><b>";
        if (title.empty()) {
          std::string name(type);
          name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
          appendEscaped(html, name);
        } else {
          inlineToHtml(title, html, images());
        }
        html += "</b></p>\n";
        sink.write(html);
        return;
      }
    }
    if (content.empty()) {
      closeParagraph();
      return;
    }
    // A list item inside a quote: its own line with the marker as text ("•" or the number), since
    // the quote holds paragraphs, not lists.
    {
      std::string_view item = content;
      std::string marker;
      if ((item[0] == '-' || item[0] == '*' || item[0] == '+') && item.size() > 1 && isSpace(item[1])) {
        marker = "\xe2\x80\xa2 ";
        item = trimLeft(item.substr(1));
      } else {
        size_t d = 0;
        while (d < item.size() && d < 9 && std::isdigit(static_cast<unsigned char>(item[d]))) ++d;
        if (d > 0 && d + 1 < item.size() && (item[d] == '.' || item[d] == ')') && isSpace(item[d + 1])) {
          marker = std::string(item.substr(0, d + 1)) + " ";
          item = trimLeft(item.substr(d + 1));
        }
      }
      if (!marker.empty()) {
        closeParagraph();
        std::string html = "<p>" + marker;
        inlineToHtml(item, html, images());
        html += "</p>\n";
        sink.write(html);
        return;
      }
    }
    if (!paragraph.empty()) paragraph += '\n';
    paragraph += content;
    if (paragraph.size() >= MAX_PARAGRAPH) closeParagraph();
    return;
  }
  // Footnote definition "[^id]: text": a paragraph the references jump to.
  if (indent == 0 && startsWith(trimmed, "[^")) {
    const size_t close = trimmed.find("]:");
    if (close != std::string_view::npos && close > 2) {
      closeAll();  // a definition ends any list, quote or table above it
      const std::string_view id = trimmed.substr(2, close - 2);
      std::string html = "<p id=\"fn-";
      appendEscaped(html, footnoteId(id));
      html += "\"><sup>";
      appendEscaped(html, id);
      html += "</sup> ";
      inlineToHtml(trimLeft(trimmed.substr(close + 2)), html, images());
      html += "</p>\n";
      sink.write(html);
      return;
    }
  }

  if (quoteOpen) closeQuote();

  {
    size_t markerLen = 0;
    bool ordered = false;
    int number = 0;
    if ((trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed.size() > 1 && isSpace(trimmed[1])) {
      markerLen = 1;
    } else {
      size_t d = 0;
      while (d < trimmed.size() && d < 9 && std::isdigit(static_cast<unsigned char>(trimmed[d]))) ++d;
      if (d > 0 && d + 1 < trimmed.size() && (trimmed[d] == '.' || trimmed[d] == ')') && isSpace(trimmed[d + 1])) {
        markerLen = d + 1;
        ordered = true;
        number = std::atoi(std::string(trimmed.substr(0, d)).c_str());
      }
    }
    if (markerLen > 0) {
      listItem(indent, ordered, number, trimLeft(trimmed.substr(markerLen)));
      return;
    }
  }

  if (!lists.empty()) {
    if (indent == 0 && paragraph.empty()) {
      closeLists(0);
    } else {
      // Continuation line of the open list item.
      if (!paragraph.empty()) paragraph += '\n';
      paragraph += trimmed;
      if (paragraph.size() >= MAX_PARAGRAPH) closeParagraph();
      return;
    }
  }

  if (indent >= 4 && paragraph.empty()) {
    sink.write("<pre>");
    writeEscaped(text.substr(std::min<size_t>(4, text.size())));
    sink.write("</pre>\n");
    return;
  }

  if (!paragraph.empty()) paragraph += '\n';
  paragraph += trimmed;
  if (paragraph.size() >= MAX_PARAGRAPH) closeParagraph();
}
