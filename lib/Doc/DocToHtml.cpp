#include "DocToHtml.h"

#include <CodePage.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace doc {
namespace {

constexpr uint32_t END_OF_CHAIN = 0xFFFFFFFE;
constexpr uint32_t NO_STREAM = 0xFFFFFFFF;
constexpr uint32_t DIR_ENTRY_SIZE = 128;
constexpr uint32_t MINI_SECTOR = 64;
constexpr uint32_t MAX_DIR_SECTORS = 4096;
constexpr size_t TEXT_CHUNK = 512;
constexpr size_t MAX_SUMMARY = 4096;  // of the SummaryInformation stream; a title past it is not found

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// A stream as runs of file bytes, in stream order.
class Stream {
 public:
  Stream() = default;
  ~Stream() { free(extents_); }
  Stream(const Stream&) = delete;
  Stream& operator=(const Stream&) = delete;

  Status add(const uint32_t file, const uint32_t len) {
    if (count_ > 0 && extents_[count_ - 1].file + extents_[count_ - 1].len == file) {
      extents_[count_ - 1].len += len;
      size_ += len;
      return Status::Ok;
    }
    if (count_ == cap_) {
      if (cap_ >= MAX_EXTENTS) return Status::TooBig;
      const size_t cap = cap_ == 0 ? 8 : cap_ * 2;
      auto* grown = static_cast<Extent*>(realloc(extents_, cap * sizeof(Extent)));
      if (!grown) return Status::Oom;
      extents_ = grown;
      cap_ = cap;
    }
    extents_[count_++] = Extent{size_, file, len};
    size_ += len;
    return Status::Ok;
  }

  void trim(const uint32_t size) {
    if (size < size_) size_ = size;
  }
  uint32_t size() const { return size_; }

  // Where stream byte `off` is in the file, and how many stream bytes follow it there without a break.
  bool map(const uint32_t off, uint32_t& file, uint32_t& run) const {
    if (off >= size_) return false;
    size_t lo = 0;
    size_t hi = count_;
    while (hi - lo > 1) {
      const size_t mid = lo + (hi - lo) / 2;
      if (extents_[mid].start <= off)
        lo = mid;
      else
        hi = mid;
    }
    const Extent& e = extents_[lo];
    const uint32_t in = off - e.start;
    if (in >= e.len) return false;
    file = e.file + in;
    run = e.len - in;
    if (run > size_ - off) run = size_ - off;
    return true;
  }

 private:
  struct Extent {
    uint32_t start;  // in the stream
    uint32_t file;
    uint32_t len;
  };
  Extent* extents_ = nullptr;
  size_t count_ = 0;
  size_t cap_ = 0;
  uint32_t size_ = 0;
};

struct DirEntry {
  uint32_t start = 0;
  uint32_t size = 0;
  bool found = false;
  bool tooBig = false;
};

// The OLE2 compound file ([MS-CFB]): header, DIFAT, FAT, MiniFAT, directory.
class Cfb {
 public:
  explicit Cfb(const Source& source) : src_(source) {}

  Status open() {
    if (src_.size < 512) return Status::NotDoc;
    uint8_t h[512];
    if (!src_.readExact(0, h, sizeof(h))) return Status::IoError;
    static const uint8_t magic[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    if (memcmp(h, magic, sizeof(magic)) != 0) return Status::NotDoc;
    if (src_.size > MAX_FILE_SIZE) return Status::TooBig;
    major_ = le16(h + 0x1A);
    sectorShift_ = le16(h + 0x1E);
    if (le16(h + 0x1C) != 0xFFFE || (sectorShift_ != 9 && sectorShift_ != 12) || le16(h + 0x20) != 6) {
      return Status::Corrupt;
    }
    sectorSize_ = 1u << sectorShift_;
    // The header takes the place of one sector; a last sector cut short still counts.
    sectorCount_ = ((src_.size + sectorSize_ - 1) >> sectorShift_) - 1;
    fatCount_ = le32(h + 0x2C);
    const uint32_t firstDir = le32(h + 0x30);
    miniCutoff_ = le32(h + 0x38);
    firstMiniFat_ = le32(h + 0x3C);
    uint32_t difat = le32(h + 0x44);
    if (fatCount_ == 0 || miniCutoff_ > 4096) return Status::Corrupt;
    if (fatCount_ > MAX_FAT_SECTORS) return Status::TooBig;

    fatSectors_.reset(new (std::nothrow) uint32_t[fatCount_]);
    if (!fatSectors_) return Status::Oom;
    uint32_t filled = 0;
    for (; filled < fatCount_ && filled < 109; filled++) fatSectors_[filled] = le32(h + 0x4C + filled * 4);
    const uint32_t perSector = sectorSize_ / 4;
    while (filled < fatCount_) {  // each DIFAT sector adds entries, so this ends
      if (difat >= sectorCount_) return Status::Corrupt;
      const uint32_t base = sectorOffset(difat);
      for (uint32_t i = 0; i + 1 < perSector && filled < fatCount_; i++) {
        if (!u32At(base + i * 4, fatSectors_[filled++])) return Status::Corrupt;
      }
      if (!u32At(base + (perSector - 1) * 4, difat)) return Status::Corrupt;
    }
    for (uint32_t i = 0; i < fatCount_; i++) {
      if (fatSectors_[i] >= sectorCount_) return Status::Corrupt;
    }

    Status status = chain(firstDir, 0, false, dir_);
    if (status != Status::Ok) return status;
    uint8_t root[DIR_ENTRY_SIZE];
    if (!read(dir_, 0, root, sizeof(root))) return Status::Corrupt;
    rootStart_ = le32(root + 0x74);
    rootSize_ = le32(root + 0x78);
    rootChild_ = le32(root + 0x4C);
    return Status::Ok;
  }

  // The streams named `names` (ASCII) among the root storage's children; a stream inside another storage (an
  // embedded object can hold its own WordDocument) is not looked at.
  Status find(const char* const* names, DirEntry* out, const size_t count) const {
    uint32_t stack[32];
    size_t sp = 0;
    stack[sp++] = rootChild_;
    uint32_t visited = 0;
    while (sp > 0 && visited < MAX_DIR_ENTRIES) {
      const uint32_t id = stack[--sp];
      if (id == NO_STREAM || id >= dir_.size() / DIR_ENTRY_SIZE) continue;
      uint8_t e[DIR_ENTRY_SIZE];
      if (!read(dir_, id * DIR_ENTRY_SIZE, e, sizeof(e))) return Status::Corrupt;
      visited++;
      if (e[0x42] == 2) {
        const uint16_t nameBytes = le16(e + 0x40);  // counts the closing zero
        for (size_t n = 0; n < count; n++) {
          const size_t len = strlen(names[n]);
          if (out[n].found || nameBytes != (len + 1) * 2) continue;
          bool same = true;
          for (size_t i = 0; i < len && same; i++) {
            same = e[i * 2 + 1] == 0 && e[i * 2] == static_cast<uint8_t>(names[n][i]);
          }
          if (!same) continue;
          out[n].found = true;
          out[n].start = le32(e + 0x74);
          out[n].size = le32(e + 0x78);
          out[n].tooBig = major_ >= 4 && le32(e + 0x7C) != 0;
        }
      }
      // A full stack drops the push: the entries below it are not seen (a tree that deep is not a balanced one).
      if (sp < 32) stack[sp++] = le32(e + 0x44);
      if (sp < 32) stack[sp++] = le32(e + 0x48);
    }
    return Status::Ok;
  }

  Status openStream(const DirEntry& entry, Stream& out) {
    if (entry.tooBig) return Status::TooBig;
    if (entry.size > src_.size) return Status::Corrupt;  // also bounds the walk along the chain
    if (entry.size >= miniCutoff_) return chain(entry.start, entry.size, true, out);
    Status status = ensureMini();
    if (status != Status::Ok) return status;
    const uint32_t sectors = (entry.size + MINI_SECTOR - 1) / MINI_SECTOR;
    uint32_t m = entry.start;
    for (uint32_t k = 0; k < sectors; k++) {
      uint32_t file = 0;
      uint32_t run = 0;
      if (m > 0x00FFFFFF || !miniStream_.map(m * MINI_SECTOR, file, run) || run < MINI_SECTOR) return Status::Corrupt;
      status = out.add(file, MINI_SECTOR);
      if (status != Status::Ok) return status;
      uint8_t next[4];
      if (!read(miniFat_, m * 4, next, 4)) return Status::Corrupt;
      m = le32(next);
    }
    out.trim(entry.size);
    return Status::Ok;
  }

  bool read(const Stream& s, uint32_t off, uint8_t* buf, size_t n) const {
    while (n > 0) {
      uint32_t file = 0;
      uint32_t run = 0;
      if (!s.map(off, file, run)) return false;
      const size_t take = run < n ? run : n;
      if (!src_.readExact(file, buf, take)) return false;
      off += static_cast<uint32_t>(take);
      buf += take;
      n -= take;
    }
    return true;
  }

 private:
  uint32_t sectorOffset(const uint32_t sector) const { return (sector + 1) << sectorShift_; }

  // Four bytes of the file through a 512-byte window (the FAT and DIFAT are read an entry at a time).
  bool u32At(const uint32_t fileOff, uint32_t& value) const {
    const uint32_t base = fileOff & ~511u;
    if (!windowValid_ || base != windowBase_) {
      windowValid_ = false;
      const uint32_t left = base < src_.size ? src_.size - base : 0;
      windowLen_ = left < 512 ? left : 512;
      if (windowLen_ == 0 || !src_.readExact(base, window_, windowLen_)) return false;
      windowBase_ = base;
      windowValid_ = true;
    }
    const uint32_t in = fileOff - base;
    if (in + 4 > windowLen_) return false;
    value = le32(window_ + in);
    return true;
  }

  bool fatEntry(const uint32_t sector, uint32_t& next) const {
    const uint32_t perSector = sectorSize_ / 4;
    const uint32_t index = sector / perSector;
    if (index >= fatCount_) return false;
    return u32At(sectorOffset(fatSectors_[index]) + (sector % perSector) * 4, next);
  }

  // A chain of sectors in the FAT. With a known size it must be that long; without one it runs to its end.
  Status chain(uint32_t sector, const uint32_t size, const bool sizeKnown, Stream& out) const {
    const uint32_t want = sizeKnown ? ((size + sectorSize_ - 1) >> sectorShift_) : MAX_DIR_SECTORS;
    for (uint32_t n = 0; n < want; n++) {
      if (sector >= sectorCount_) {
        if (!sizeKnown && sector == END_OF_CHAIN) break;
        return Status::Corrupt;
      }
      const Status status = out.add(sectorOffset(sector), sectorSize_);
      if (status != Status::Ok) return status;
      if (!fatEntry(sector, sector)) return Status::Corrupt;
    }
    if (sizeKnown) out.trim(size);
    return Status::Ok;
  }

  Status ensureMini() {
    if (miniReady_) return miniStatus_;
    miniReady_ = true;
    miniStatus_ = chain(rootStart_, rootSize_, true, miniStream_);
    if (miniStatus_ == Status::Ok) miniStatus_ = chain(firstMiniFat_, 0, false, miniFat_);
    return miniStatus_;
  }

  const Source& src_;
  uint16_t major_ = 3;
  uint32_t sectorShift_ = 9;
  uint32_t sectorSize_ = 512;
  uint32_t sectorCount_ = 0;
  uint32_t fatCount_ = 0;
  uint32_t miniCutoff_ = 4096;
  uint32_t firstMiniFat_ = END_OF_CHAIN;
  uint32_t rootStart_ = END_OF_CHAIN;
  uint32_t rootSize_ = 0;
  uint32_t rootChild_ = NO_STREAM;
  std::unique_ptr<uint32_t[]> fatSectors_;
  Stream dir_;
  Stream miniStream_;
  Stream miniFat_;
  bool miniReady_ = false;
  Status miniStatus_ = Status::Ok;
  mutable uint8_t window_[512] = {};
  mutable uint32_t windowBase_ = 0;
  mutable uint32_t windowLen_ = 0;
  mutable bool windowValid_ = false;
};

// Word's characters -> paragraphs of escaped text.
class TextWriter {
 public:
  explicit TextWriter(xhtml::Out& out) : out_(out) {}

  // One UTF-16 code unit of the document text.
  void unit(const uint16_t u) {
    if (u >= 0xD800 && u <= 0xDBFF) {
      high_ = u;
      return;
    }
    uint32_t cp = u;
    if (u >= 0xDC00 && u <= 0xDFFF) {
      if (high_ == 0) return;
      cp = 0x10000 + ((static_cast<uint32_t>(high_) - 0xD800) << 10) + (u - 0xDC00);
    }
    high_ = 0;
    character(cp);
  }

  // The end of a part of the document (the main text, the footnotes): open fields are forgotten.
  void endPart() {
    closeParagraph();
    fieldDepth_ = 0;
    fieldOverflow_ = 0;
    hidden_ = 0;
    high_ = 0;
  }

  void rule() { out_.write("<hr/>\n"); }
  bool ok() const { return out_.ok(); }
  bool wroteText() const { return wroteText_; }

 private:
  void character(const uint32_t cp) {
    // Fields: 0x13 instruction 0x14 result 0x15. Only the result is shown; a field with no 0x14 shows nothing.
    if (cp == 0x13) {
      if (fieldDepth_ < MAX_FIELD_DEPTH) {
        inInstruction_[fieldDepth_++] = true;
      } else {
        fieldOverflow_++;
      }
      hidden_++;
      return;
    }
    if (cp == 0x14) {
      if (fieldOverflow_ == 0 && fieldDepth_ > 0 && inInstruction_[fieldDepth_ - 1]) {
        inInstruction_[fieldDepth_ - 1] = false;
        hidden_--;
      }
      return;
    }
    if (cp == 0x15) {
      if (fieldOverflow_ > 0) {
        fieldOverflow_--;
        hidden_--;
      } else if (fieldDepth_ > 0) {
        if (inInstruction_[--fieldDepth_]) hidden_--;
      }
      return;
    }
    if (hidden_ > 0) return;

    const bool afterCell = cellJustEnded_;
    cellJustEnded_ = false;
    switch (cp) {
      case 0x0D:  // paragraph mark
      case 0x0C:  // page or section break
        closeParagraph();
        return;
      case 0x07:  // the end of a table cell; straight after another one, the end of the row
        if (afterCell) {
          closeParagraph();
        } else {
          cellPending_ = paraOpen_;
          cellJustEnded_ = true;
        }
        return;
      case 0x0B:  // line break
        if (paraOpen_) out_.write("<br/>");
        return;
      case 0x09:
        if (paraOpen_) out_.write(" ");
        return;
      case 0x1E:  // non-breaking hyphen
        text('-');
        return;
      default:
        break;
    }
    // The rest below a space are marks with no text: 0x01 a picture, 0x02 a note number, 0x05 a comment, 0x08 a
    // drawing, 0x1F an optional hyphen.
    if (cp < 0x20 || !codepage::xmlChar(cp)) return;
    text(cp);
  }

  void text(const uint32_t cp) {
    if (!paraOpen_) {
      if (cp == ' ' || cp == 0xA0) return;  // no paragraph of spaces only
      out_.write("<p>");
      paraOpen_ = true;
    }
    if (cellPending_) {
      out_.write(" | ");
      cellPending_ = false;
    }
    char utf8[4];
    const size_t n = codepage::encodeUtf8(cp, utf8);
    out_.escaped(std::string_view(utf8, n));
    wroteText_ = true;
  }

  void closeParagraph() {
    if (paraOpen_) out_.write("</p>\n");
    paraOpen_ = false;
    cellPending_ = false;
    cellJustEnded_ = false;
  }

  xhtml::Out& out_;
  bool paraOpen_ = false;
  bool cellPending_ = false;
  bool cellJustEnded_ = false;
  bool wroteText_ = false;
  uint16_t high_ = 0;
  bool inInstruction_[MAX_FIELD_DEPTH] = {};
  size_t fieldDepth_ = 0;
  uint32_t fieldOverflow_ = 0;
  uint32_t hidden_ = 0;
};

const char* languageOf(const uint16_t lid) {
  switch (lid & 0x3FF) {  // the primary language of a Windows language id
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

class Document {
 public:
  explicit Document(const Source& source) : cfb_(source) {}

  Status open() {
    Status status = cfb_.open();
    if (status != Status::Ok) return status;
    static const char* const names[4] = {"WordDocument", "0Table", "1Table", "\005SummaryInformation"};
    DirEntry entries[4];
    status = cfb_.find(names, entries, 4);
    if (status != Status::Ok) return status;
    if (!entries[0].found) return Status::NotDoc;
    status = cfb_.openStream(entries[0], word_);
    if (status != Status::Ok) return status;

    // The FIB ([MS-DOC] 2.5): FibBase, then csw / fibRgW, cslw / fibRgLw, cbRgFcLcb / fibRgFcLcb.
    uint8_t fib[0x1AA];
    if (word_.size() < 0x44 || !cfb_.read(word_, 0, fib, 0x44)) return Status::NotDoc;
    const uint16_t ident = le16(fib);
    if (ident == 0xA5DC) return Status::Unsupported;  // Word 6 / 95
    if (ident != 0xA5EC) return Status::NotDoc;
    const uint16_t flags = le16(fib + 0x0A);
    if (flags & 0x0100) return Status::Encrypted;  // fEncrypted: RC4, or XOR when fObfuscated is set too
    lid_ = le16(fib + 0x06);
    if (word_.size() < sizeof(fib) || !cfb_.read(word_, 0, fib, sizeof(fib))) return Status::Unsupported;
    if (le16(fib + 0x20) != 14 || le16(fib + 0x3E) != 22 || le16(fib + 0x98) < 34) return Status::Unsupported;
    ccpText_ = le32(fib + 0x4C);
    ccpFtn_ = le32(fib + 0x50);
    const uint32_t ccpHdd = le32(fib + 0x54);
    const uint32_t ccpMcr = le32(fib + 0x58);
    const uint32_t ccpAtn = le32(fib + 0x5C);
    ccpEdn_ = le32(fib + 0x60);
    const uint32_t ccps[6] = {ccpText_, ccpFtn_, ccpHdd, ccpMcr, ccpAtn, ccpEdn_};
    uint32_t sum = 0;
    for (const uint32_t ccp : ccps) {
      if (ccp > 0x3FFFFFFF - sum) return Status::Corrupt;
      sum += ccp;
    }
    ednStart_ = sum - ccpEdn_;
    const uint32_t fcClx = le32(fib + 0x1A2);
    const uint32_t lcbClx = le32(fib + 0x1A6);

    const DirEntry& table = entries[(flags & 0x0200) ? 2 : 1];  // fWhichTblStm
    if (!table.found) return Status::Corrupt;
    status = cfb_.openStream(table, table_);
    if (status != Status::Ok) return status;
    if (lcbClx == 0 || fcClx > table_.size() || lcbClx > table_.size() - fcClx) return Status::Corrupt;

    // The Clx: any number of Prc (0x01, a 16-bit size, data), then the Pcdt (0x02, a 32-bit size, the PlcPcd).
    uint32_t pos = fcClx;
    const uint32_t end = fcClx + lcbClx;
    for (;;) {  // each turn moves pos on by 3 bytes or more, so this ends
      uint8_t head[5];
      if (end - pos < 5 || !cfb_.read(table_, pos, head, 5)) return Status::Corrupt;
      if (head[0] == 1) {
        const uint32_t skip = 3u + le16(head + 1);
        if (skip > end - pos) return Status::Corrupt;
        pos += skip;
        continue;
      }
      if (head[0] != 2) return Status::Corrupt;
      const uint32_t lcb = le32(head + 1);
      pos += 5;
      if (lcb < 16 || lcb > end - pos) return Status::Corrupt;
      pieces_ = (lcb - 4) / 12;
      if (pieces_ > MAX_PIECES) return Status::TooBig;
      plc_ = pos;
      break;
    }

    if (entries[3].found && cfb_.openStream(entries[3], summary_) != Status::Ok) summaryBad_ = true;
    return Status::Ok;
  }

  void info(Info& info) {
    info.language = languageOf(lid_);
    if (summaryBad_ || summary_.size() < 48) return;
    const size_t size = summary_.size() < MAX_SUMMARY ? summary_.size() : MAX_SUMMARY;
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[size]);
    if (!buf || !cfb_.read(summary_, 0, buf.get(), size)) return;
    const uint8_t* p = buf.get();
    // A property set stream ([MS-OLEPS]): byte order, ..., the count of sets at 24, the first set's offset at 44.
    if (le16(p) != 0xFFFE || le32(p + 24) < 1) return;
    const uint32_t set = le32(p + 44);
    if (set > size || size - set < 8) return;
    uint32_t count = le32(p + set + 4);
    if (count > (size - set - 8) / 8) count = static_cast<uint32_t>((size - set - 8) / 8);
    codepage::Id page = codepage::Id::Cp1252;
    for (int pass = 0; pass < 2; pass++) {  // the code page (property 1) first, then the strings
      for (uint32_t i = 0; i < count; i++) {
        const uint32_t id = le32(p + set + 8 + i * 8);
        const uint32_t off = le32(p + set + 12 + i * 8);
        if (off > size - set || size - set - off < 8) continue;
        const uint8_t* prop = p + set + off;
        const size_t left = size - set - off - 8;
        const uint32_t type = le32(prop) & 0xFFFF;
        if (pass == 0) {
          if (id == 1 && type == 2) codepage::fromWindowsNumber(le16(prop + 4), page);
          continue;
        }
        if (id != 2 && id != 4) continue;
        std::string& target = id == 2 ? info.title : info.author;
        uint32_t len = le32(prop + 4);
        if (type == 30) {  // VT_LPSTR: bytes in the set's code page
          if (len > left) len = static_cast<uint32_t>(left);
          if (len > MAX_INFO_TEXT) len = MAX_INFO_TEXT;
          char utf8[MAX_INFO_TEXT * codepage::Decoder::MAX_OUT_PER_BYTE + codepage::Decoder::MAX_PENDING_OUT];
          codepage::Decoder decoder(page);
          uint32_t n = 0;
          while (n < len && prop[8 + n] != 0) n++;
          target.assign(utf8, decoder.feed(prop + 8, n, utf8));  // no finish(): a cut character is dropped
        } else if (type == 31) {                                 // VT_LPWSTR: UTF-16 code units
          if (len > left / 2) len = static_cast<uint32_t>(left / 2);
          target.clear();
          for (uint32_t n = 0; n < len && target.size() + 3 <= MAX_INFO_TEXT; n++) {
            const uint16_t u = le16(prop + 8 + n * 2);
            if (u == 0) break;
            if (u < 0x20 || (u >= 0xD800 && u <= 0xDFFF)) continue;  // a title outside the BMP loses those letters
            char utf8[4];
            target.append(utf8, codepage::encodeUtf8(u, utf8));
          }
        }
        while (!target.empty() && static_cast<unsigned char>(target.back()) <= ' ') target.pop_back();
        size_t lead = 0;
        while (lead < target.size() && static_cast<unsigned char>(target[lead]) <= ' ') lead++;
        target.erase(0, lead);
        for (char& c : target) {
          if (static_cast<unsigned char>(c) < ' ') c = ' ';
        }
      }
    }
  }

  // The main text, then the footnotes and the endnotes after a rule each.
  // With `firstLetterOnly` it stops at the first character that shows (the check for a document with no text).
  Status text(TextWriter& writer, const bool firstLetterOnly = false) {
    firstLetterOnly_ = firstLetterOnly;
    budget_ = word_.size();
    Status status = range(0, ccpText_, writer);
    writer.endPart();
    if (status == Status::Ok && ccpFtn_ > 1) {
      writer.rule();
      status = range(ccpText_, ccpText_ + ccpFtn_, writer);
      writer.endPart();
    }
    if (status == Status::Ok && ccpEdn_ > 1) {
      writer.rule();
      status = range(ednStart_, ednStart_ + ccpEdn_, writer);
      writer.endPart();
    }
    return status;
  }

 private:
  // The characters [from, to) through the piece table ([MS-DOC] 2.8.35 PlcPcd, 2.9.177 Pcd, 2.9.73 FcCompressed).
  Status range(const uint32_t from, const uint32_t to, TextWriter& writer) {
    uint8_t buf[TEXT_CHUNK];
    uint8_t cps[8];
    for (uint32_t i = 0; i < pieces_; i++) {
      if (!cfb_.read(table_, plc_ + i * 4, cps, 8)) return Status::Truncated;
      const uint32_t cpStart = le32(cps);
      const uint32_t cpEnd = le32(cps + 4);
      if (cpEnd < cpStart) return Status::Truncated;
      if (cpEnd <= from) continue;
      if (cpStart >= to) break;
      uint8_t pcd[8];
      if (!cfb_.read(table_, plc_ + (pieces_ + 1) * 4 + i * 8, pcd, 8)) return Status::Truncated;
      const uint32_t fc = le32(pcd + 2);
      const bool compressed = (fc & 0x40000000) != 0;  // 8-bit characters at fc / 2, else UTF-16LE at fc
      const uint32_t first = cpStart > from ? cpStart : from;
      const uint32_t last = cpEnd < to ? cpEnd : to;
      const uint32_t width = compressed ? 1 : 2;
      uint32_t at = compressed ? (fc & 0x3FFFFFFF) / 2 : (fc & 0x3FFFFFFF);
      at += (first - cpStart) * width;  // below 2^31: both are under 2^30
      uint32_t bytes = (last - first) * width;
      while (bytes > 0) {
        const uint32_t take = bytes < TEXT_CHUNK ? bytes : static_cast<uint32_t>(TEXT_CHUNK);
        if (firstLetterOnly_ && writer.wroteText()) return Status::Ok;
        if (!writer.ok()) return Status::WriteError;
        if (take > budget_ || !cfb_.read(word_, at, buf, take)) return Status::Truncated;
        budget_ -= take;
        if (compressed) {
          for (uint32_t k = 0; k < take; k++) {
            // [MS-DOC] FcCompressed: the byte is the character, but for 0x82..0x9F, which read as Windows-1252.
            const uint8_t b = buf[k];
            writer.unit(b >= 0x80 && b <= 0x9F ? codepage::toUnicode(codepage::Id::Cp1252, b) : b);
          }
        } else {
          for (uint32_t k = 0; k + 1 < take; k += 2) writer.unit(le16(buf + k));
        }
        at += take;
        bytes -= take;
      }
    }
    return Status::Ok;
  }

  Cfb cfb_;
  Stream word_;
  Stream table_;
  Stream summary_;
  bool summaryBad_ = false;
  uint16_t lid_ = 0;
  uint32_t ccpText_ = 0;
  uint32_t ccpFtn_ = 0;
  uint32_t ccpEdn_ = 0;
  uint32_t ednStart_ = 0;
  uint32_t plc_ = 0;
  uint32_t pieces_ = 0;
  uint32_t budget_ = 0;
  bool firstLetterOnly_ = false;
};

}  // namespace

const char* statusText(const Status status) {
  switch (status) {
    case Status::Ok:
      return "ok";
    case Status::Truncated:
      return "the text is cut short";
    case Status::IoError:
      return "read error";
    case Status::Oom:
      return "out of memory";
    case Status::NotDoc:
      return "not a Word document";
    case Status::Unsupported:
      return "older than Word 97";
    case Status::Encrypted:
      return "password-protected";
    case Status::TooBig:
      return "too big or too fragmented";
    case Status::Corrupt:
      return "damaged";
    case Status::WriteError:
      return "write error";
    case Status::NoText:
      return "no text in it";
  }
  return "?";
}

Status readInfo(const Source& source, Info& info) {
  std::unique_ptr<Document> document(new (std::nothrow) Document(source));
  if (!document) return Status::Oom;
  Status status = document->open();
  if (status == Status::Ok) {
    // Is there anything to read? The text is run into nothing, up to its first letter.
    xhtml::Out nothing([](void*, const char*, size_t) { return true; }, nullptr);
    TextWriter writer(nothing);
    status = document->text(writer, true);
    if (status == Status::Ok || status == Status::Truncated) status = writer.wroteText() ? Status::Ok : Status::NoText;
  }
  if (status != Status::Ok && source.ioFailed) return Status::IoError;
  if (status == Status::Ok) document->info(info);
  return source.ioFailed ? Status::IoError : status;
}

Status streamHtml(const Source& source, const std::string_view title, const xhtml::Emit emit, void* emitCtx) {
  std::unique_ptr<Document> document(new (std::nothrow) Document(source));
  std::unique_ptr<xhtml::Out> out(new (std::nothrow) xhtml::Out(emit, emitCtx));
  if (!document || !out) return Status::Oom;
  Status status = document->open();
  if (status != Status::Ok) return source.ioFailed ? Status::IoError : status;
  out->head(CACHE_VERSION_TAG, title);
  TextWriter writer(*out);
  status = document->text(writer);
  if (!out->tail()) return out->tooBig() ? Status::TooBig : Status::WriteError;
  // A read that failed is the card's doing, not the file's: the cut page is not the book.
  if (source.ioFailed) return Status::IoError;
  if (!writer.wroteText()) return Status::NoText;
  return status;
}

}  // namespace doc
