#include "Flashcards.h"

#include <cstdio>

#include "TextFold.h"

namespace flashcards {
namespace {

std::string_view trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

// "- ", "* ", "+ ", "1. " and "> " in front of a line are not part of the card.
std::string_view stripMarker(std::string_view s) {
  while (true) {
    s = trim(s);
    if (s.size() >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+' || s[0] == '>') && s[1] == ' ') {
      s.remove_prefix(2);
      continue;
    }
    size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    if (i > 0 && i + 1 < s.size() && s[i] == '.' && s[i + 1] == ' ') {
      s.remove_prefix(i + 2);
      continue;
    }
    return s;
  }
}

// The plugin's scheduling comment sits at the end of a card line; it isn't text.
std::string_view dropScheduleComment(std::string_view s) {
  const size_t at = s.find("<!--SR:");
  return at == std::string_view::npos ? s : trim(s.substr(0, at));
}

// True when `at` is inside an inline code span on this line (an odd number of backticks before it).
bool inCodeSpan(std::string_view line, size_t at) {
  size_t ticks = 0;
  for (size_t i = 0; i < at && i < line.size(); ++i) ticks += line[i] == '`';
  return ticks % 2 == 1;
}

size_t findSeparator(std::string_view line, std::string_view sep, size_t from = 0) {
  while (true) {
    const size_t at = line.find(sep, from);
    if (at == std::string_view::npos || !inCodeSpan(line, at)) return at;
    from = at + 1;
  }
}

struct Cloze {
  size_t begin;  // of the opener
  size_t end;    // one past the closer
  size_t open;   // opener length
};

std::vector<Cloze> findClozes(std::string_view line, bool bold) {
  std::vector<Cloze> out;
  size_t i = 0;
  while (i < line.size()) {
    const bool curly = line.compare(i, 2, "{{") == 0;
    const bool star = bold && line.compare(i, 2, "**") == 0;
    if (!curly && !star) {
      ++i;
      continue;
    }
    const std::string_view closer = curly ? "}}" : "**";
    const size_t close = line.find(closer, i + 2);
    if (close == std::string_view::npos || close == i + 2 || inCodeSpan(line, i)) {
      i += 2;
      continue;
    }
    out.push_back({i, close + 2, 2});
    i = close + 2;
  }
  return out;
}

// The line with every cloze shown, except `hide` which becomes "[...]" (npos: all shown).
std::string renderCloze(std::string_view line, const std::vector<Cloze>& clozes, size_t hide) {
  std::string out;
  size_t pos = 0;
  for (size_t k = 0; k < clozes.size(); ++k) {
    out.append(line.substr(pos, clozes[k].begin - pos));
    if (k == hide) {
      out += "[...]";
    } else {
      out.append(line.substr(clozes[k].begin + clozes[k].open, clozes[k].end - clozes[k].begin - 2 * clozes[k].open));
    }
    pos = clozes[k].end;
  }
  out.append(line.substr(pos));
  return out;
}

struct Builder {
  std::vector<Card>& cards;
  bool full() const { return cards.size() >= MAX_CARDS_PER_NOTE; }
  void add(std::string front, std::string back, uint32_t line) {
    if (full() || front.empty() || back.empty() || front.size() > MAX_FIELD_BYTES || back.size() > MAX_FIELD_BYTES) {
      return;
    }
    cards.push_back({std::move(front), std::move(back), line});
  }
  void addBoth(const std::string& a, const std::string& b, uint32_t line) {
    add(a, b, line);
    add(b, a, line);
  }
};

struct Row {
  std::string_view text;
  uint32_t line;
};

void blockCards(const std::vector<Row>& block, bool boldCloze, Builder& out) {
  // A `?` or `??` line with text on both sides makes the whole block one multi-line card.
  for (size_t k = 1; k + 1 < block.size(); ++k) {
    const std::string_view t = trim(dropScheduleComment(trim(block[k].text)));
    if (t != "?" && t != "??") continue;
    std::string front;
    std::string back;
    for (size_t i = 0; i < k; ++i) {
      if (i) front += '\n';
      front.append(trim(block[i].text));
    }
    for (size_t i = k + 1; i < block.size(); ++i) {
      if (i > k + 1) back += '\n';
      back.append(trim(dropScheduleComment(block[i].text)));
    }
    if (t == "??") {
      out.addBoth(front, back, block[0].line);
    } else {
      out.add(front, back, block[0].line);
    }
    return;
  }
  for (const Row& row : block) {
    const std::string_view line = stripMarker(dropScheduleComment(trim(row.text)));
    if (line.empty()) continue;
    const size_t triple = findSeparator(line, ":::");
    const size_t pair = triple == std::string_view::npos ? findSeparator(line, "::") : triple;
    if (pair != std::string_view::npos && pair > 0) {
      const std::string a(trim(line.substr(0, pair)));
      const std::string b(trim(line.substr(pair + (triple == std::string_view::npos ? 2 : 3))));
      if (triple == std::string_view::npos) {
        out.add(a, b, row.line);
      } else {
        out.addBoth(a, b, row.line);
      }
      continue;
    }
    const auto clozes = findClozes(line, boldCloze);
    for (size_t k = 0; k < clozes.size(); ++k) {
      out.add(renderCloze(line, clozes, k), renderCloze(line, clozes, std::string::npos), row.line);
    }
  }
}

}  // namespace

std::vector<Card> parseCards(const std::string_view text) {
  std::vector<Card> cards;
  Builder out{cards};
  bool boldCloze = false;
  bool inFence = false;
  uint32_t lineNo = 0;
  size_t pos = 0;

  // Frontmatter: a first line of "---" up to the next "---" or "...".
  bool inFront = false;
  bool frontDone = false;
  std::vector<Row> block;

  const auto flush = [&] {
    if (!block.empty()) blockCards(block, boldCloze, out);
    block.clear();
  };

  while (pos <= text.size() && !out.full()) {
    size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) end = text.size();
    const std::string_view raw = text.substr(pos, end - pos);
    const std::string_view line = trim(raw);
    ++lineNo;
    const bool lastLine = end >= text.size();
    pos = end + 1;

    if (lineNo == 1 && line == "---") {
      inFront = true;
      continue;
    }
    if (inFront) {
      if (line == "---" || line == "...") {
        inFront = false;
        frontDone = true;
      } else if (startsWith(line, "sr-cloze:") && trim(line.substr(9)) == "bold") {
        boldCloze = true;
      }
      continue;
    }
    (void)frontDone;
    if (startsWith(line, "```") || startsWith(line, "~~~")) {
      flush();
      inFence = !inFence;
      if (lastLine) break;
      continue;
    }
    if (inFence) {
      if (lastLine) break;
      continue;
    }
    if (line.empty() || startsWith(line, "<!--SR:") || startsWith(line, "%%")) {
      flush();
    } else {
      block.push_back({raw, lineNo});
    }
    if (lastLine) break;
  }
  flush();
  return cards;
}

uint64_t cardId(const std::string_view relPath, const Card& card) {
  const auto normal = [](std::string_view s) {
    std::string folded = textfold::fold(s);
    std::string out;
    bool space = true;
    for (const char c : folded) {
      const bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r';
      if (ws) {
        space = true;
        continue;
      }
      if (space && !out.empty()) out += ' ';
      space = false;
      out += c;
    }
    return out;
  };
  uint64_t h = 1469598103934665603ULL;
  const auto mix = [&h](std::string_view s) {
    for (const unsigned char c : s) {
      h ^= c;
      h *= 1099511628211ULL;
    }
  };
  mix(relPath);
  mix("\n");
  mix(normal(card.front));
  mix("\n");
  mix(normal(card.back));
  return h;
}

std::string plainText(const std::string_view md) {
  std::string out;
  out.reserve(md.size());
  size_t i = 0;
  while (i < md.size()) {
    const bool embed = md.compare(i, 3, "![[") == 0;
    if (embed || md.compare(i, 2, "[[") == 0) {
      const size_t open = i + (embed ? 3 : 2);
      const size_t close = md.find("]]", open);
      if (close != std::string_view::npos && md.find('\n', open) > close) {
        if (embed) {
          out += "[image]";
        } else {
          std::string_view inner = md.substr(open, close - open);
          const size_t bar = inner.find('|');
          if (bar != std::string_view::npos) inner.remove_prefix(bar + 1);
          out.append(inner);
        }
        i = close + 2;
        continue;
      }
    }
    if (md[i] == '[') {  // [text](url)
      const size_t close = md.find(']', i + 1);
      if (close != std::string_view::npos && close + 1 < md.size() && md[close + 1] == '(') {
        const size_t paren = md.find(')', close + 2);
        if (paren != std::string_view::npos && md.find('\n', i) > paren) {
          out.append(md.substr(i + 1, close - i - 1));
          i = paren + 1;
          continue;
        }
      }
    }
    if (md.compare(i, 2, "**") == 0 || md.compare(i, 2, "~~") == 0 || md.compare(i, 2, "==") == 0) {
      i += 2;
      continue;
    }
    if (md[i] == '`') {
      ++i;
      continue;
    }
    out += md[i++];
  }
  return out;
}

std::string idToHex(const uint64_t id) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(id));
  return buf;
}

}  // namespace flashcards
