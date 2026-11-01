#include "Excalidraw.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "LzString.h"

namespace excalidraw {
namespace {

constexpr size_t MAX_FIELD = 256;    // ids, type names, colours
constexpr size_t RAW_POINTS = 4096;  // points read per element before thinning
constexpr size_t MAX_ARRAY_SKIP = 1 << 20;

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

int16_t toI16(const float v) { return static_cast<int16_t>(std::lround(std::max(-32767.0f, std::min(32767.0f, v)))); }

// Byte stream over the reader.
class Stream {
 public:
  explicit Stream(const Reader& read) : read(read) {}
  int peek() {
    if (pos == len && !fill()) return -1;
    return static_cast<unsigned char>(buf[pos]);
  }
  int next() {
    const int c = peek();
    if (c >= 0) ++pos;
    return c;
  }

 private:
  bool fill() {
    len = read(buf, sizeof(buf));
    pos = 0;
    return len > 0;
  }
  const Reader& read;
  char buf[512] = {};
  size_t len = 0;
  size_t pos = 0;
};

class Parser {
 public:
  Parser(const Reader& read, Drawing& out) : in(read), out(out) {}

  // Parses the stream. A ```compressed-json block is not parsed here: its base64 text is returned in
  // `compressed` with Error::BadCompressed, and parse() decodes it and runs a second Parser.
  Error run(std::string& compressed) {
    skipSpace();
    if (in.peek() != '{') {
      const Error e = findFence();
      if (e == Error::BadCompressed) return readCompressedBlock(compressed);
      if (e != Error::Ok) return e;
      skipSpace();
      if (in.peek() != '{') return Error::NotJson;
    }
    return parseObject();
  }

 private:
  Error parseObject() {
    in.next();  // '{'
    // Top level: the "elements" array; "files", "appState" and the rest are skipped.
    while (true) {
      skipSpace();
      const int c = in.peek();
      if (c == '}' || c < 0) break;
      if (c == ',') {
        in.next();
        continue;
      }
      std::string key;
      if (!readString(&key)) break;
      skipSpace();
      if (in.next() != ':') break;
      skipSpace();
      if (key == "elements" && in.peek() == '[') {
        readElements();
      } else if (!skipValue()) {
        break;
      }
    }
    finish();
    return Error::Ok;
  }

  static bool isSpace(const int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

  // After a ```compressed-json fence line: collects the block's base64 text (the plugin writes it in
  // lines of 256 characters; whitespace is dropped) up to the closing fence or the end of the file.
  Error readCompressedBlock(std::string& compressed) {
    compressed.clear();
    bool lineStart = true;
    while (true) {
      const int c = in.next();
      if (c < 0) break;
      if (lineStart && c == '`') break;  // the closing fence
      lineStart = c == '\n';
      if (isSpace(c)) continue;
      if (compressed.size() >= lzstring::MAX_INPUT_CHARS) return Error::BadCompressed;
      compressed += static_cast<char>(c);
    }
    return compressed.empty() ? Error::NotJson : Error::BadCompressed;
  }

  void skipSpace() {
    for (int c = in.peek(); isSpace(c); c = in.peek()) in.next();
  }

  // Markdown wrapper: moves past the first "```json" fence line (Ok) or "```compressed-json" fence
  // line (BadCompressed, meaning "compressed block follows").
  Error findFence() {
    bool lineStart = true;
    while (true) {
      const int c = in.next();
      if (c < 0) return Error::NotJson;
      if (c == '\n') {
        lineStart = true;
        continue;
      }
      if (!lineStart || c != '`') {
        lineStart = isSpace(c) && lineStart;
        continue;
      }
      if (in.next() != '`' || in.next() != '`') {
        lineStart = false;
        continue;
      }
      std::string info;
      for (int d = in.peek(); d >= 0 && d != '\n' && info.size() < 32; d = in.peek())
        info += static_cast<char>(in.next());
      while (!info.empty() && isSpace(info.back())) info.pop_back();
      if (info == "json") return Error::Ok;
      if (info == "compressed-json") return Error::BadCompressed;
      lineStart = false;
    }
  }

  // Reads a JSON string starting at its opening quote; `value` (unescaped, cut at `cap` bytes) is
  // optional.
  bool readString(std::string* value, const size_t cap = MAX_FIELD) {
    if (in.next() != '"') return false;
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
          if (e2 == 'u') {
            uint32_t low = 0;
            if (!hex4(low)) return false;
            if (low >= 0xDC00 && low <= 0xDFFF) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            } else {
              if (keep()) appendUtf8(*value, 0xFFFD);
              cp = low;
            }
          } else {
            cp = 0xFFFD;
            pending = e2;
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
    if (value) capUtf8(*value, cap);
    return true;
  }

  bool readNumber(float& v) {
    std::string s;
    for (int c = in.peek(); c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E' || (c >= '0' && c <= '9');
         c = in.peek()) {
      s += static_cast<char>(in.next());
      if (s.size() > 32) return false;
    }
    if (s.empty()) return false;
    const double d = std::strtod(s.c_str(), nullptr);
    v = static_cast<float>(std::isfinite(d) ? std::max(-1e7, std::min(1e7, d)) : 0.0);
    return true;
  }

  // Skips any value; false on a broken file.
  bool skipValue() {
    skipSpace();
    const int c = in.peek();
    if (c == '"') return readString(nullptr);
    if (c == '{' || c == '[') {
      int depth = 0;
      do {
        const int d = in.peek();
        if (d < 0) return false;
        if (d == '"') {
          if (!readString(nullptr)) return false;
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
      if (d < 0 || d == ',' || d == '}' || d == ']' || isSpace(d)) break;
      in.next();
    }
    return true;
  }

  // Reads `true`/`false`/`null` or a string into `isSet` (null and false clear it).
  bool readFlag(bool& isSet) {
    const int c = in.peek();
    if (c == '"') {
      std::string v;
      if (!readString(&v)) return false;
      isSet = !v.empty() && v != "none";
      return true;
    }
    std::string word;
    while (true) {
      const int d = in.peek();
      if (d < 0 || d == ',' || d == '}' || d == ']' || isSpace(d) || word.size() > 8) break;
      word += static_cast<char>(in.next());
    }
    isSet = word == "true";
    return true;
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
      if (!readString(&key)) return false;
      skipSpace();
      if (in.next() != ':') return false;
      skipSpace();
      if (!field(key)) return false;
    }
  }

  void readElements() {
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
      if (out.shapes.size() >= MAX_SHAPES) {
        out.truncated = true;
        if (!skipValue()) return;
        continue;
      }
      if (!readElement()) return;
    }
  }

  // "points": [[x,y], ...] into `raw`, at most RAW_POINTS kept (the last one always).
  bool readPoints(std::vector<Point>& raw) {
    if (in.peek() != '[') return skipValue();
    in.next();
    size_t seen = 0;
    while (true) {
      skipSpace();
      const int c = in.peek();
      if (c < 0) return false;
      if (c == ']') {
        in.next();
        return true;
      }
      if (c == ',') {
        in.next();
        continue;
      }
      if (c != '[') {
        if (!skipValue()) return false;
        continue;
      }
      in.next();
      float x = 0, y = 0;
      skipSpace();
      const bool okX = readNumber(x);
      skipSpace();
      if (in.peek() == ',') in.next();
      skipSpace();
      const bool okY = readNumber(y);
      // Step over the rest of the pair (pressure or extra values).
      while (true) {
        skipSpace();
        const int d = in.peek();
        if (d < 0) return false;
        in.next();
        if (d == ']') break;
        if (d != ',' && !skipValue()) return false;
      }
      if (!okX || !okY) continue;
      const Point p{toI16(x), toI16(y)};
      if (++seen > MAX_ARRAY_SKIP) return false;
      if (raw.size() < RAW_POINTS) {
        raw.push_back(p);
      } else {
        raw.back() = p;
        out.truncated = true;
      }
    }
  }

  // "groupIds": ["a", ...]: the first id, mapped to an index.
  bool readGroupIds(int16_t& groupId) {
    if (in.peek() != '[') return skipValue();
    in.next();
    bool first = true;
    while (true) {
      skipSpace();
      const int c = in.peek();
      if (c < 0) return false;
      if (c == ']') {
        in.next();
        return true;
      }
      if (c == ',') {
        in.next();
        continue;
      }
      if (c != '"') {
        if (!skipValue()) return false;
        continue;
      }
      std::string id;
      if (!readString(&id)) return false;
      if (!first || id.empty()) continue;
      first = false;
      const auto it = std::find(groups.begin(), groups.end(), id);
      if (it != groups.end()) {
        groupId = static_cast<int16_t>(it - groups.begin());
      } else if (groups.size() < MAX_GROUPS) {
        groupId = static_cast<int16_t>(groups.size());
        groups.push_back(id);
      } else {
        out.truncated = true;
      }
    }
  }

  // Keeps at most `cap` points: every n-th plus the last.
  static void thin(std::vector<Point>& raw, const size_t cap) {
    if (raw.size() <= cap) return;
    const size_t step = (raw.size() + cap - 2) / (cap - 1);  // so that kept + last <= cap
    std::vector<Point> kept;
    kept.reserve(cap);
    for (size_t i = 0; i < raw.size() - 1 && kept.size() < cap - 1; i += step) kept.push_back(raw[i]);
    kept.push_back(raw.back());
    raw.swap(kept);
  }

  bool readElement() {
    Shape s;
    std::string type, text;
    bool deleted = false, fill = false, startArrow = false, endArrow = false;
    bool hasStart = false, hasEnd = false;
    float strokeWidth = 1;
    std::vector<Point> raw;
    const bool ok = readMembers([&](const std::string& key) {
      if (key == "x") return readNumber(s.x);
      if (key == "y") return readNumber(s.y);
      if (key == "width") return readNumber(s.w);
      if (key == "height") return readNumber(s.h);
      if (key == "angle") return readNumber(s.angle);
      if (key == "strokeWidth") return readNumber(strokeWidth);
      if (key == "isDeleted") return readFlag(deleted);
      if (key == "points") return readPoints(raw);
      if (key == "groupIds") return readGroupIds(s.groupId);
      if (key == "startArrowhead") {
        hasStart = true;
        return readFlag(startArrow);
      }
      if (key == "endArrowhead") {
        hasEnd = true;
        return readFlag(endArrow);
      }
      if (in.peek() != '"') return skipValue();
      if (key == "text") return readString(&text, TEXT_CHARS);
      std::string v;
      if (!readString(&v)) return false;
      if (key == "type") type = v;
      if (key == "strokeStyle") s.strokeStyle = v == "dashed" ? 1 : v == "dotted" ? 2 : 0;
      if (key == "backgroundColor") fill = !v.empty() && v != "transparent";
      return true;
    });
    if (!ok) return false;
    if (type == "rectangle") {
      s.kind = Kind::Rect;
    } else if (type == "ellipse") {
      s.kind = Kind::Ellipse;
    } else if (type == "diamond") {
      s.kind = Kind::Diamond;
    } else if (type == "text") {
      s.kind = Kind::Text;
    } else if (type == "line") {
      s.kind = Kind::Line;
    } else if (type == "arrow") {
      s.kind = Kind::Arrow;
    } else if (type == "freedraw") {
      s.kind = Kind::FreeDraw;
    } else {
      ++out.skipped;  // image, frame, embeddable, iframe, magicframe, unknown
      return true;
    }
    if (deleted) {
      ++out.skipped;
      return true;
    }
    s.fill = fill ? 1 : 0;
    s.strokeWidth = static_cast<uint8_t>(std::max(1.0f, std::min(8.0f, strokeWidth)));
    if (s.kind == Kind::Arrow) {
      s.startArrow = hasStart && startArrow;
      s.endArrow = hasEnd ? endArrow : true;
    }
    if (s.kind == Kind::Line || s.kind == Kind::Arrow || s.kind == Kind::FreeDraw) {
      thin(raw, s.kind == Kind::FreeDraw ? FREEDRAW_POINTS : LINE_POINTS);
      if (out.points.size() + raw.size() <= MAX_POINTS) {
        s.pointStart = static_cast<uint16_t>(out.points.size());
        s.pointCount = static_cast<uint16_t>(raw.size());
        out.points.insert(out.points.end(), raw.begin(), raw.end());
      } else {
        out.truncated = true;
      }
    }
    if (s.kind == Kind::Text && !text.empty()) {
      if (out.text.size() + text.size() <= MAX_TEXT) {
        s.textOffset = static_cast<uint16_t>(out.text.size());
        s.textLength = static_cast<uint8_t>(text.size());
        out.text += text;
      } else {
        out.truncated = true;
      }
    }
    out.shapes.push_back(s);
    return true;
  }

  void finish() {
    bool first = true;
    const auto grow = [&](const float x, const float y) {
      if (first) {
        out.minX = out.maxX = x;
        out.minY = out.maxY = y;
        first = false;
        return;
      }
      out.minX = std::min(out.minX, x);
      out.minY = std::min(out.minY, y);
      out.maxX = std::max(out.maxX, x);
      out.maxY = std::max(out.maxY, y);
    };
    for (const auto& s : out.shapes) {
      grow(s.x, s.y);
      if (s.pointCount == 0) grow(s.x + s.w, s.y + s.h);
      for (size_t i = s.pointStart; i < static_cast<size_t>(s.pointStart) + s.pointCount; ++i) {
        grow(s.x + out.points[i].x, s.y + out.points[i].y);
      }
    }
  }

  Stream in;
  Drawing& out;
  std::vector<std::string> groups;
};

}  // namespace

Error parse(const Reader& read, Drawing& out) {
  out = Drawing{};
  std::string compressed;
  const Error first = Parser(read, out).run(compressed);
  if (first != Error::BadCompressed || compressed.empty()) return first;

  std::string json;
  if (!lzstring::decompressFromBase64(compressed, json)) return Error::BadCompressed;
  compressed.clear();
  compressed.shrink_to_fit();
  size_t pos = 0;
  const Reader fromJson = [&json, &pos](char* buf, const size_t cap) {
    const size_t n = std::min(cap, json.size() - pos);
    std::copy_n(json.data() + pos, n, buf);
    pos += n;
    return n;
  };
  std::string none;
  const Error second = Parser(fromJson, out).run(none);
  // The decoded text is plain JSON; a fence or another compressed block inside it is damage.
  return second == Error::BadCompressed ? Error::NotJson : second;
}

}  // namespace excalidraw
