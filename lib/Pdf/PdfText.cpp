#include "PdfText.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "PdfCrypt.h"

namespace pdf {

std::string pageSeparator(std::string& previous, const std::string& next) {
  if (previous.empty() || next.empty()) return "";
  // The last and first code points as bytes: a letter is ASCII or any multi-byte character that isn't
  // typographic punctuation (U+2000-U+206F, U+00B7, U+037E), good enough to tell "mid-sentence".
  const auto lastCodePoint = [](const std::string& s) {
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<uint8_t>(s[i]) & 0xC0) == 0x80) --i;
    uint32_t cp = static_cast<uint8_t>(s[i]);
    size_t extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
    if (extra) cp &= 0x3F >> extra;
    for (size_t k = 1; k <= extra && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<uint8_t>(s[i + k]) & 0x3F);
    return cp;
  };
  const auto isLetter = [](uint32_t cp) {
    if (cp < 0x80) return std::isalpha(static_cast<int>(cp)) != 0;
    if (cp == 0xB7 || cp == 0x37E || (cp >= 0x2000 && cp <= 0x206F) || cp == 0xAB || cp == 0xBB) return false;
    return cp >= 0xC0;
  };
  const auto isLower = [](uint32_t cp) {
    if (cp < 0x80) return std::islower(static_cast<int>(cp)) != 0;
    // Greek lower case and the Latin-1 and Latin Extended lower-case blocks, enough for the common cases.
    return (cp >= 0x3AC && cp <= 0x3CE) || (cp >= 0xDF && cp <= 0xFF && cp != 0xF7) || (cp >= 0x430 && cp <= 0x45F);
  };
  const uint32_t last = lastCodePoint(previous);
  // The first code point of `next`.
  uint32_t first = static_cast<uint8_t>(next[0]);
  if (first >= 0xC0 && next.size() >= 2) {
    const size_t extra = first >= 0xF0 ? 3 : first >= 0xE0 ? 2 : 1;
    first &= 0x3F >> extra;
    for (size_t k = 1; k <= extra && k < next.size(); ++k)
      first = (first << 6) | (static_cast<uint8_t>(next[k]) & 0x3F);
  }
  if (last == '-' && previous.size() >= 2 && isLetter(lastCodePoint(previous.substr(0, previous.size() - 1))) &&
      isLower(first)) {
    previous.pop_back();
    return "";
  }
  if ((isLetter(last) || last == ',') && isLower(first)) return " ";
  return "\n\n";
}

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
  } else if (cp <= 0x10FFFF) {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

namespace {

// ---------------------------------------------------------------------------------------------
// Objects

struct Obj {
  enum Type : uint8_t { Null, Bool, Int, Real, Name, Str, Arr, Dict, Ref, Stream } type = Null;
  int64_t i = 0;        // Int, Bool, Ref number
  double r = 0;         // Real
  uint32_t gen = 0;     // Ref: the generation it names; a loaded object: its own
  uint32_t objNum = 0;  // a loaded object: its number (for decrypting)
  std::string s;        // Name, Str
  std::vector<Obj> a;
  std::map<std::string, Obj> d;  // Dict and Stream
  uint64_t streamPos = 0;        // Stream: file offset of the data
  int64_t streamLen = -1;        // Stream: /Length when it was a direct number, else -1 until resolved

  bool isNum() const { return type == Int || type == Real; }
  double num() const { return type == Int ? static_cast<double>(i) : r; }
  bool isDict() const { return type == Dict || type == Stream; }
  const Obj* get(const std::string& key) const {
    if (!isDict()) return nullptr;
    const auto it = d.find(key);
    return it == d.end() ? nullptr : &it->second;
  }
};

bool isWhite(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; }
bool isDelim(char c) {
  return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' ||
         c == '%';
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Tokenizer and object parser over a byte buffer. `truncated` is set when the buffer ended inside an
// object, so the caller can read more and try again.
class Lexer {
 public:
  explicit Lexer(std::string_view buf, size_t pos = 0) : buf(buf), pos(pos) {}
  size_t position() const { return pos; }
  void seek(size_t p) { pos = p; }
  bool truncated = false;

  void skipSpace() {
    while (pos < buf.size()) {
      if (isWhite(buf[pos])) {
        ++pos;
      } else if (buf[pos] == '%') {
        while (pos < buf.size() && buf[pos] != '\n' && buf[pos] != '\r') ++pos;
      } else {
        break;
      }
    }
  }
  bool atEnd() {
    skipSpace();
    return pos >= buf.size();
  }

  // A bare word (keyword or operator): letters, digits and the rest up to a delimiter.
  std::string_view word() {
    skipSpace();
    const size_t start = pos;
    while (pos < buf.size() && !isWhite(buf[pos]) && !isDelim(buf[pos])) ++pos;
    return buf.substr(start, pos - start);
  }

  bool peekIs(std::string_view w) {
    skipSpace();
    return buf.substr(pos, w.size()) == w;
  }

  // Parses one object. Names, numbers, strings, arrays, dictionaries and "n g R" references. Returns false
  // at a keyword (operator) or on error, leaving `kw` with the keyword when it was one.
  bool object(Obj& out, std::string_view* kw = nullptr, int depth = 0) {
    if (depth > 12) return false;  // real files nest 6 deep at most; the stack is 24 KB and an Obj 152 bytes
    skipSpace();
    if (pos >= buf.size()) {
      truncated = true;
      return false;
    }
    const char c = buf[pos];
    if (c == '/') {
      ++pos;
      out = Obj();
      out.type = Obj::Name;
      while (pos < buf.size() && !isWhite(buf[pos]) && !isDelim(buf[pos])) {
        if (buf[pos] == '#' && pos + 2 < buf.size() && hexVal(buf[pos + 1]) >= 0 && hexVal(buf[pos + 2]) >= 0) {
          out.s += static_cast<char>(hexVal(buf[pos + 1]) * 16 + hexVal(buf[pos + 2]));
          pos += 3;
        } else {
          out.s += buf[pos++];
        }
      }
      return true;
    }
    if (c == '(') return literalString(out);
    if (c == '<' && pos + 1 < buf.size() && buf[pos + 1] == '<') return dict(out, depth);
    if (c == '<') return hexString(out);
    if (c == '[') {
      ++pos;
      out = Obj();
      out.type = Obj::Arr;
      while (true) {
        skipSpace();
        if (pos >= buf.size()) {
          truncated = true;
          return false;
        }
        if (buf[pos] == ']') {
          ++pos;
          return true;
        }
        Obj item;
        std::string_view word;
        if (!object(item, &word, depth + 1)) {
          if (word == "true" || word == "false" || word == "null") continue;
          if (truncated) return false;
          if (!word.empty()) continue;  // a stray keyword inside an array: skip it
          return false;
        }
        out.a.push_back(std::move(item));
        if (out.a.size() > 30000) return false;  // an Obj is big; a longer array would not fit in memory
      }
    }
    if (c == ']' || c == '>' || c == ')' || c == '{' || c == '}') {
      ++pos;
      return false;
    }
    // number, boolean, null, reference or keyword
    const std::string_view w = word();
    if (w.empty()) return false;
    const bool numeric = (w[0] >= '0' && w[0] <= '9') || w[0] == '+' || w[0] == '-' || w[0] == '.';
    if (numeric) {
      const std::string text(w);
      char* end = nullptr;
      const double v = std::strtod(text.c_str(), &end);
      if (end == text.c_str()) {
        if (kw) *kw = w;
        return false;
      }
      const bool isInt = text.find_first_of(".eE") == std::string::npos;
      out = Obj();
      if (isInt) {
        out.type = Obj::Int;
        out.i = static_cast<int64_t>(v);
        // "n g R"?
        if (out.i >= 0) {
          const size_t save = pos;
          const std::string_view w2 = word();
          if (!w2.empty() && w2.find_first_not_of("0123456789") == std::string_view::npos && w2.size() < 6) {
            const std::string_view w3 = word();
            if (w3 == "R") {
              out.type = Obj::Ref;
              out.gen = static_cast<uint32_t>(std::strtoul(std::string(w2).c_str(), nullptr, 10));
              return true;
            }
          }
          pos = save;
        }
      } else {
        out.type = Obj::Real;
        out.r = v;
      }
      return true;
    }
    if (w == "true" || w == "false") {
      out = Obj();
      out.type = Obj::Bool;
      out.i = w == "true";
      return true;
    }
    if (w == "null") {
      out = Obj();
      return true;
    }
    if (kw) *kw = w;
    return false;
  }

 private:
  bool dict(Obj& out, int depth) {
    pos += 2;
    out = Obj();
    out.type = Obj::Dict;
    while (true) {
      skipSpace();
      if (pos >= buf.size()) {
        truncated = true;
        return false;
      }
      if (buf.substr(pos, 2) == ">>") {
        pos += 2;
        return true;
      }
      Obj key;
      if (buf[pos] != '/' || !object(key, nullptr, depth + 1)) return false;
      Obj value;
      std::string_view word;
      if (!object(value, &word, depth + 1)) {
        if (truncated) return false;
        if (word == "endobj" || word == "stream") return false;
        continue;  // a key without a value
      }
      out.d[key.s] = std::move(value);
    }
  }

  bool literalString(Obj& out) {
    ++pos;
    out = Obj();
    out.type = Obj::Str;
    int nesting = 1;
    while (pos < buf.size()) {
      const char c = buf[pos++];
      if (c == '\\') {
        if (pos >= buf.size()) break;
        const char e = buf[pos++];
        switch (e) {
          case 'n':
            out.s += '\n';
            break;
          case 'r':
            out.s += '\r';
            break;
          case 't':
            out.s += '\t';
            break;
          case 'b':
            out.s += '\b';
            break;
          case 'f':
            out.s += '\f';
            break;
          case '\r':
            if (pos < buf.size() && buf[pos] == '\n') ++pos;
            break;
          case '\n':
            break;
          default:
            if (e >= '0' && e <= '7') {
              int v = e - '0';
              for (int k = 0; k < 2 && pos < buf.size() && buf[pos] >= '0' && buf[pos] <= '7'; ++k) {
                v = v * 8 + (buf[pos++] - '0');
              }
              out.s += static_cast<char>(v & 0xFF);
            } else {
              out.s += e;
            }
        }
      } else if (c == '(') {
        ++nesting;
        out.s += c;
      } else if (c == ')') {
        if (--nesting == 0) return true;
        out.s += c;
      } else {
        out.s += c;
      }
    }
    truncated = true;
    return false;
  }

  bool hexString(Obj& out) {
    ++pos;
    out = Obj();
    out.type = Obj::Str;
    int high = -1;
    while (pos < buf.size()) {
      const char c = buf[pos++];
      if (c == '>') {
        if (high >= 0) out.s += static_cast<char>(high * 16);
        return true;
      }
      const int v = hexVal(c);
      if (v < 0) continue;
      if (high < 0) {
        high = v;
      } else {
        out.s += static_cast<char>(high * 16 + v);
        high = -1;
      }
    }
    truncated = true;
    return false;
  }

  std::string_view buf;
  size_t pos;
};

// ---------------------------------------------------------------------------------------------
// The file: cross-reference table, objects, streams

struct XEntry {
  enum Kind : uint8_t { Free, InFile, InStream } kind = Free;
  uint64_t offset = 0;  // InFile
  uint32_t stream = 0;  // InStream: the object stream's number
  uint32_t index = 0;   // InStream: position inside it
};

class Doc {
 public:
  Doc(const Source& src, const Inflate& inflate, const Limits& limits) : src(src), inflate(inflate), limits(limits) {}

  bool open(Result& result) {
    if (src.size < 20) return false;
    std::string head;
    if (!readRange(0, std::min<uint64_t>(1024, src.size), head) || head.find("%PDF-") == std::string::npos) {
      return false;
    }
    uint64_t xref = 0;
    if (!findStartXref(xref)) {
      result.xrefFailed = true;
      return false;
    }
    std::set<uint64_t> seen;
    bool first = true;
    while (xref != 0 && !seen.count(xref) && seen.size() < 200) {
      seen.insert(xref);
      Obj trailer;
      if (!readXref(xref, trailer)) {
        result.xrefFailed = first;
        if (first) return false;
        break;
      }
      if (first) {
        root = trailerRoot(trailer);
        first = false;
        firstTrailer = trailer;
      }
      const Obj* prev = trailer.get("Prev");
      xref = prev && prev->isNum() ? static_cast<uint64_t>(prev->num()) : 0;
    }
    if (root == 0) {
      result.xrefFailed = true;
      return false;
    }
    if (firstTrailer.get("Encrypt") && !setupEncryption(firstTrailer)) {
      result.encrypted = true;
      return false;
    }
    return true;
  }

  // Loads object `num`. Streams keep their data position; the data is read with streamData().
  bool load(uint32_t num, Obj& out, int depth = 0) {
    if (depth > 8) return false;
    const auto cached = cache.find(num);
    if (cached != cache.end()) {
      out = cached->second;
      return true;
    }
    const auto it = xref.find(num);
    if (it == xref.end()) return false;
    bool ok = false;
    if (it->second.kind == XEntry::InFile) {
      ok = loadAt(it->second.offset, num, out, depth);
    } else if (it->second.kind == XEntry::InStream) {
      ok = loadFromObjStm(it->second.stream, it->second.index, num, out, depth);
    }
    if (ok && out.type != Obj::Stream) {
      if (cache.size() > 600) cache.clear();
      cache[num] = out;
    }
    return ok;
  }

  // Follows references until a direct object (or gives up and returns Null).
  Obj resolve(const Obj& o) {
    Obj cur = o;
    for (int hops = 0; cur.type == Obj::Ref && hops < 8; ++hops) {
      Obj next;
      if (!load(static_cast<uint32_t>(cur.i), next)) return Obj();
      cur = std::move(next);
    }
    return cur.type == Obj::Ref ? Obj() : cur;
  }

  Obj resolveKey(const Obj& dict, const std::string& key) {
    const Obj* v = dict.get(key);
    return v ? resolve(*v) : Obj();
  }

  // The decoded bytes of a stream object. False for filters this reader doesn't do.
  bool streamData(const Obj& stream, std::string& out) {
    out.clear();
    if (stream.type != Obj::Stream) return false;
    int64_t len = stream.streamLen;
    if (const Obj* l = stream.get("Length")) {
      const Obj v = resolve(*l);
      if (v.type == Obj::Int) len = v.i;
    }
    std::string raw;
    if (len < 0 || static_cast<uint64_t>(len) > limits.maxStreamBytes || stream.streamPos > src.size ||
        static_cast<uint64_t>(len) > src.size - stream.streamPos ||
        !readRange(stream.streamPos, static_cast<size_t>(len), raw)) {
      // A wrong /Length: read up to "endstream".
      if (!scanToEndstream(stream.streamPos, raw)) return false;
    } else {
      // A /Length that stops short of "endstream" is also seen: trust it only when "endstream" follows.
      std::string after;
      const uint64_t end = stream.streamPos + static_cast<uint64_t>(len);
      if (readRange(end, static_cast<size_t>(std::min<uint64_t>(24, src.size - end)), after)) {
        const size_t first = after.find_first_not_of(" \r\n\t");
        if (first == std::string::npos || after.compare(first, 9, "endstream") != 0) {
          if (!scanToEndstream(stream.streamPos, raw)) return false;
        }
      }
    }
    const bool xrefStream = stream.d.count("Type") && stream.d.at("Type").s == "XRef";
    if (encrypted && !xrefStream && stream.objNum != encryptNum && !decryptBytes(stream.objNum, stream.gen, raw)) {
      return false;
    }
    std::vector<std::string> filters;
    std::vector<Obj> parms;
    collectFilters(stream, filters, parms);
    std::string data = std::move(raw);
    for (size_t f = 0; f < filters.size(); ++f) {
      if (filters[f] == "ASCII85Decode" || filters[f] == "A85") {
        std::string decoded;
        if (!ascii85(data, decoded)) return false;
        data = std::move(decoded);
        continue;
      }
      if (filters[f] == "ASCIIHexDecode" || filters[f] == "AHx") {
        std::string decoded;
        int high = -1;
        for (const char c : data) {
          if (c == '>') break;
          const int v = hexVal(c);
          if (v < 0) continue;
          if (high < 0) {
            high = v;
          } else {
            decoded += static_cast<char>(high * 16 + v);
            high = -1;
          }
        }
        if (high >= 0) decoded += static_cast<char>(high * 16);
        data = std::move(decoded);
        continue;
      }
      if (filters[f] != "FlateDecode" && filters[f] != "Fl") return false;
      std::string inflated;
      if (!inflate(reinterpret_cast<const uint8_t*>(data.data()), data.size(), inflated, limits.maxStreamBytes)) {
        return false;
      }
      data = std::move(inflated);
      if (f < parms.size() && parms[f].isDict()) {
        const Obj p = resolveKey(parms[f], "Predictor");
        if (p.isNum() && p.num() >= 10) {
          const Obj cols = resolveKey(parms[f], "Columns");
          const Obj colors = resolveKey(parms[f], "Colors");
          const Obj bpc = resolveKey(parms[f], "BitsPerComponent");
          const int columns = cols.isNum() ? static_cast<int>(std::clamp(cols.num(), 1.0, 1048576.0)) : 1;
          const int pixelBytes = std::max(1, (colors.isNum() ? static_cast<int>(colors.num()) : 1) *
                                                 (bpc.isNum() ? static_cast<int>(bpc.num()) : 8) / 8);
          if (!pngPredictor(data, columns * pixelBytes, pixelBytes)) return false;
        }
      }
    }
    out = std::move(data);
    return true;
  }

  // The standard security handler, revisions 2 to 4 (RC4 and AES-128), opened with the empty user password.
  // Files that need a password, or use AES-256, are not read.
  bool setupEncryption(const Obj& trailer) {
    const Obj* ref = trailer.get("Encrypt");
    if (!ref) return true;
    if (ref->type == Obj::Ref) encryptNum = static_cast<uint32_t>(ref->i);
    const Obj enc = resolve(*ref);
    if (!enc.isDict() || resolveKey(enc, "Filter").s != "Standard") return false;
    const Obj v = resolveKey(enc, "V");
    const Obj r = resolveKey(enc, "R");
    if (!v.isNum() || !r.isNum()) return false;
    const int version = static_cast<int>(v.num());
    const int revision = static_cast<int>(r.num());
    if (version < 1 || version > 4 || revision < 2 || revision > 4) return false;
    int keyBytes = 5;
    bool useAes = false;
    if (version >= 2) {
      const Obj length = resolveKey(enc, "Length");
      keyBytes = length.isNum() ? static_cast<int>(length.num()) / 8 : 5;
    }
    if (version == 4) {
      const Obj cf = resolveKey(enc, "CF");
      const Obj stmF = resolveKey(enc, "StmF");
      keyBytes = 16;
      if (stmF.type == Obj::Name && stmF.s != "Identity") {
        const Obj filter = cf.isDict() ? resolveKey(cf, stmF.s) : Obj();
        const std::string method = filter.isDict() ? resolveKey(filter, "CFM").s : std::string();
        if (method == "AESV2") {
          useAes = true;
        } else if (method != "V2") {
          return false;
        }
      } else {
        return false;  // Identity: no encryption on streams; treat it as unsupported rather than guess
      }
    }
    if (keyBytes < 5 || keyBytes > 16) return false;
    const Obj o = resolveKey(enc, "O");
    const Obj u = resolveKey(enc, "U");
    const Obj p = resolveKey(enc, "P");
    if (o.type != Obj::Str || u.type != Obj::Str || o.s.size() < 32 || u.s.size() < 32 || !p.isNum()) return false;
    const Obj meta = resolveKey(enc, "EncryptMetadata");
    const bool encryptMetadata = !(meta.type == Obj::Bool && meta.i == 0);
    std::string id;
    const Obj* idArray = trailer.get("ID");
    if (idArray) {
      const Obj ids = resolve(*idArray);
      if (ids.type == Obj::Arr && !ids.a.empty()) id = resolve(ids.a[0]).s;
    }
    static const uint8_t pad[32] = {0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E,
                                    0x56, 0xFF, 0xFA, 0x01, 0x08, 0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68,
                                    0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A};
    const std::string padding(reinterpret_cast<const char*>(pad), 32);
    std::string material = padding + o.s.substr(0, 32);
    const uint32_t flags = static_cast<uint32_t>(static_cast<int64_t>(p.num()));
    for (int k = 0; k < 4; ++k) material += static_cast<char>((flags >> (8 * k)) & 0xFF);
    material += id;
    if (revision >= 4 && !encryptMetadata) material.append(4, '\xFF');
    std::string key = crypt::md5(material);
    if (revision >= 3) {
      for (int round = 0; round < 50; ++round) key = crypt::md5(key.substr(0, static_cast<size_t>(keyBytes)));
    }
    key.resize(static_cast<size_t>(revision == 2 ? 5 : keyBytes));
    // The empty password must be the right one: check the key against /U.
    std::string check;
    if (revision == 2) {
      check = padding;
      crypt::rc4(reinterpret_cast<const uint8_t*>(key.data()), key.size(), reinterpret_cast<uint8_t*>(check.data()),
                 32);
      if (check != u.s.substr(0, 32)) return false;
    } else {
      check = crypt::md5(padding + id);
      crypt::rc4(reinterpret_cast<const uint8_t*>(key.data()), key.size(), reinterpret_cast<uint8_t*>(check.data()),
                 16);
      for (int round = 1; round <= 19; ++round) {
        std::string roundKey = key;
        for (char& c : roundKey) c = static_cast<char>(c ^ round);
        crypt::rc4(reinterpret_cast<const uint8_t*>(roundKey.data()), roundKey.size(),
                   reinterpret_cast<uint8_t*>(check.data()), 16);
      }
      if (check.substr(0, 16) != u.s.substr(0, 16)) return false;
    }
    fileKey = key;
    aes = useAes;
    encrypted = true;
    cache.clear();  // objects read before the key was known
    return true;
  }

  // Decrypts the strings and stream data of object `num` in place. False when AES data is malformed.
  bool decryptBytes(uint32_t num, uint32_t gen, std::string& data) const {
    if (!encrypted) return true;
    std::string key = fileKey;
    for (int k = 0; k < 3; ++k) key += static_cast<char>((num >> (8 * k)) & 0xFF);
    for (int k = 0; k < 2; ++k) key += static_cast<char>((gen >> (8 * k)) & 0xFF);
    if (aes) key += "sAlT";
    const std::string hash = crypt::md5(key);
    const size_t keyLen = std::min<size_t>(fileKey.size() + 5, 16);
    if (aes) {
      std::string plain;
      if (!crypt::aes128CbcDecrypt(reinterpret_cast<const uint8_t*>(hash.data()),
                                   reinterpret_cast<const uint8_t*>(data.data()), data.size(), plain)) {
        return false;
      }
      data = std::move(plain);
      return true;
    }
    crypt::rc4(reinterpret_cast<const uint8_t*>(hash.data()), keyLen, reinterpret_cast<uint8_t*>(data.data()),
               data.size());
    return true;
  }

  void decryptStrings(Obj& o, uint32_t num, uint32_t gen) const {
    if (o.type == Obj::Str) {
      std::string copy = o.s;
      if (decryptBytes(num, gen, copy)) o.s = std::move(copy);
    }
    for (Obj& item : o.a) decryptStrings(item, num, gen);
    for (auto& entry : o.d) decryptStrings(entry.second, num, gen);
  }

  uint32_t rootNum() const { return root; }

 private:
  bool readRange(uint64_t offset, size_t len, std::string& out) {
    if (offset > src.size || len > src.size - offset) return false;
    out.resize(len);
    return len == 0 || src.read(offset, reinterpret_cast<uint8_t*>(&out[0]), len);
  }

  bool scanToEndstream(uint64_t pos, std::string& out) {
    out.clear();
    std::string chunk;
    uint64_t at = pos;
    while (at < src.size && out.size() <= limits.maxStreamBytes) {
      const size_t n = static_cast<size_t>(std::min<uint64_t>(16384, src.size - at));
      if (!readRange(at, n, chunk)) return false;
      out += chunk;
      const size_t e = out.find("endstream");
      if (e != std::string::npos) {
        out.resize(e);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        return true;
      }
      at += n;
    }
    return false;
  }

  void collectFilters(const Obj& stream, std::vector<std::string>& filters, std::vector<Obj>& parms) {
    Obj f = resolveKey(stream, "Filter");
    if (f.type == Obj::Null) f = resolveKey(stream, "F");
    Obj p = resolveKey(stream, "DecodeParms");
    if (p.type == Obj::Null) p = resolveKey(stream, "DP");
    if (f.type == Obj::Name) {
      filters.push_back(f.s);
      parms.push_back(p);
    } else if (f.type == Obj::Arr) {
      for (size_t k = 0; k < f.a.size(); ++k) {
        const Obj fk = resolve(f.a[k]);
        filters.push_back(fk.s);
        parms.push_back(p.type == Obj::Arr && k < p.a.size() ? resolve(p.a[k]) : (k == 0 ? p : Obj()));
      }
    }
  }

  static bool ascii85(const std::string& in, std::string& out) {
    uint32_t group[5];
    int n = 0;
    size_t i = 0;
    if (in.compare(0, 2, "<~") == 0) i = 2;
    for (; i < in.size(); ++i) {
      const char c = in[i];
      if (isWhite(c)) continue;
      if (c == '~') break;
      if (c == 'z' && n == 0) {
        out.append(4, '\0');
        continue;
      }
      if (c < '!' || c > 'u') return false;
      group[n++] = static_cast<uint32_t>(c - '!');
      if (n == 5) {
        uint32_t v = 0;
        for (int k = 0; k < 5; ++k) v = v * 85 + group[k];
        for (int k = 3; k >= 0; --k) out += static_cast<char>((v >> (8 * k)) & 0xFF);
        n = 0;
      }
    }
    if (n > 1) {
      for (int k = n; k < 5; ++k) group[k] = 84;
      uint32_t v = 0;
      for (int k = 0; k < 5; ++k) v = v * 85 + group[k];
      for (int k = 3; k > 4 - n; --k) out += static_cast<char>((v >> (8 * k)) & 0xFF);
    }
    return true;
  }

  static bool pngPredictor(std::string& data, int rowBytes, int pixelBytes) {
    if (rowBytes <= 0) return false;
    const size_t stride = static_cast<size_t>(rowBytes) + 1;
    const size_t rows = data.size() / stride;
    std::string out(rows * static_cast<size_t>(rowBytes), '\0');
    for (size_t row = 0; row < rows; ++row) {
      const uint8_t filter = static_cast<uint8_t>(data[row * stride]);
      const uint8_t* in = reinterpret_cast<const uint8_t*>(&data[row * stride + 1]);
      uint8_t* cur = reinterpret_cast<uint8_t*>(&out[row * rowBytes]);
      const uint8_t* up = row ? cur - rowBytes : nullptr;
      for (int x = 0; x < rowBytes; ++x) {
        const int a = x >= pixelBytes ? cur[x - pixelBytes] : 0;
        const int b = up ? up[x] : 0;
        const int c = up && x >= pixelBytes ? up[x - pixelBytes] : 0;
        int v = in[x];
        switch (filter) {
          case 0:
            break;
          case 1:
            v += a;
            break;
          case 2:
            v += b;
            break;
          case 3:
            v += (a + b) / 2;
            break;
          case 4: {
            const int p = a + b - c;
            const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
            v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            break;
          }
          default:
            return false;
        }
        cur[x] = static_cast<uint8_t>(v & 0xFF);
      }
    }
    data = std::move(out);
    return true;
  }

  bool findStartXref(uint64_t& out) {
    const size_t n = static_cast<size_t>(std::min<uint64_t>(2048, src.size));
    std::string tail;
    if (!readRange(src.size - n, n, tail)) return false;
    const size_t at = tail.rfind("startxref");
    if (at == std::string::npos) return false;
    Lexer lx(tail, at + 9);
    Obj v;
    if (!lx.object(v) || v.type != Obj::Int || v.i <= 0) return false;
    out = static_cast<uint64_t>(v.i);
    return true;
  }

  static uint32_t trailerRoot(const Obj& trailer) {
    const Obj* r = trailer.get("Root");
    return r && r->type == Obj::Ref ? static_cast<uint32_t>(r->i) : 0;
  }

  // Reads "N G obj <object> [stream]" at `offset`; the buffer window grows until the object fits.
  bool loadAt(uint64_t offset, uint32_t expectNum, Obj& out, int depth) {
    for (size_t window = 8192; window <= (1u << 21); window *= 4) {
      const size_t n = static_cast<size_t>(std::min<uint64_t>(window, src.size - std::min(offset, src.size)));
      std::string buf;
      if (n == 0 || !readRange(offset, n, buf)) return false;
      Lexer lx(buf);
      Obj numObj, genObj;
      if (!lx.object(numObj) || numObj.type != Obj::Int) return false;
      if (!lx.object(genObj) || genObj.type != Obj::Int) return false;
      if (lx.word() != "obj") return false;
      if (expectNum != 0 && static_cast<uint32_t>(numObj.i) != expectNum) return false;
      if (!lx.object(out)) {
        if (lx.truncated && n == window && offset + n < src.size) continue;
        return false;
      }
      if (out.type == Obj::Dict && lx.peekIs("stream")) {
        lx.word();
        size_t p = lx.position();
        if (p < buf.size() && buf[p] == '\r') ++p;
        if (p < buf.size() && buf[p] == '\n') ++p;
        out.type = Obj::Stream;
        out.streamPos = offset + p;
        const Obj* len = out.get("Length");
        out.streamLen = len && len->type == Obj::Int ? len->i : -1;
      }
      (void)depth;
      out.objNum = static_cast<uint32_t>(numObj.i);
      out.gen = static_cast<uint32_t>(genObj.i);
      // Strings are encrypted too, except in the encryption dictionary itself and in cross-reference streams.
      const bool xrefStream = out.type == Obj::Stream && out.d.count("Type") && out.d.at("Type").s == "XRef";
      if (encrypted && out.objNum != encryptNum && !xrefStream) decryptStrings(out, out.objNum, out.gen);
      return true;
    }
    return false;
  }

  bool loadFromObjStm(uint32_t stmNum, uint32_t index, uint32_t wantNum, Obj& out, int depth) {
    if (cachedStm != stmNum) {
      Obj stm;
      if (!load(stmNum, stm, depth + 1) || stm.type != Obj::Stream) return false;
      if (!streamData(stm, stmData)) {
        cachedStm = 0;
        return false;
      }
      cachedStm = stmNum;
      stmN = 0;
      stmFirst = 0;
      const Obj n = resolveKey(stm, "N");
      const Obj first = resolveKey(stm, "First");
      if (n.type != Obj::Int || first.type != Obj::Int || n.i < 0 || n.i > 100000) {
        cachedStm = 0;
        return false;
      }
      stmN = static_cast<size_t>(n.i);
      stmFirst = static_cast<size_t>(first.i);
    }
    Lexer head(stmData);
    size_t offset = 0;
    bool found = false;
    for (size_t k = 0; k < stmN; ++k) {
      Obj on, oo;
      if (!head.object(on) || !head.object(oo)) return false;
      if (k == index || static_cast<uint32_t>(on.i) == wantNum) {
        if (static_cast<uint32_t>(on.i) != wantNum) continue;
        offset = static_cast<size_t>(oo.i);
        found = true;
        break;
      }
    }
    if (!found || stmFirst + offset >= stmData.size()) return false;
    Lexer lx(stmData, stmFirst + offset);
    return lx.object(out);
  }

  bool readXref(uint64_t offset, Obj& trailer) {
    std::string head;
    if (!readRange(offset, static_cast<size_t>(std::min<uint64_t>(16, src.size - std::min(offset, src.size))), head)) {
      return false;
    }
    if (head.compare(0, 4, "xref") == 0) return readXrefTable(offset, trailer);
    return readXrefStream(offset, trailer);
  }

  bool readXrefTable(uint64_t offset, Obj& trailer) {
    uint64_t pos = offset + 4;
    for (int subsections = 0; subsections < 100000; ++subsections) {
      std::string buf;
      const size_t n = static_cast<size_t>(std::min<uint64_t>(4096, src.size - std::min(pos, src.size)));
      if (n == 0 || !readRange(pos, n, buf)) return false;
      Lexer lx(buf);
      lx.skipSpace();
      if (lx.peekIs("trailer")) {
        lx.word();
        if (!lx.object(trailer)) {
          // A big trailer: read it with more room.
          std::string bigger;
          if (!readRange(pos, static_cast<size_t>(std::min<uint64_t>(65536, src.size - pos)), bigger)) return false;
          Lexer l2(bigger);
          l2.word();
          if (!l2.object(trailer)) return false;
        }
        if (const Obj* stm = trailer.get("XRefStm")) {
          Obj ignored;
          if (stm->isNum()) readXrefStream(static_cast<uint64_t>(stm->num()), ignored);
        }
        return trailer.isDict();
      }
      Obj startObj, countObj;
      if (!lx.object(startObj) || !lx.object(countObj) || startObj.type != Obj::Int || countObj.type != Obj::Int) {
        return false;
      }
      if (startObj.i < 0 || countObj.i < 0 || countObj.i > 50000000) return false;
      uint64_t entriesAt = pos + lx.position();
      // Entries are 20 bytes each, after the newline that ends the subsection line.
      std::string tmp;
      if (!readRange(entriesAt, std::min<uint64_t>(4, src.size - entriesAt), tmp)) return false;
      size_t skip = 0;
      while (skip < tmp.size() && isWhite(tmp[skip])) ++skip;
      entriesAt += skip;
      for (int64_t k = 0; k < countObj.i; ++k) {
        std::string entry;
        if (!readRange(entriesAt + static_cast<uint64_t>(k) * 20, 20, entry)) return false;
        const uint32_t num = static_cast<uint32_t>(startObj.i + k);
        if (entry[17] == 'n' && !xref.count(num)) {
          XEntry e;
          e.kind = XEntry::InFile;
          e.offset = std::strtoull(entry.substr(0, 10).c_str(), nullptr, 10);
          xref[num] = e;
        } else if (entry[17] == 'f' && !xref.count(num)) {
          xref[num] = XEntry();
        }
      }
      pos = entriesAt + static_cast<uint64_t>(countObj.i) * 20;
    }
    return false;
  }

  bool readXrefStream(uint64_t offset, Obj& trailer) {
    Obj stm;
    if (!loadAt(offset, 0, stm, 0) || stm.type != Obj::Stream) return false;
    const Obj type = resolveKey(stm, "Type");
    if (type.s != "XRef") return false;
    std::string data;
    if (!streamData(stm, data)) return false;
    const Obj w = resolveKey(stm, "W");
    if (w.type != Obj::Arr || w.a.size() != 3) return false;
    const int w0 = static_cast<int>(w.a[0].num()), w1 = static_cast<int>(w.a[1].num()),
              w2 = static_cast<int>(w.a[2].num());
    if (w0 < 0 || w1 < 0 || w2 < 0 || w0 > 8 || w1 > 8 || w2 > 8 || w0 + w1 + w2 == 0) return false;
    const Obj size = resolveKey(stm, "Size");
    std::vector<std::pair<int64_t, int64_t>> ranges;
    const Obj index = resolveKey(stm, "Index");
    if (index.type == Obj::Arr) {
      for (size_t k = 0; k + 1 < index.a.size(); k += 2) {
        ranges.emplace_back(static_cast<int64_t>(index.a[k].num()), static_cast<int64_t>(index.a[k + 1].num()));
      }
    } else {
      ranges.emplace_back(0, size.isNum() ? static_cast<int64_t>(size.num()) : 0);
    }
    const size_t rowLen = static_cast<size_t>(w0 + w1 + w2);
    size_t at = 0;
    const auto field = [&](int width) {
      uint64_t v = 0;
      for (int k = 0; k < width; ++k) v = (v << 8) | static_cast<uint8_t>(data[at++]);
      return v;
    };
    for (const auto& [start, count] : ranges) {
      for (int64_t k = 0; k < count; ++k) {
        if (at + rowLen > data.size()) break;
        const uint64_t t = w0 ? field(w0) : 1;
        const uint64_t f2 = field(w1);
        const uint64_t f3 = field(w2);
        const uint32_t num = static_cast<uint32_t>(start + k);
        if (xref.count(num)) continue;
        XEntry e;
        if (t == 1) {
          e.kind = XEntry::InFile;
          e.offset = f2;
        } else if (t == 2) {
          e.kind = XEntry::InStream;
          e.stream = static_cast<uint32_t>(f2);
          e.index = static_cast<uint32_t>(f3);
        }
        xref[num] = e;
      }
    }
    trailer = stm;
    return true;
  }

  const Source& src;
  const Inflate& inflate;
  const Limits& limits;
  std::unordered_map<uint32_t, XEntry> xref;
  std::unordered_map<uint32_t, Obj> cache;
  uint32_t root = 0;
  Obj firstTrailer;
  bool encrypted = false;
  bool aes = false;
  std::string fileKey;
  uint32_t encryptNum = 0;
  uint32_t cachedStm = 0;
  std::string stmData;
  size_t stmN = 0, stmFirst = 0;
};

// ---------------------------------------------------------------------------------------------
// Fonts

struct Font {
  bool twoByte = false;
  std::unordered_map<uint32_t, std::string> toUnicode;  // code -> UTF-8
  bool hasToUnicode = false;
  std::string simple[256];  // simple fonts without a ToUnicode map
  bool simpleReady = false;
  // Glyph widths in 1/1000 em, to know where a string ends and so where a word space is.
  double defaultWidth = 500;
  int firstChar = 0;
  std::vector<double> widths;                      // simple fonts, from FirstChar
  std::unordered_map<uint32_t, double> cidWidths;  // CID fonts
  double width(uint32_t code) const {
    if (twoByte) {
      const auto it = cidWidths.find(code);
      return it == cidWidths.end() ? defaultWidth : it->second;
    }
    const int k = static_cast<int>(code) - firstChar;
    return k >= 0 && static_cast<size_t>(k) < widths.size() ? widths[static_cast<size_t>(k)] : defaultWidth;
  }
};

// cp1252 for 0x80-0x9F, latin-1 elsewhere.
uint32_t winAnsi(uint8_t c) {
  static const uint16_t high[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
  return c >= 0x80 && c < 0xA0 ? high[c - 0x80] : c;
}

uint32_t glyphNameToUnicode(const std::string& n) {
  static const std::map<std::string, uint32_t> names = {{"space", 32},
                                                        {"exclam", 33},
                                                        {"quotedbl", 34},
                                                        {"numbersign", 35},
                                                        {"dollar", 36},
                                                        {"percent", 37},
                                                        {"ampersand", 38},
                                                        {"quotesingle", 39},
                                                        {"parenleft", 40},
                                                        {"parenright", 41},
                                                        {"asterisk", 42},
                                                        {"plus", 43},
                                                        {"comma", 44},
                                                        {"hyphen", 45},
                                                        {"minus", 0x2212},
                                                        {"period", 46},
                                                        {"slash", 47},
                                                        {"zero", 48},
                                                        {"one", 49},
                                                        {"two", 50},
                                                        {"three", 51},
                                                        {"four", 52},
                                                        {"five", 53},
                                                        {"six", 54},
                                                        {"seven", 55},
                                                        {"eight", 56},
                                                        {"nine", 57},
                                                        {"colon", 58},
                                                        {"semicolon", 59},
                                                        {"less", 60},
                                                        {"equal", 61},
                                                        {"greater", 62},
                                                        {"question", 63},
                                                        {"at", 64},
                                                        {"bracketleft", 91},
                                                        {"backslash", 92},
                                                        {"bracketright", 93},
                                                        {"underscore", 95},
                                                        {"grave", 96},
                                                        {"braceleft", 123},
                                                        {"bar", 124},
                                                        {"braceright", 125},
                                                        {"asciitilde", 126},
                                                        {"endash", 0x2013},
                                                        {"emdash", 0x2014},
                                                        {"quoteleft", 0x2018},
                                                        {"quoteright", 0x2019},
                                                        {"quotedblleft", 0x201C},
                                                        {"quotedblright", 0x201D},
                                                        {"bullet", 0x2022},
                                                        {"ellipsis", 0x2026},
                                                        {"fi", 0xFB01},
                                                        {"fl", 0xFB02},
                                                        {"eacute", 0xE9},
                                                        {"egrave", 0xE8},
                                                        {"agrave", 0xE0},
                                                        {"aacute", 0xE1},
                                                        {"ccedilla", 0xE7},
                                                        {"ntilde", 0xF1},
                                                        {"odieresis", 0xF6},
                                                        {"udieresis", 0xFC},
                                                        {"adieresis", 0xE4},
                                                        {"germandbls", 0xDF}};
  if (n.size() == 1) return static_cast<uint8_t>(n[0]);
  if (n.size() == 7 && n.compare(0, 3, "uni") == 0)
    return static_cast<uint32_t>(std::strtoul(n.c_str() + 3, nullptr, 16));
  const auto it = names.find(n);
  if (it != names.end()) return it->second;
  return 0;
}

void parseToUnicode(const std::string& cmap, Font& font) {
  Lexer lx(cmap);
  const auto hexCode = [](const Obj& o, uint32_t& code, size_t& bytes) {
    if (o.type != Obj::Str || o.s.empty() || o.s.size() > 4) return false;
    code = 0;
    for (const char c : o.s) code = (code << 8) | static_cast<uint8_t>(c);
    bytes = o.s.size();
    return true;
  };
  const auto utf16ToUtf8 = [](const std::string& s) {
    std::string out;
    for (size_t k = 0; k + 1 < s.size(); k += 2) {
      uint32_t u = (static_cast<uint8_t>(s[k]) << 8) | static_cast<uint8_t>(s[k + 1]);
      if (u >= 0xD800 && u < 0xDC00 && k + 3 < s.size()) {
        const uint32_t lo = (static_cast<uint8_t>(s[k + 2]) << 8) | static_cast<uint8_t>(s[k + 3]);
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
        k += 2;
      }
      appendUtf8(out, u);
    }
    return out;
  };
  size_t maxBytes = 0;
  while (!lx.atEnd()) {
    Obj o;
    std::string_view kw;
    if (lx.object(o, &kw)) continue;
    if (kw == "beginbfchar") {
      while (true) {
        Obj src, dst;
        std::string_view k2;
        if (!lx.object(src, &k2)) break;
        if (!lx.object(dst, &k2)) break;
        uint32_t code;
        size_t bytes;
        if (hexCode(src, code, bytes) && dst.type == Obj::Str) {
          font.toUnicode[code] = utf16ToUtf8(dst.s);
          maxBytes = std::max(maxBytes, bytes);
        }
      }
    } else if (kw == "beginbfrange") {
      while (true) {
        Obj lo, hi, dst;
        std::string_view k2;
        if (!lx.object(lo, &k2)) break;
        if (!lx.object(hi, &k2)) break;
        if (!lx.object(dst, &k2)) break;
        uint32_t a, b;
        size_t bytes;
        if (!hexCode(lo, a, bytes) || !hexCode(hi, b, bytes) || b < a || b - a > 0xFFFF ||
            font.toUnicode.size() + (b - a) > 80000)
          continue;
        maxBytes = std::max(maxBytes, bytes);
        if (dst.type == Obj::Str) {
          std::string base = dst.s;
          for (uint32_t c = a; c <= b; ++c) {
            font.toUnicode[c] = utf16ToUtf8(base);
            // Increment the last UTF-16 unit.
            if (base.size() >= 2) {
              for (size_t p = base.size(); p-- > base.size() - 2;) {
                if (++reinterpret_cast<uint8_t&>(base[p]) != 0) break;
              }
            }
          }
        } else if (dst.type == Obj::Arr) {
          for (uint32_t c = a; c <= b && c - a < dst.a.size(); ++c) font.toUnicode[c] = utf16ToUtf8(dst.a[c - a].s);
        }
      }
    }
  }
  font.hasToUnicode = !font.toUnicode.empty();
  if (maxBytes >= 2) font.twoByte = true;
}

void loadWidths(Doc& doc, const Obj& fontDict, Font& font) {
  if (font.twoByte) {
    const Obj desc = doc.resolveKey(fontDict, "DescendantFonts");
    const Obj cid = desc.type == Obj::Arr && !desc.a.empty() ? doc.resolve(desc.a[0]) : Obj();
    if (!cid.isDict()) return;
    const Obj dw = doc.resolveKey(cid, "DW");
    font.defaultWidth = dw.isNum() ? dw.num() : 1000;
    const Obj w = doc.resolveKey(cid, "W");
    if (w.type != Obj::Arr) return;
    for (size_t k = 0; k < w.a.size() && font.cidWidths.size() < 70000;) {
      const Obj first = doc.resolve(w.a[k]);
      const Obj second = k + 1 < w.a.size() ? doc.resolve(w.a[k + 1]) : Obj();
      if (!first.isNum() || first.num() < 0 || first.num() > 4294967000.0) break;
      if (second.type == Obj::Arr) {
        for (size_t j = 0; j < second.a.size(); ++j) {
          font.cidWidths[static_cast<uint32_t>(first.num()) + static_cast<uint32_t>(j)] =
              doc.resolve(second.a[j]).num();
        }
        k += 2;
      } else if (second.isNum() && second.num() >= first.num() && second.num() <= 4294967000.0 && k + 2 < w.a.size()) {
        const double width = doc.resolve(w.a[k + 2]).num();
        const uint32_t a = static_cast<uint32_t>(first.num());
        const uint32_t b = static_cast<uint32_t>(second.num());
        for (uint32_t c = a; c <= b && c - a < 70000; ++c) font.cidWidths[c] = width;
        k += 3;
      } else {
        break;
      }
    }
    return;
  }
  const Obj first = doc.resolveKey(fontDict, "FirstChar");
  const Obj widths = doc.resolveKey(fontDict, "Widths");
  if (first.isNum()) font.firstChar = static_cast<int>(first.num());
  if (widths.type == Obj::Arr) {
    for (const Obj& item : widths.a) font.widths.push_back(doc.resolve(item).num());
  }
  const Obj desc = doc.resolveKey(fontDict, "FontDescriptor");
  if (desc.isDict()) {
    const Obj missing = doc.resolveKey(desc, "MissingWidth");
    if (missing.isNum() && missing.num() > 0) font.defaultWidth = missing.num();
  }
}

Font loadFont(Doc& doc, const Obj& fontDict) {
  Font font;
  const Obj subtype = doc.resolveKey(fontDict, "Subtype");
  if (subtype.s == "Type0") font.twoByte = true;
  loadWidths(doc, fontDict, font);
  const Obj toUni = doc.resolveKey(fontDict, "ToUnicode");
  if (toUni.type == Obj::Stream) {
    std::string cmap;
    if (doc.streamData(toUni, cmap)) parseToUnicode(cmap, font);
  }
  if (font.twoByte || font.hasToUnicode) return font;
  // A simple font by its encoding.
  const Obj enc = doc.resolveKey(fontDict, "Encoding");
  for (int c = 0; c < 256; ++c) {
    if (c < 32 && c != '\t') continue;  // control codes show nothing unless a Differences entry says so
    std::string s;
    appendUtf8(s, winAnsi(static_cast<uint8_t>(c)));
    font.simple[c] = s;
  }
  if (enc.isDict()) {
    const Obj diff = doc.resolveKey(enc, "Differences");
    if (diff.type == Obj::Arr) {
      int code = 0;
      for (const Obj& item : diff.a) {
        const Obj v = doc.resolve(item);
        if (v.type == Obj::Int) {
          code = static_cast<int>(v.i);
        } else if (v.type == Obj::Name && code >= 0 && code < 256) {
          const uint32_t u = glyphNameToUnicode(v.s);
          if (u) {
            std::string s;
            appendUtf8(s, u);
            font.simple[code] = s;
          }
          ++code;
        }
      }
    }
  }
  font.simpleReady = true;
  return font;
}

// ---------------------------------------------------------------------------------------------
// Content streams

struct TextOut {
  std::string text;
  double lastY = 0;
  bool haveY = false;
  double fontSize = 10;
  size_t maxBytes = 0;

  bool full() const { return text.size() >= maxBytes; }

  void gap(double y, double size, bool needSpace) {
    if (!haveY) {
      haveY = true;
      lastY = y;
      return;
    }
    const double dy = std::fabs(y - lastY);
    const double ref = std::max(size, 1.0);
    if (dy > 0.4 * ref) {
      // A new line: a paragraph when the gap is wide, the line joins otherwise (a hyphen at the end goes).
      if (dy > 1.9 * ref) {
        while (!text.empty() && text.back() == ' ') text.pop_back();
        text += "\n\n";
      } else if (text.size() >= 2 && text.back() == '-' && text[text.size() - 2] != ' ' &&
                 static_cast<uint8_t>(text[text.size() - 2]) >= 0x41) {
        text.pop_back();
      } else if (!text.empty() && text.back() != ' ' && text.back() != '\n') {
        text += ' ';
      }
    } else if (needSpace && !text.empty() && text.back() != ' ' && text.back() != '\n') {
      text += ' ';
    }
    lastY = y;
  }
};

struct Matrix {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
};

// p' = p x M, row vectors as in the PDF spec: the result applies m first and then n.
Matrix multiply(const Matrix& m, const Matrix& n) {
  return Matrix{m.a * n.a + m.b * n.c, m.a * n.b + m.b * n.d,       m.c * n.a + m.d * n.c,
                m.c * n.b + m.d * n.d, m.e * n.a + m.f * n.c + n.e, m.e * n.b + m.f * n.d + n.f};
}

// One text-showing operator's text with where it sat on the page (x across, row down).
struct Fragment {
  double x0 = 0, x1 = 0;  // left edge and where the last glyph ended
  double row = 0;         // smaller is higher on the page
  double size = 10;       // line height scale, for deciding what is a new line
  double unit = 10;       // the em across, for deciding what is a word gap
  std::string text;
};

struct PageText {
  std::vector<Fragment> frags;
  size_t bytes = 0;
  size_t maxBytes = 0;
  bool full() const { return bytes >= maxBytes; }
};

// Text order for a page set in two columns: the fragments read top to bottom in the left column and then the
// right one, with anything that crosses the gutter (a title, a table) as a break between bands. Other pages
// keep the order the content stream drew them in. Empty when the page isn't two columns.
std::vector<size_t> twoColumnOrder(const std::vector<Fragment>& frags) {
  if (frags.size() < 12) return {};
  double minX = frags[0].x0, maxX = frags[0].x1;
  for (const Fragment& f : frags) {
    minX = std::min(minX, f.x0);
    maxX = std::max(maxX, f.x1);
  }
  const double width = maxX - minX;
  if (width < 150) return {};
  // The best gutter: the x in the middle part of the page that the fewest fragments cross.
  double gutter = 0;
  size_t fewest = frags.size() + 1;
  double runStart = 0;
  size_t bestRunLength = 0, runLength = 0;
  for (int step = 30; step <= 70; ++step) {
    const double x = minX + width * step / 100.0;
    size_t crossing = 0;
    for (const Fragment& f : frags) crossing += f.x0 < x && f.x1 > x;
    if (crossing < fewest) {
      fewest = crossing;
      gutter = x;
      runStart = x;
      runLength = 1;
      bestRunLength = 1;
    } else if (crossing == fewest) {
      ++runLength;
      if (runLength > bestRunLength) {
        bestRunLength = runLength;
        gutter = runStart + (x - runStart) / 2;
      }
    } else {
      runLength = 0;
      runStart = x;
    }
  }
  size_t left = 0, right = 0, spanning = 0;
  for (const Fragment& f : frags) {
    if (f.x1 <= gutter) {
      ++left;
    } else if (f.x0 >= gutter) {
      ++right;
    } else {
      ++spanning;
    }
  }
  const size_t n = frags.size();
  if (left * 5 < n || right * 5 < n || spanning * 12 > n) return {};
  // Lines first: fragments by row, a new line when the row moves more than a part of a line height, each
  // line left to right. Then band by band: a line that crosses the gutter splits the page into bands, and
  // a band is its left column's lines and then its right column's.
  std::vector<size_t> byRow(n);
  for (size_t i = 0; i < n; ++i) byRow[i] = i;
  std::stable_sort(byRow.begin(), byRow.end(), [&](size_t a, size_t b) { return frags[a].row < frags[b].row; });
  std::vector<size_t> order;
  std::vector<size_t> leftBand, rightBand;
  const auto flushBand = [&] {
    order.insert(order.end(), leftBand.begin(), leftBand.end());
    order.insert(order.end(), rightBand.begin(), rightBand.end());
    leftBand.clear();
    rightBand.clear();
  };
  for (size_t start = 0; start < n;) {
    size_t end = start + 1;
    const double lineRow = frags[byRow[start]].row;
    const double tolerance = 0.4 * std::max(frags[byRow[start]].size, 1.0);
    while (end < n && std::fabs(frags[byRow[end]].row - lineRow) <= tolerance) ++end;
    std::vector<size_t> line(byRow.begin() + static_cast<std::ptrdiff_t>(start),
                             byRow.begin() + static_cast<std::ptrdiff_t>(end));
    std::stable_sort(line.begin(), line.end(), [&](size_t a, size_t b) { return frags[a].x0 < frags[b].x0; });
    bool crosses = false;
    for (const size_t i : line) crosses = crosses || (frags[i].x0 < gutter && frags[i].x1 > gutter);
    if (crosses) {
      flushBand();
      order.insert(order.end(), line.begin(), line.end());
    } else {
      for (const size_t i : line) (frags[i].x1 <= gutter ? leftBand : rightBand).push_back(i);
    }
    start = end;
  }
  flushBand();
  return order;
}

// The page's text: fragments in reading order, with the spaces, line joins and paragraph breaks their
// positions imply.
std::string layoutPage(const PageText& page) {
  TextOut out;
  out.maxBytes = page.maxBytes;
  std::vector<size_t> order = twoColumnOrder(page.frags);
  if (order.empty()) {
    order.resize(page.frags.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  }
  const Fragment* prev = nullptr;
  for (const size_t i : order) {
    const Fragment& f = page.frags[i];
    bool needSpace = false;
    if (prev) {
      const double gap = f.x0 - prev->x1;  // where the last glyph ended against where this one starts
      needSpace = gap > 0.15 * f.unit || gap < -2 * f.unit;
    }
    out.gap(f.row, f.size, needSpace);
    out.text += f.text;
    prev = &f;
    if (out.full()) break;
  }
  return out.text;
}

class Interpreter {
 public:
  Interpreter(Doc& doc, std::unordered_map<uint32_t, std::unique_ptr<Font>>& fonts, PageText& out)
      : doc(doc), fonts(fonts), out(out) {}

  void run(const std::string& content, const Obj& resources, int depth, const Matrix& base = Matrix()) {
    if (depth > 3) return;
    Lexer lx(content);
    std::vector<Obj> stack;
    Font* font = nullptr;
    double size = 10;
    Matrix tm, line;
    Matrix ctm = base;
    std::vector<Matrix> saved;
    double leading = 0, charSpace = 0, wordSpace = 0, hScale = 1;
    Fragment cur;
    bool open = false;
    const Obj fontDict = doc.resolveKey(resources, "Font");
    const Obj xobjects = doc.resolveKey(resources, "XObject");

    const auto num = [&](size_t k) { return k < stack.size() ? stack[k].num() : 0.0; };
    const auto move = [&](double textX) {  // the text position along its line
      tm.e += textX * tm.a;
      tm.f += textX * tm.b;
    };
    const auto closeFragment = [&] {
      if (open && !cur.text.empty()) {
        cur.x1 = multiply(tm, ctm).e;
        out.bytes += cur.text.size();
        out.frags.push_back(std::move(cur));
      }
      cur = Fragment();
      open = false;
    };
    const auto showBytes = [&](const std::string& s) {
      if (!font || out.full()) return;
      if (!open) {
        const Matrix trm = multiply(tm, ctm);  // text space to the page
        cur = Fragment();
        cur.x0 = trm.e;
        cur.row = trm.d >= 0 ? -trm.f : trm.f;
        cur.size = size * std::max(std::fabs(trm.d), std::fabs(trm.c));
        cur.unit = size * std::hypot(trm.a, trm.b);
        open = true;
      }
      double advance = 0;
      if (font->twoByte) {
        for (size_t k = 0; k + 1 < s.size(); k += 2) {
          const uint32_t code = (static_cast<uint8_t>(s[k]) << 8) | static_cast<uint8_t>(s[k + 1]);
          const auto it = font->toUnicode.find(code);
          if (it != font->toUnicode.end()) cur.text += it->second;
          advance += (font->width(code) / 1000.0 * size + charSpace) * hScale;
        }
      } else {
        for (const char ch : s) {
          const uint8_t b = static_cast<uint8_t>(ch);
          if (font->hasToUnicode) {
            const auto it = font->toUnicode.find(b);
            if (it != font->toUnicode.end()) {
              cur.text += it->second;
            } else if (b >= 32 && b < 127) {
              cur.text += ch;
            }
          } else {
            cur.text += font->simple[b];
          }
          advance += (font->width(b) / 1000.0 * size + charSpace + (b == 32 ? wordSpace : 0)) * hScale;
        }
      }
      move(advance);
    };

    std::string_view kw;
    while (!lx.atEnd() && !out.full()) {
      Obj o;
      if (lx.object(o, &kw)) {
        stack.push_back(std::move(o));
        if (stack.size() > 64) stack.erase(stack.begin());
        continue;
      }
      if (kw.empty()) {
        if (lx.truncated) break;
        continue;  // a stray delimiter
      }
      const std::string op(kw);
      kw = std::string_view();
      if (op == "BT") {
        tm = Matrix();
        line = Matrix();
      } else if (op == "q") {
        if (saved.size() < 64) saved.push_back(ctm);
      } else if (op == "Q") {
        if (!saved.empty()) {
          ctm = saved.back();
          saved.pop_back();
        }
      } else if (op == "cm" && stack.size() >= 6) {
        ctm = multiply(Matrix{num(0), num(1), num(2), num(3), num(4), num(5)}, ctm);
      } else if (op == "Tf" && stack.size() >= 2) {
        size = num(stack.size() - 1);
        const Obj nameObj = stack[stack.size() - 2];
        font = nullptr;
        if (const Obj* ref = fontDict.get(nameObj.s)) {
          const uint32_t key = ref->type == Obj::Ref ? static_cast<uint32_t>(ref->i) : 0;
          auto it = key ? fonts.find(key) : fonts.end();
          if (it == fonts.end()) {
            auto loaded = std::make_unique<Font>(loadFont(doc, doc.resolve(*ref)));
            font = loaded.get();
            if (key)
              fonts[key] = std::move(loaded);
            else
              inlineFonts.push_back(std::move(loaded));
          } else {
            font = it->second.get();
          }
        }
      } else if (op == "Td" && stack.size() >= 2) {
        line.e += num(0) * line.a + num(1) * line.c;
        line.f += num(0) * line.b + num(1) * line.d;
        tm = line;
      } else if (op == "TD" && stack.size() >= 2) {
        leading = -num(1);
        line.e += num(0) * line.a + num(1) * line.c;
        line.f += num(0) * line.b + num(1) * line.d;
        tm = line;
      } else if (op == "Tm" && stack.size() >= 6) {
        tm = line = Matrix{num(0), num(1), num(2), num(3), num(4), num(5)};
      } else if (op == "T*") {
        line.f -= leading * line.d;
        tm = line;
      } else if (op == "TL" && !stack.empty()) {
        leading = num(0);
      } else if (op == "Tc" && !stack.empty()) {
        charSpace = num(0);
      } else if (op == "Tw" && !stack.empty()) {
        wordSpace = num(0);
      } else if (op == "Tz" && !stack.empty()) {
        hScale = num(0) / 100.0;
      } else if (op == "Tj" && !stack.empty()) {
        showBytes(stack.back().s);
        closeFragment();
      } else if (op == "'" || op == "\"") {
        line.f -= leading * line.d;
        tm = line;
        if (!stack.empty()) showBytes(stack.back().s);
        closeFragment();
      } else if (op == "TJ" && !stack.empty() && stack.back().type == Obj::Arr) {
        for (const Obj& item : stack.back().a) {
          if (item.type == Obj::Str) {
            showBytes(item.s);
          } else if (item.isNum()) {
            move(-item.num() / 1000.0 * size * hScale);
            if (item.num() < -200 && open && !cur.text.empty() && cur.text.back() != ' ')
              cur.text += ' ';  // a word space
          }
        }
        closeFragment();
      } else if (op == "Do" && !stack.empty() && stack.back().type == Obj::Name) {
        if (const Obj* ref = xobjects.get(stack.back().s)) {
          const Obj form = doc.resolve(*ref);
          if (form.type == Obj::Stream && doc.resolveKey(form, "Subtype").s == "Form") {
            std::string data;
            if (doc.streamData(form, data)) {
              const Obj res = doc.resolveKey(form, "Resources");
              Matrix formMatrix;
              const Obj fm = doc.resolveKey(form, "Matrix");
              if (fm.type == Obj::Arr && fm.a.size() == 6) {
                formMatrix =
                    Matrix{fm.a[0].num(), fm.a[1].num(), fm.a[2].num(), fm.a[3].num(), fm.a[4].num(), fm.a[5].num()};
              }
              run(data, res.isDict() ? res : resources, depth + 1, multiply(formMatrix, ctm));
            }
          }
        }
      }
      stack.clear();
    }
  }

 private:
  Doc& doc;
  std::unordered_map<uint32_t, std::unique_ptr<Font>>& fonts;
  std::vector<std::unique_ptr<Font>> inlineFonts;
  PageText& out;
};

// ---------------------------------------------------------------------------------------------
// Pages

void walkPages(Doc& doc, const Obj& node, const Obj& inheritedResources,
               const std::function<void(const Obj& page, const Obj& resources)>& visit, size_t& visits, size_t& pages,
               int depth, size_t maxPages) {
  // A page tree that loops back on itself would be walked for ever: count every node.
  if (depth > 32 || pages >= maxPages || ++visits > 50000) return;
  Obj resources = inheritedResources;
  const Obj own = doc.resolveKey(node, "Resources");
  if (own.isDict()) resources = own;
  const Obj type = doc.resolveKey(node, "Type");
  const Obj kids = doc.resolveKey(node, "Kids");
  if (kids.type == Obj::Arr && type.s != "Page") {
    for (const Obj& kid : kids.a) {
      const Obj child = doc.resolve(kid);
      if (child.isDict()) walkPages(doc, child, resources, visit, visits, pages, depth + 1, maxPages);
      if (pages >= maxPages) return;
    }
    return;
  }
  if (node.isDict()) {
    ++pages;
    visit(node, resources);
  }
}

}  // namespace

bool extractText(const Source& source, const Inflate& inflate,
                 const std::function<void(size_t, size_t, const std::string&)>& page, Result* resultOut,
                 const Limits& limits) {
  Result result;
  Doc doc(source, inflate, limits);
  const bool opened = doc.open(result);
  if (!opened) {
    if (resultOut) *resultOut = result;
    return false;
  }
  Obj rootObj;
  Obj pagesRoot;
  if (!doc.load(doc.rootNum(), rootObj)) {
    result.xrefFailed = true;
    if (resultOut) *resultOut = result;
    return false;
  }
  pagesRoot = doc.resolveKey(rootObj, "Pages");
  // Pages are read as the tree is walked, one at a time, so a book of hundreds of pages never has all of
  // its page dictionaries in memory at once. The count in the root only feeds the progress bar.
  const Obj declared = doc.resolveKey(pagesRoot, "Count");
  const size_t declaredPages =
      declared.type == Obj::Int && declared.i > 0 ? static_cast<size_t>(std::min<int64_t>(declared.i, 100000)) : 0;
  size_t visits = 0;
  size_t index = 0;
  std::unordered_map<uint32_t, std::unique_ptr<Font>> fonts;
  const auto handlePage = [&](const Obj& node, const Obj& pageResources) {
    const size_t p = index++;
    PageText pageText;
    pageText.maxBytes = limits.maxPageText;
    const Obj contents = doc.resolveKey(node, "Contents");
    std::string content;
    const auto addStream = [&](const Obj& s) {
      const Obj stream = doc.resolve(s);
      std::string data;
      if (stream.type == Obj::Stream && doc.streamData(stream, data)) {
        content += data;
        content += '\n';
      }
    };
    if (contents.type == Obj::Stream) {
      std::string data;
      if (doc.streamData(contents, data)) content = std::move(data);
    } else if (contents.type == Obj::Arr) {
      for (const Obj& item : contents.a) addStream(item);
    } else if (const Obj* raw = node.get("Contents")) {
      addStream(*raw);
    }
    if (!content.empty()) {
      Interpreter interp(doc, fonts, pageText);
      interp.run(content, pageResources, 0);
    }
    TextOut out;
    out.text = layoutPage(pageText);
    // Filled-in form fields (a character sheet): "Name: value" after the page text.
    const Obj annots = doc.resolveKey(node, "Annots");
    if (annots.type == Obj::Arr) {
      std::string fields;
      for (const Obj& item : annots.a) {
        const Obj annot = doc.resolve(item);
        if (!annot.isDict() || fields.size() > 16384) continue;
        const Obj value = doc.resolveKey(annot, "V");
        if (value.type != Obj::Str || value.s.empty()) continue;
        Obj name = doc.resolveKey(annot, "T");
        if (name.type != Obj::Str) {
          const Obj parent = doc.resolveKey(annot, "Parent");
          if (parent.isDict()) name = doc.resolveKey(parent, "T");
        }
        const auto text = [](const std::string& raw) {
          if (raw.size() >= 2 && static_cast<uint8_t>(raw[0]) == 0xFE && static_cast<uint8_t>(raw[1]) == 0xFF) {
            std::string utf8;
            for (size_t k = 2; k + 1 < raw.size(); k += 2) {
              appendUtf8(utf8, (static_cast<uint8_t>(raw[k]) << 8) | static_cast<uint8_t>(raw[k + 1]));
            }
            return utf8;
          }
          std::string utf8;
          for (const char c : raw) appendUtf8(utf8, static_cast<uint8_t>(c));
          return utf8;
        };
        if (!fields.empty()) fields += '\n';
        if (name.type == Obj::Str && !name.s.empty()) fields += text(name.s) + ": ";
        fields += text(value.s);
      }
      if (!fields.empty()) {
        if (!out.text.empty()) out.text += "\n\n";
        out.text += fields;
      }
    }
    while (!out.text.empty() && (out.text.back() == ' ' || out.text.back() == '\n')) out.text.pop_back();
    if (!out.text.empty()) ++result.textPages;
    page(p, std::max(declaredPages, index), out.text);
  };
  size_t pagesFound = 0;
  walkPages(doc, pagesRoot, Obj(), handlePage, visits, pagesFound, 0, limits.maxPages);
  result.pages = index;
  if (resultOut) *resultOut = result;
  return true;
}

}  // namespace pdf
