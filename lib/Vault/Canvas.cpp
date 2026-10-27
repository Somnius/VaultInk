#include "Canvas.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace canvas {
namespace {

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Cuts `s` to at most `cap` bytes without splitting a UTF-8 sequence.
void capUtf8(std::string& s, const size_t cap) {
  if (s.size() <= cap) return;
  size_t end = cap;
  while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
  s.resize(end);
}

Side sideOf(const std::string& v) {
  if (v == "top") return Side::Top;
  if (v == "right") return Side::Right;
  if (v == "bottom") return Side::Bottom;
  if (v == "left") return Side::Left;
  return Side::None;
}

uint8_t colorOf(const std::string& v) {
  if (v.size() == 1 && v[0] >= '1' && v[0] <= '6') return static_cast<uint8_t>(v[0] - '0');
  return v.empty() ? 0 : 7;
}

// Byte stream with the position of each byte in the file.
class Stream {
 public:
  explicit Stream(const Reader& read) : read(read) {}
  int peek() {
    if (pos == len && !fill()) return -1;
    return static_cast<unsigned char>(buf[pos]);
  }
  int next() {
    const int c = peek();
    if (c >= 0) {
      ++pos;
      ++offset;
    }
    return c;
  }
  uint32_t tell() const { return offset; }

 private:
  bool fill() {
    len = read(buf, sizeof(buf));
    pos = 0;
    return len > 0;
  }
  const Reader& read;
  char buf[512];
  size_t len = 0;
  size_t pos = 0;
  uint32_t offset = 0;
};

class Parser {
 public:
  explicit Parser(const Reader& read, Canvas& out) : in(read), out(out) {}

  bool run() {
    skipSpace();
    if (in.next() != '{') return false;
    // Top level: "nodes" and "edges" arrays; anything else is skipped.
    while (true) {
      skipSpace();
      int c = in.peek();
      if (c == '}' || c < 0) break;
      if (c == ',') {
        in.next();
        continue;
      }
      std::string key;
      if (!readString(&key, nullptr, nullptr)) return true;
      skipSpace();
      if (in.next() != ':') return true;
      skipSpace();
      if ((key == "nodes" || key == "edges") && in.peek() == '[') {
        readObjects(key == "nodes");
      } else if (!skipValue()) {
        return true;
      }
    }
    finish();
    return true;
  }

  void finish() {
    std::map<std::string, uint16_t> index;
    for (size_t i = 0; i < out.nodes.size(); ++i) index.emplace(out.nodes[i].id, static_cast<uint16_t>(i));
    for (const auto& e : pendingEdges) {
      const auto a = index.find(e.fromId);
      const auto b = index.find(e.toId);
      if (a == index.end() || b == index.end()) continue;
      Edge edge = e.edge;
      edge.from = a->second;
      edge.to = b->second;
      out.edges.push_back(std::move(edge));
    }
    pendingEdges.clear();
    bool first = true;
    for (const auto& n : out.nodes) {
      if (first) {
        out.minX = n.x;
        out.minY = n.y;
        out.maxX = n.x + n.width;
        out.maxY = n.y + n.height;
        first = false;
      }
      out.minX = std::min(out.minX, n.x);
      out.minY = std::min(out.minY, n.y);
      out.maxX = std::max(out.maxX, n.x + n.width);
      out.maxY = std::max(out.maxY, n.y + n.height);
    }
  }

 private:
  struct PendingEdge {
    std::string fromId, toId;
    Edge edge;
  };

  void skipSpace() {
    for (int c = in.peek(); c == ' ' || c == '\n' || c == '\r' || c == '\t'; c = in.peek()) in.next();
  }

  // Reads a JSON string starting at its opening quote. `value` (unescaped, cut at `cap` bytes)
  // and the raw body's offset/length are optional.
  bool readString(std::string* value, uint32_t* rawOffset, uint32_t* rawLength, const size_t cap = MAX_FIELD) {
    if (in.next() != '"') return false;
    if (rawOffset) *rawOffset = in.tell();
    const uint32_t start = in.tell();
    const auto keep = [&] { return value && value->size() < cap + 4; };
    const auto hex4 = [&](uint32_t& cp) {
      char hex[5] = {};
      for (int i = 0; i < 4; ++i) {
        const int h = in.next();
        if (h < 0) return false;
        hex[i] = static_cast<char>(h);
      }
      cp = std::strtoul(hex, nullptr, 16);
      return true;
    };
    int pending = -1;  // an escape letter already read after a lone high surrogate
    while (true) {
      int e = pending;
      pending = -1;
      if (e < 0) {
        const int c = in.next();
        if (c < 0) return false;
        if (c == '"') break;
        if (c != '\\') {
          if (keep()) *value += static_cast<char>(c);
          continue;
        }
        e = in.next();
        if (e < 0) return false;
      }
      if (e == 'u') {
        uint32_t cp = 0;
        if (!hex4(cp)) return false;
        if (cp >= 0xD800 && cp <= 0xDBFF && in.peek() == '\\') {
          in.next();
          const int e2 = in.next();
          if (e2 < 0) return false;
          uint32_t low = 0;
          if (e2 == 'u') {
            if (!hex4(low)) return false;
            if (low >= 0xDC00 && low <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else {
              if (keep()) appendUtf8(*value, 0xFFFD);
              cp = low;
            }
          } else {
            cp = 0xFFFD;
            pending = e2;  // an ordinary escape ("\n", "\"") follows the lone surrogate
          }
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        if (keep()) appendUtf8(*value, cp);
        continue;
      }
      if (!keep()) continue;
      switch (e) {
        case 'n':
          *value += '\n';
          break;
        case 't':
          *value += '\t';
          break;
        case 'r':
        case 'b':
        case 'f':
          break;
        default:
          *value += static_cast<char>(e);
      }
    }
    if (rawLength) *rawLength = in.tell() - 1 - start;
    if (value) capUtf8(*value, cap);
    return true;
  }

  bool readNumber(int32_t& v) {
    std::string s;
    for (int c = in.peek(); c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' || (c >= '0' && c <= '9');
         c = in.peek()) {
      s += static_cast<char>(in.next());
      if (s.size() > 32) return false;
    }
    if (s.empty()) return false;
    const double d = std::strtod(s.c_str(), nullptr);
    v = static_cast<int32_t>(std::max(-1e9, std::min(1e9, d)));
    return true;
  }

  // Skips any value; false on a broken file.
  bool skipValue() {
    skipSpace();
    const int c = in.peek();
    if (c == '"') return readString(nullptr, nullptr, nullptr);
    if (c == '{' || c == '[') {
      int depth = 0;
      do {
        const int d = in.peek();
        if (d < 0) return false;
        if (d == '"') {
          if (!readString(nullptr, nullptr, nullptr)) return false;
          continue;
        }
        in.next();
        if (d == '{' || d == '[') ++depth;
        if (d == '}' || d == ']') --depth;
      } while (depth > 0);
      return true;
    }
    // Number, true, false, null. A stray '}' or ']' is stepped over, so callers always move on.
    if (c == '}' || c == ']' || c == ':') {
      in.next();
      return true;
    }
    while (true) {
      const int d = in.peek();
      if (d < 0 || d == ',' || d == '}' || d == ']' || d == ' ' || d == '\n' || d == '\r' || d == '\t') break;
      in.next();
    }
    return true;
  }

  void readObjects(const bool nodes) {
    in.next();  // '['
    while (true) {
      skipSpace();
      const int c = in.peek();
      if (c < 0) return;
      if (c == ']') {
        in.next();
        return;
      }
      if (c == ',') {
        in.next();
        continue;
      }
      if (c != '{') {
        if (!skipValue()) return;
        continue;
      }
      const bool full = nodes ? out.nodes.size() >= MAX_NODES : pendingEdges.size() >= MAX_EDGES;
      if (full) {
        out.truncated = true;
        if (!skipValue()) return;
        continue;
      }
      if (!(nodes ? readNode() : readEdge())) return;
    }
  }

  // Calls `field(key)` for each member of the object at the cursor; `field` consumes the value.
  template <typename F>
  bool readMembers(F&& field) {
    in.next();  // '{'
    while (true) {
      skipSpace();
      const int c = in.peek();
      if (c < 0) return false;
      if (c == '}') {
        in.next();
        return true;
      }
      if (c == ',') {
        in.next();
        continue;
      }
      std::string key;
      if (!readString(&key, nullptr, nullptr)) return false;
      skipSpace();
      if (in.next() != ':') return false;
      skipSpace();
      if (!field(key)) return false;
    }
  }

  bool readNode() {
    Node n;
    std::string type, file, url, label, text;
    uint32_t textOffset = 0, textLength = 0;
    const bool ok = readMembers([&](const std::string& key) {
      std::string v;
      if (key == "x") return readNumber(n.x);
      if (key == "y") return readNumber(n.y);
      if (key == "width") return readNumber(n.width);
      if (key == "height") return readNumber(n.height);
      if (in.peek() != '"') return skipValue();
      if (key == "text") return readString(&text, &textOffset, &textLength, LABEL_CHARS);
      if (!readString(&v, nullptr, nullptr)) return false;
      if (key == "id") n.id = v;
      if (key == "type") type = v;
      if (key == "file") file = v;
      if (key == "subpath") n.subpath = v.empty() || v[0] != '#' ? v : v.substr(1);
      if (key == "url") url = v;
      if (key == "label") label = v;
      if (key == "color") n.color = colorOf(v);
      return true;
    });
    if (!ok) return false;
    n.width = std::max<int32_t>(n.width, 1);
    n.height = std::max<int32_t>(n.height, 1);
    if (type == "text") {
      n.type = NodeType::Text;
      n.label = text;
      n.textOffset = textOffset;
      n.textLength = textLength;
    } else if (type == "file") {
      n.type = NodeType::File;
      n.label = file;
    } else if (type == "link") {
      n.type = NodeType::Link;
      n.label = url;
    } else if (type == "group") {
      n.type = NodeType::Group;
      n.label = label;
    }
    if (!n.id.empty()) out.nodes.push_back(std::move(n));
    return true;
  }

  bool readEdge() {
    PendingEdge e;
    const bool ok = readMembers([&](const std::string& key) {
      if (in.peek() != '"') return skipValue();
      std::string v;
      if (!readString(&v, nullptr, nullptr)) return false;
      if (key == "fromNode") e.fromId = v;
      if (key == "toNode") e.toId = v;
      if (key == "fromSide") e.edge.fromSide = sideOf(v);
      if (key == "toSide") e.edge.toSide = sideOf(v);
      if (key == "fromEnd") e.edge.fromArrow = v == "arrow";
      if (key == "toEnd") e.edge.toArrow = v == "arrow";
      if (key == "color") e.edge.color = colorOf(v);
      if (key == "label") e.edge.label = v;
      return true;
    });
    if (ok) pendingEdges.push_back(std::move(e));
    return ok;
  }

  Stream in;
  Canvas& out;
  std::vector<PendingEdge> pendingEdges;
};

}  // namespace

bool parse(const Reader& read, Canvas& out) {
  out = Canvas{};
  Parser parser(read, out);
  return parser.run();
}

std::string unescape(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c != '\\' || i + 1 >= raw.size()) {
      out += c;
      continue;
    }
    const char e = raw[++i];
    switch (e) {
      case 'n':
        out += '\n';
        break;
      case 't':
        out += '\t';
        break;
      case 'r':
      case 'b':
      case 'f':
        break;
      case 'u': {
        if (i + 4 >= raw.size()) return out;
        uint32_t cp = std::strtoul(raw.substr(i + 1, 4).c_str(), nullptr, 16);
        i += 4;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < raw.size() && raw[i + 1] == '\\' && raw[i + 2] == 'u') {
          const uint32_t low = std::strtoul(raw.substr(i + 3, 4).c_str(), nullptr, 16);
          if (low >= 0xDC00 && low <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            i += 6;
          }
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;  // a lone surrogate is not UTF-8
        appendUtf8(out, cp);
        break;
      }
      default:
        out += e;
    }
  }
  return out;
}

}  // namespace canvas
