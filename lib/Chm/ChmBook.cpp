#include "ChmBook.h"

#include <CodePage.h>
#include <HtmlBook.h>
#include <MobiHtmlFilter.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "Lzx.h"

namespace chm {
namespace {

constexpr size_t CHUNK = 1024;
constexpr size_t MAX_SYSTEM = 4096;  // of #SYSTEM; a title past it is not found
constexpr size_t MAX_TAG = 600;      // of a tag in the contents file; a longer one is dropped
constexpr size_t MAX_LOCAL = 512;    // of a page's path in the contents file
constexpr size_t MAX_HHC = 4;        // contents files remembered from the directory
constexpr uint32_t NO_FRAME = 0xFFFFFFFF;

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t le64(const uint8_t* p) { return le32(p) | (static_cast<uint64_t>(le32(p + 4)) << 32); }

char lower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

bool equalsNoCase(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}
bool endsWithNoCase(const std::string_view text, const std::string_view end) {
  return text.size() >= end.size() && equalsNoCase(text.substr(text.size() - end.size()), end);
}
bool startsWith(const std::string_view text, const std::string_view start) {
  return text.substr(0, start.size()) == start;
}

// A file's name as the directory has it ("/dir/page.htm"), letters lowered, as 32 bits (FNV-1a). Two names with
// the same number would be taken for each other; among 4096 names that is about one chance in 500.
uint32_t nameHash(const std::string_view name) {
  uint32_t hash = 2166136261u;
  for (const char c : name) hash = (hash ^ static_cast<uint8_t>(lower(c))) * 16777619u;
  return hash;
}

// The value of attribute `key` in the inside of a tag (after the tag's name).
std::string_view attribute(const std::string_view tag, const std::string_view key) {
  size_t pos = 0;
  while (pos < tag.size() && !isSpace(tag[pos])) pos++;  // the tag's name
  while (pos < tag.size()) {
    while (pos < tag.size() && (isSpace(tag[pos]) || tag[pos] == '/')) pos++;
    const size_t nameStart = pos;
    while (pos < tag.size() && !isSpace(tag[pos]) && tag[pos] != '=') pos++;
    const std::string_view name = tag.substr(nameStart, pos - nameStart);
    while (pos < tag.size() && isSpace(tag[pos])) pos++;
    std::string_view value;
    if (pos < tag.size() && tag[pos] == '=') {
      pos++;
      while (pos < tag.size() && isSpace(tag[pos])) pos++;
      if (pos < tag.size() && (tag[pos] == '"' || tag[pos] == '\'')) {
        const char quote = tag[pos++];
        const size_t start = pos;
        while (pos < tag.size() && tag[pos] != quote) pos++;
        value = tag.substr(start, pos - start);
        if (pos < tag.size()) pos++;
      } else {
        const size_t start = pos;
        while (pos < tag.size() && !isSpace(tag[pos])) pos++;
        value = tag.substr(start, pos - start);
      }
    }
    if (name.empty() && value.empty()) break;
    if (equalsNoCase(name, key)) return value;
  }
  return {};
}

const char* languageOf(const uint32_t lcid) {
  switch (lcid & 0x3FF) {  // the primary language of a Windows language id
    case 0x07:
      return "de";
    case 0x08:
      return "el";
    case 0x09:
      return "en";
    case 0x0A:
      return "es";
    case 0x0C:
      return "fr";
    case 0x10:
      return "it";
    case 0x19:
      return "ru";
    default:
      return "";
  }
}

// The code page pages with no named encoding are read with: Greek and the Cyrillic languages have their own;
// the rest is Utf8, which reads a byte that is not UTF-8 as Windows-1252.
codepage::Id pageOf(const uint32_t lcid) {
  switch (lcid & 0x3FF) {
    case 0x08:
      return codepage::Id::Cp1253;
    case 0x02:  // Bulgarian
    case 0x19:  // Russian
    case 0x22:  // Ukrainian
    case 0x23:  // Belarusian
      return codepage::Id::Cp1251;
    default:
      return codepage::Id::Utf8;
  }
}

struct Entry {
  uint32_t offset = 0;
  uint32_t length = 0;
  uint8_t section = 0;
  bool found = false;
};

struct Topic {
  uint32_t hash;
  uint32_t offset;
  uint32_t length;
  uint8_t section;
  uint8_t used;  // already written (it was in the contents file)
};

struct Ordered {
  uint32_t titleAt;  // in the title pool
  uint16_t topic;
  uint16_t titleLen;
};

class Archive {
 public:
  Archive(const Source& source, const Memory& memory) : src_(source), mem_(memory) {
    if (!mem_.alloc || !mem_.release) {
      mem_.alloc = malloc;
      mem_.release = free;
    }
  }
  ~Archive() {
    mem_.release(topics_);
    mem_.release(order_);
    mem_.release(pool_);
    mem_.release(frameIn_);
  }
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;

  Status open() {
    if (src_.size < 0x58) return Status::NotChm;
    uint8_t h[0x60] = {};
    if (!src_.readExact(0, h, src_.size < sizeof(h) ? 0x58 : sizeof(h))) return Status::IoError;
    if (memcmp(h, "ITSF", 4) != 0) return Status::NotChm;
    if (src_.size > MAX_FILE_SIZE) return Status::TooBig;
    const uint32_t version = le32(h + 4);
    if (version < 2 || version > 3) return Status::Unsupported;
    lcid_ = le32(h + 0x14);
    const uint64_t dirOff = le64(h + 0x48);
    const uint64_t dirLen = le64(h + 0x50);
    if (dirOff > src_.size || dirLen > src_.size - dirOff) return Status::Corrupt;
    const uint64_t content = version >= 3 ? le64(h + 0x58) : dirOff + dirLen;
    if (content > src_.size) return Status::Corrupt;
    contentOffset_ = static_cast<uint32_t>(content);

    // The directory: an ITSP header, then chunks of one size. The PMGL ones list the files, in name order.
    uint8_t d[0x54];
    if (dirLen < sizeof(d) || !src_.readExact(static_cast<uint32_t>(dirOff), d, sizeof(d))) return Status::Corrupt;
    if (memcmp(d, "ITSP", 4) != 0) return Status::Corrupt;
    const uint32_t headerLen = le32(d + 8);
    const uint32_t chunkSize = le32(d + 0x10);
    if (headerLen < sizeof(d) || headerLen > dirLen) return Status::Corrupt;
    if (chunkSize < 64 || chunkSize > MAX_CHUNK_SIZE) return Status::Unsupported;
    const uint64_t chunks = (dirLen - headerLen) / chunkSize;
    if (chunks > MAX_DIR_CHUNKS) return Status::TooBig;
    std::unique_ptr<uint8_t[]> chunk(new (std::nothrow) uint8_t[chunkSize]);
    if (!chunk) return Status::Oom;
    for (uint32_t c = 0; c < chunks; c++) {
      const uint32_t at = static_cast<uint32_t>(dirOff) + headerLen + c * chunkSize;
      if (!src_.readExact(at, chunk.get(), chunkSize)) return Status::IoError;
      if (memcmp(chunk.get(), "PMGL", 4) != 0) continue;  // an index chunk (PMGI), or nothing
      const uint32_t unused = le32(chunk.get() + 4);
      if (unused > chunkSize - 0x14) continue;
      const Status status = listing(chunk.get() + 0x14, chunkSize - 0x14 - unused);
      if (status != Status::Ok) return status;
    }

    budget_ = src_.size;
    compression();
    system();
    return Status::Ok;
  }

  size_t topicCount() const { return topicCount_; }
  bool compressedTopics() const { return compressedTopics_; }
  Status compressionStatus() const { return lzxStatus_; }
  uint32_t lcid() const { return lcid_; }
  const std::string& title() const { return title_; }

  Status write(xhtml::Out& out, const std::string_view title) {
    in_.reset(new (std::nothrow) uint8_t[CHUNK]);
    utf8_.reset(
        new (std::nothrow) char[CHUNK * codepage::Decoder::MAX_OUT_PER_BYTE + codepage::Decoder::MAX_PENDING_OUT]);
    if (!in_ || !utf8_) return Status::Oom;
    contents();
    out.head(CACHE_VERSION_TAG, title);
    for (size_t i = 0; i < orderCount_ && out.ok(); i++) {
      page(out, topics_[order_[i].topic], std::string_view(pool_ + order_[i].titleAt, order_[i].titleLen));
    }
    for (size_t i = 0; i < topicCount_ && out.ok(); i++) {
      if (!topics_[i].used) page(out, topics_[i], {});
    }
    if (!out.tail()) return Status::WriteError;
    if (pages_ == 0) return failure_ != Status::Ok ? failure_ : Status::NoPages;
    return cut_ || topicsCut_ ? Status::Truncated : Status::Ok;
  }

 private:
  // A number in the directory: 7 bits a byte, the top bit says another byte follows.
  static bool number(const uint8_t* data, const size_t size, size_t& pos, uint64_t& value) {
    value = 0;
    for (int i = 0; i < 10 && pos < size; i++) {
      const uint8_t b = data[pos++];
      value = (value << 7) | (b & 0x7F);
      if (!(b & 0x80)) return true;
    }
    return false;
  }

  Status listing(const uint8_t* data, const size_t size) {
    size_t pos = 0;
    while (pos < size) {
      uint64_t nameLen = 0;
      if (!number(data, size, pos, nameLen) || nameLen > size - pos) break;
      const std::string_view name(reinterpret_cast<const char*>(data + pos), static_cast<size_t>(nameLen));
      pos += static_cast<size_t>(nameLen);
      uint64_t section = 0;
      uint64_t offset = 0;
      uint64_t length = 0;
      if (!number(data, size, pos, section) || !number(data, size, pos, offset) || !number(data, size, pos, length)) {
        break;
      }
      if (section > 1 || offset > 0xFFFFFFFFu || length > 0xFFFFFFFFu) continue;
      const Entry entry{static_cast<uint32_t>(offset), static_cast<uint32_t>(length), static_cast<uint8_t>(section),
                        true};
      if (startsWith(name, "::")) {
        if (name == "::DataSpace/Storage/MSCompressed/Content") content_ = entry;
        if (name == "::DataSpace/Storage/MSCompressed/ControlData") control_ = entry;
        if (startsWith(name, "::DataSpace/Storage/MSCompressed/Transform/") &&
            endsWithNoCase(name, "/InstanceData/ResetTable")) {
          reset_ = entry;
        }
        continue;
      }
      if (name == "/#SYSTEM") system_ = entry;
      if (startsWith(name, "/#") || startsWith(name, "/$") || length == 0) continue;
      if (endsWithNoCase(name, ".hhc")) {
        if (hhcCount_ < MAX_HHC) {
          hhc_[hhcCount_] = entry;
          hhcHash_[hhcCount_++] = nameHash(name);
        }
        continue;
      }
      if (!endsWithNoCase(name, ".htm") && !endsWithNoCase(name, ".html")) continue;
      if (topicCount_ == MAX_TOPICS) {
        topicsCut_ = true;
        continue;
      }
      if (topicCount_ == topicCap_) {
        const size_t cap = topicCap_ == 0 ? 64 : topicCap_ * 2;
        auto* grown = static_cast<Topic*>(mem_.alloc(cap * sizeof(Topic)));
        if (!grown) return Status::Oom;
        if (topics_) memcpy(grown, topics_, topicCount_ * sizeof(Topic));
        mem_.release(topics_);
        topics_ = grown;
        topicCap_ = cap;
      }
      topics_[topicCount_++] = Topic{nameHash(name), entry.offset, entry.length, entry.section, 0};
      if (entry.section == 1) compressedTopics_ = true;
    }
    return Status::Ok;
  }

  // The MSCompressed section: ControlData says how it was compressed, the ResetTable where each frame of 32768
  // bytes starts in Content.
  void compression() {
    lzxStatus_ = Status::Corrupt;
    if (!content_.found || !control_.found || !reset_.found) return;
    if (content_.section != 0 || control_.section != 0 || reset_.section != 0) return;
    uint8_t c[0x18];
    if (control_.length < sizeof(c) || read(control_, 0, c, sizeof(c)) != Status::Ok) return;
    if (memcmp(c + 4, "LZXC", 4) != 0) return;
    lzxStatus_ = Status::Unsupported;
    const uint32_t version = le32(c + 8);
    uint64_t resetInterval = le32(c + 0x0C);
    uint64_t window = le32(c + 0x10);
    if (version == 2) {  // counted in units of 32768 bytes
      resetInterval *= lzx::FRAME;
      window *= lzx::FRAME;
    } else if (version != 1) {
      return;
    }
    windowBits_ = 0;
    for (unsigned b = lzx::MIN_WINDOW_BITS; b <= lzx::MAX_WINDOW_BITS; b++) {
      if (window == (1ull << b)) windowBits_ = b;
    }
    if (windowBits_ == 0 || resetInterval == 0 || resetInterval % lzx::FRAME != 0) return;
    // How many frames lie between two resets: the reset interval, taken as it is. The third number (called the
    // cache size, or the windows per reset) is not used. That is how 7-Zip reads it, checked with files where
    // using the third number would give another answer; the one real file looked at has 2, 2, 1.
    const uint64_t plain = resetInterval / lzx::FRAME;
    if (plain > 0xFFFF) return;
    resetFrames_ = static_cast<uint32_t>(plain);

    lzxStatus_ = Status::Corrupt;
    uint8_t r[0x28];
    if (reset_.length < sizeof(r) || read(reset_, 0, r, sizeof(r)) != Status::Ok) return;
    const uint32_t entries = le32(r + 4);
    tableAt_ = le32(r + 0x0C);
    const uint64_t plainSize = le64(r + 0x10);
    const uint64_t packedSize = le64(r + 0x18);
    if (le32(r + 8) != 8 || le64(r + 0x20) != lzx::FRAME || tableAt_ < sizeof(r) || tableAt_ > reset_.length) return;
    if (plainSize > 0xFFFFFFFFu || entries > (reset_.length - tableAt_) / 8) return;
    if (entries < (plainSize + lzx::FRAME - 1) / lzx::FRAME) return;
    plainSize_ = static_cast<uint32_t>(plainSize);
    packedSize_ = packedSize < content_.length ? static_cast<uint32_t>(packedSize) : content_.length;
    budget_ += plainSize_;
    lzxStatus_ = Status::Ok;
  }

  // #SYSTEM: a version, then records of a 16-bit code, a 16-bit length and the data.
  void system() {
    if (!system_.found || system_.length < 8) return;
    const uint32_t size = system_.length < MAX_SYSTEM ? system_.length : static_cast<uint32_t>(MAX_SYSTEM);
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[size]);
    if (!buf || read(system_, 0, buf.get(), size) != Status::Ok) return;
    const uint8_t* p = buf.get();
    std::string_view rawTitle;
    for (uint32_t pos = 4; size - pos >= 4;) {
      const uint32_t code = p[pos] | (p[pos + 1] << 8);
      const uint32_t len = p[pos + 2] | (p[pos + 3] << 8);
      pos += 4;
      if (len > size - pos) break;
      std::string_view text(reinterpret_cast<const char*>(p + pos), len);
      text = text.substr(0, text.find('\0'));
      if (code == 0 && !text.empty() && text.size() < MAX_LOCAL) {  // the contents file
        char path[MAX_LOCAL + 2];
        hhcWanted_ = nameHash(std::string_view(path, normalise(text, path)));
        hasHhcWanted_ = true;
      } else if (code == 3) {
        rawTitle = text.substr(0, MAX_TITLE);
      } else if (code == 4 && len >= 4) {
        lcid_ = le32(p + pos);  // the language the file was compiled with
      }
      pos += len;
    }
    if (!rawTitle.empty()) {
      char utf8[MAX_TITLE * codepage::Decoder::MAX_OUT_PER_BYTE + codepage::Decoder::MAX_PENDING_OUT];
      codepage::Decoder decoder(pageOf(lcid_));
      // No finish(): a title cut inside a character loses that character.
      title_.assign(utf8, decoder.feed(reinterpret_cast<const uint8_t*>(rawTitle.data()), rawTitle.size(), utf8));
      while (!title_.empty() && isSpace(title_.back())) title_.pop_back();
    }
  }

  // A page's path as the contents file writes it -> the directory's name for it, lowered: "/dir/page.htm".
  // `out` needs MAX_LOCAL + 2 bytes. Returns the length.
  static size_t normalise(std::string_view local, char* out) {
    const size_t its = local.rfind("::");  // "ms-its:book.chm::/page.htm"
    if (its != std::string_view::npos) local = local.substr(its + 2);
    local = local.substr(0, local.find('#'));
    while (startsWith(local, "./") || startsWith(local, ".\\")) local = local.substr(2);
    size_t len = 0;
    if (local.empty() || (local[0] != '/' && local[0] != '\\')) out[len++] = '/';
    for (size_t i = 0; i < local.size() && len < MAX_LOCAL + 1; i++) {
      char c = local[i];
      if (c == '%' && i + 2 < local.size() && hex(local[i + 1]) >= 0 && hex(local[i + 2]) >= 0) {
        c = static_cast<char>(hex(local[i + 1]) * 16 + hex(local[i + 2]));
        i += 2;
      }
      out[len++] = c == '\\' ? '/' : lower(c);
    }
    return len;
  }

  static int hex(const char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  // `n` bytes of a file inside the archive, from byte `at` of it.
  Status read(const Entry& entry, const uint32_t at, uint8_t* dst, uint32_t n) {
    if (at > entry.length || n > entry.length - at) return Status::Corrupt;
    if (entry.section == 0) {
      const uint64_t file = static_cast<uint64_t>(contentOffset_) + entry.offset + at;
      if (file > src_.size || !src_.readExact(static_cast<uint32_t>(file), dst, n)) return Status::Corrupt;
      return Status::Ok;
    }
    uint64_t pos = static_cast<uint64_t>(entry.offset) + at;
    while (n > 0) {
      const uint8_t* data = nullptr;
      uint32_t size = 0;
      if (pos >= plainSize_) return Status::Corrupt;
      const Status status = frame(static_cast<uint32_t>(pos / lzx::FRAME), data, size);
      if (status != Status::Ok) return status;
      const uint32_t in = static_cast<uint32_t>(pos % lzx::FRAME);
      if (in >= size) return Status::Corrupt;
      const uint32_t take = n < size - in ? n : size - in;
      memcpy(dst, data + in, take);
      dst += take;
      pos += take;
      n -= take;
    }
    return Status::Ok;
  }

  bool tableEntry(const uint32_t index, uint32_t& value) {
    uint8_t raw[8];
    if (read(reset_, tableAt_ + index * 8, raw, 8) != Status::Ok) return false;
    const uint64_t v = le64(raw);
    if (v > packedSize_) return false;
    value = static_cast<uint32_t>(v);
    return true;
  }

  // Frame `index` of the compressed section, decoded. The decoder only goes forward, so a frame before the one
  // it is at, or past the next reset, starts again from the reset before `index`.
  Status frame(const uint32_t index, const uint8_t*& data, uint32_t& size) {
    if (lzxStatus_ != Status::Ok) return lzxStatus_;
    if (!lzxReady_) {
      if (!frameIn_) frameIn_ = static_cast<uint8_t*>(mem_.alloc(lzx::MAX_FRAME_INPUT));
      if (!frameIn_ || !lzx_.init(windowBits_, mem_.alloc, mem_.release)) return lzxStatus_ = Status::Oom;
      lzxReady_ = true;
    }
    if (index == frameAt_) {
      data = frameData_;
      size = frameSize_;
      return Status::Ok;
    }
    const uint32_t resetAt = index - index % resetFrames_;
    uint32_t next = frameAt_ == NO_FRAME ? resetAt : frameAt_ + 1;
    if (frameAt_ == NO_FRAME || index < frameAt_ || resetAt > frameAt_) next = resetAt;
    frameAt_ = NO_FRAME;
    for (uint32_t f = next; f <= index; f++) {
      if (f % resetFrames_ == 0) lzx_.reset();
      uint32_t from = 0;
      uint32_t to = packedSize_;
      const bool last = static_cast<uint64_t>(f + 1) * lzx::FRAME >= plainSize_;
      if (!tableEntry(f, from) || (!last && !tableEntry(f + 1, to))) return Status::Corrupt;
      if (to < from || to - from > lzx::MAX_FRAME_INPUT) return Status::Corrupt;
      const Entry packed{content_.offset + from, to - from, 0, true};
      if (packed.offset < content_.offset || read(packed, 0, frameIn_, to - from) != Status::Ok) return Status::Corrupt;
      frameSize_ = last ? plainSize_ - f * lzx::FRAME : lzx::FRAME;
      const lzx::Result result = lzx_.decodeFrame(frameIn_, to - from, frameSize_, frameData_);
      if (result != lzx::Result::Ok) {
        return result == lzx::Result::Unsupported ? lzxStatus_ = Status::Unsupported : Status::Corrupt;
      }
    }
    frameAt_ = index;
    data = frameData_;
    size = frameSize_;
    return Status::Ok;
  }

  // The contents file (.hhc), an HTML list of <object> blocks with <param name="Name"> (the title) and
  // <param name="Local"> (the page): the pages it names, in its order, each once.
  void contents() {
    if (hhcCount_ == 0 || topicCount_ == 0) return;
    Entry hhc = hhc_[0];
    for (size_t i = 0; hasHhcWanted_ && i < hhcCount_; i++) {
      if (hhcHash_[i] == hhcWanted_) hhc = hhc_[i];
    }
    const size_t poolSize = hhc.length < MAX_TITLE_POOL ? hhc.length : MAX_TITLE_POOL;
    order_ = static_cast<Ordered*>(mem_.alloc(topicCount_ * sizeof(Ordered)));
    pool_ = static_cast<char*>(mem_.alloc(poolSize));
    std::unique_ptr<char[]> tag(new (std::nothrow) char[MAX_TAG]);
    if (!order_ || !pool_ || !tag) return;

    char title[MAX_TOC_TITLE];
    size_t titleLen = 0;
    bool hasTitle = false;
    char local[MAX_LOCAL + 2];
    size_t localLen = 0;
    size_t tagLen = 0;
    bool inTag = false;
    bool tagCut = false;
    char quote = 0;
    const uint32_t size = hhc.length < MAX_TOC_FILE ? hhc.length : MAX_TOC_FILE;
    for (uint32_t at = 0; at < size;) {
      const uint32_t n = size - at < CHUNK ? size - at : static_cast<uint32_t>(CHUNK);
      if (read(hhc, at, in_.get(), n) != Status::Ok) return;
      at += n;
      for (uint32_t i = 0; i < n; i++) {
        const char c = static_cast<char>(in_[i]);
        if (!inTag) {
          if (c == '<') {
            inTag = true;
            tagLen = 0;
            tagCut = false;
            quote = 0;
          }
          continue;
        }
        const bool comment = tagLen >= 3 && tag[0] == '!' && tag[1] == '-' && tag[2] == '-';
        bool ends = false;
        if (comment) {
          ends = c == '>' && tag[tagLen - 1] == '-' && tag[tagLen - 2] == '-';
          if (tagLen == MAX_TAG - 1) tagLen = 5;  // keep the "!--" and room to see the "--" before the '>'
        } else if (quote != 0) {
          if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
          quote = c;
        } else {
          ends = c == '>';
        }
        if (!ends) {
          if (tagLen < MAX_TAG - 1) {
            tag[tagLen++] = c;
          } else {
            tagCut = true;
          }
          continue;
        }
        inTag = false;
        if (tagCut || comment) continue;
        const std::string_view text(tag.get(), tagLen);
        size_t nameEnd = 0;
        while (nameEnd < text.size() && !isSpace(text[nameEnd])) nameEnd++;
        const std::string_view name = text.substr(0, nameEnd);
        if (equalsNoCase(name, "object")) {
          hasTitle = false;
          localLen = 0;
        } else if (equalsNoCase(name, "param")) {
          const std::string_view key = attribute(text, "name");
          const std::string_view value = attribute(text, "value");
          if (equalsNoCase(key, "Name") && !hasTitle) {
            titleLen = value.size() < MAX_TOC_TITLE ? value.size() : MAX_TOC_TITLE;
            if (titleLen > 0) memcpy(title, value.data(), titleLen);
            hasTitle = true;
          } else if (equalsNoCase(key, "Local") && !value.empty() && value.size() < MAX_LOCAL) {
            localLen = normalise(value, local);
          }
        } else if (equalsNoCase(name, "/object")) {
          if (localLen > 0) {
            const uint32_t hash = nameHash(std::string_view(local, localLen));
            for (size_t t = 0; t < topicCount_; t++) {
              if (topics_[t].hash != hash || topics_[t].used) continue;
              topics_[t].used = 1;
              Ordered ordered{static_cast<uint32_t>(poolUsed_), static_cast<uint16_t>(t), 0};
              if (hasTitle && titleLen <= poolSize - poolUsed_) {
                if (titleLen > 0) memcpy(pool_ + poolUsed_, title, titleLen);
                poolUsed_ += titleLen;
                ordered.titleLen = static_cast<uint16_t>(titleLen);
              }
              order_[orderCount_++] = ordered;
              break;
            }
          }
          hasTitle = false;
          localLen = 0;
        }
      }
    }
  }

  // One page through the tag filter, under its contents title.
  void page(xhtml::Out& out, const Topic& topic, const std::string_view title) {
    const auto toOut = [](void* c, const char* data, size_t size) {
      auto* o = static_cast<xhtml::Out*>(c);
      o->write(std::string_view(data, size));
      return o->ok();
    };
    const Entry entry{topic.offset, topic.length, topic.section, true};
    uint32_t n = entry.length < CHUNK ? entry.length : static_cast<uint32_t>(CHUNK);
    if (n > budget_) {
      cut_ = true;
      return;
    }
    Status status = read(entry, 0, in_.get(), n);
    if (status != Status::Ok) {
      cut_ = true;
      if (failure_ == Status::Ok) failure_ = status;
      return;
    }
    htmlbook::Info info;
    htmlbook::sniff(std::string_view(reinterpret_cast<const char*>(in_.get()), n), info);
    if (info.utf16) {
      cut_ = true;
      return;
    }
    const codepage::Id filePage = pageOf(lcid_);
    mobi::HtmlFilter filter(toOut, &out);
    if (!title.empty()) {
      codepage::Decoder titleDecoder(filePage);
      filter.feed("<h2>");
      filter.feed(std::string_view(
          utf8_.get(), titleDecoder.feed(reinterpret_cast<const uint8_t*>(title.data()), title.size(), utf8_.get())));
      filter.feed("</h2>");
    } else if (pages_ > 0) {
      out.write("<hr/>\n");
    }
    pages_++;
    codepage::Decoder decoder(info.named ? info.encoding : filePage);
    size_t skip = info.bomSize;
    uint32_t at = 0;
    for (;;) {
      budget_ -= n;
      filter.feed(std::string_view(utf8_.get(), decoder.feed(in_.get() + skip, n - skip, utf8_.get())));
      skip = 0;
      at += n;
      if (at >= entry.length || !out.ok()) break;
      n = entry.length - at < CHUNK ? entry.length - at : static_cast<uint32_t>(CHUNK);
      if (n > budget_) {
        cut_ = true;
        break;
      }
      status = read(entry, at, in_.get(), n);
      if (status != Status::Ok) {
        cut_ = true;
        break;
      }
    }
    filter.feed(std::string_view(utf8_.get(), decoder.finish(utf8_.get())));
    filter.finish();
    out.write("\n");
  }

  const Source& src_;
  Memory mem_;
  uint32_t contentOffset_ = 0;
  uint32_t lcid_ = 0;
  std::string title_;
  Entry system_;
  Entry content_;
  Entry control_;
  Entry reset_;
  Entry hhc_[MAX_HHC];
  uint32_t hhcHash_[MAX_HHC] = {};
  size_t hhcCount_ = 0;
  uint32_t hhcWanted_ = 0;
  bool hasHhcWanted_ = false;

  Topic* topics_ = nullptr;
  size_t topicCount_ = 0;
  size_t topicCap_ = 0;
  bool topicsCut_ = false;
  bool compressedTopics_ = false;
  Ordered* order_ = nullptr;
  size_t orderCount_ = 0;
  char* pool_ = nullptr;
  size_t poolUsed_ = 0;

  Status lzxStatus_ = Status::Corrupt;
  unsigned windowBits_ = 0;
  uint32_t resetFrames_ = 1;
  uint32_t tableAt_ = 0;
  uint32_t plainSize_ = 0;
  uint32_t packedSize_ = 0;
  lzx::Decoder lzx_;
  bool lzxReady_ = false;
  uint8_t* frameIn_ = nullptr;
  uint32_t frameAt_ = NO_FRAME;
  const uint8_t* frameData_ = nullptr;
  uint32_t frameSize_ = 0;

  std::unique_ptr<uint8_t[]> in_;
  std::unique_ptr<char[]> utf8_;
  uint64_t budget_ = 0;
  size_t pages_ = 0;
  bool cut_ = false;
  Status failure_ = Status::Ok;
};

Status check(const Archive& archive) {
  if (archive.topicCount() == 0) return Status::NoPages;
  if (archive.compressedTopics() && archive.compressionStatus() != Status::Ok) return archive.compressionStatus();
  return Status::Ok;
}

}  // namespace

const char* statusText(const Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::Truncated:
      return "some pages are missing or cut";
    case Status::IoError:
      return "read error";
    case Status::Oom:
      return "out of memory";
    case Status::NotChm:
      return "not a CHM file";
    case Status::Unsupported:
      return "a compression that is not read";
    case Status::TooBig:
      return "too big";
    case Status::Corrupt:
      return "damaged";
    case Status::NoPages:
      return "no pages inside";
    case Status::WriteError:
      return "write error";
  }
  return "?";
}

Status readInfo(const Source& source, Info& info, const Memory& memory) {
  std::unique_ptr<Archive> archive(new (std::nothrow) Archive(source, memory));
  if (!archive) return Status::Oom;
  Status status = archive->open();
  if (status == Status::Ok) status = check(*archive);
  if (status != Status::Ok) return status;
  info.title = archive->title();
  info.language = languageOf(archive->lcid());
  return Status::Ok;
}

Status streamHtml(const Source& source, const std::string_view title, const xhtml::Emit emit, void* emitCtx,
                  const Memory& memory) {
  std::unique_ptr<Archive> archive(new (std::nothrow) Archive(source, memory));
  std::unique_ptr<xhtml::Out> out(new (std::nothrow) xhtml::Out(emit, emitCtx));
  if (!archive || !out) return Status::Oom;
  Status status = archive->open();
  if (status == Status::Ok) status = check(*archive);
  if (status != Status::Ok) return status;
  return archive->write(*out, title.empty() ? std::string_view(archive->title()) : title);
}

}  // namespace chm
