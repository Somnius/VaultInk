#include "MobiFormat.h"

#include <cstring>
#include <memory>

#include "MobiHtmlFilter.h"

namespace mobi {
namespace {

constexpr uint32_t PDB_HEADER_SIZE = 78;
constexpr uint32_t PDB_RECORD_ENTRY = 8;
constexpr size_t RECORD0_MAX = 4096 + 2048;  // PalmDOC + MOBI header + EXTH + full name

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

bool readExact(const Reader& reader, uint32_t offset, uint8_t* buf, size_t size) {
  size_t got = 0;
  while (got < size) {
    const int n = reader.readAt(reader.ctx, offset + static_cast<uint32_t>(got), buf + got, size - got);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

// Reads one record (clamped to `cap`) into `buf`; returns its length or 0.
size_t readRecord(const Reader& reader, uint16_t index, uint8_t* buf, size_t cap) {
  uint32_t offset, length;
  if (!recordSpan(reader, index, offset, length)) return 0;
  if (length > cap) length = static_cast<uint32_t>(cap);
  if (!readExact(reader, offset, buf, length)) return 0;
  return length;
}

// cp1252 0x80..0x9F -> Unicode (0 = undefined; emitted as U+FFFD).
constexpr uint16_t CP1252_HIGH[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                                      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

std::string exthString(const uint8_t* data, uint32_t len, uint32_t encoding) {
  std::string s;
  if (encoding == ENCODING_UTF8) {
    s.assign(reinterpret_cast<const char*>(data), len);
    return s;
  }
  char utf8[3];
  for (uint32_t i = 0; i < len; i++) s.append(utf8, cp1252ToUtf8(data[i], utf8));
  return s;
}

}  // namespace

const char* statusText(Status s) {
  switch (s) {
    case Status::Ok:
      return "ok";
    case Status::NotMobi:
      return "not a MOBI file";
    case Status::BadHeader:
      return "damaged MOBI header";
    case Status::Drm:
      return "DRM-protected";
    case Status::UnsupportedCompression:
      return "unsupported compression";
    case Status::IoError:
      return "read error";
    case Status::Oom:
      return "out of memory";
  }
  return "?";
}

size_t cp1252ToUtf8(uint8_t c, char out[3]) {
  uint32_t cp = c;
  if (c >= 0x80 && c <= 0x9F) {
    cp = CP1252_HIGH[c - 0x80];
    if (cp == 0) cp = 0xFFFD;
  }
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  out[0] = static_cast<char>(0xE0 | (cp >> 12));
  out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[2] = static_cast<char>(0x80 | (cp & 0x3F));
  return 3;
}

bool recordSpan(const Reader& reader, uint16_t index, uint32_t& offset, uint32_t& length) {
  uint8_t head[PDB_HEADER_SIZE];
  if (!readExact(reader, 0, head, sizeof(head))) return false;
  const uint16_t numRecords = be16(head + 76);
  if (index >= numRecords) return false;
  uint8_t entry[PDB_RECORD_ENTRY * 2];
  const uint32_t entryOffset = PDB_HEADER_SIZE + static_cast<uint32_t>(index) * PDB_RECORD_ENTRY;
  const bool last = index + 1 == numRecords;
  if (!readExact(reader, entryOffset, entry, last ? PDB_RECORD_ENTRY : sizeof(entry))) return false;
  offset = be32(entry);
  const uint32_t end = last ? reader.fileSize : be32(entry + PDB_RECORD_ENTRY);
  if (end < offset || end > reader.fileSize) return false;
  length = end - offset;
  return true;
}

Status open(const Reader& reader, Info& info) {
  info = Info{};
  uint8_t head[PDB_HEADER_SIZE];
  if (reader.fileSize < PDB_HEADER_SIZE || !readExact(reader, 0, head, sizeof(head))) return Status::NotMobi;
  const bool bookMobi = memcmp(head + 60, "BOOKMOBI", 8) == 0;
  const bool textRead = memcmp(head + 60, "TEXtREAd", 8) == 0;
  if (!bookMobi && !textRead) return Status::NotMobi;
  info.numRecords = be16(head + 76);
  if (info.numRecords < 2) return Status::BadHeader;
  if (PDB_HEADER_SIZE + static_cast<uint32_t>(info.numRecords) * PDB_RECORD_ENTRY > reader.fileSize) {
    return Status::BadHeader;
  }

  std::unique_ptr<uint8_t[]> rec0(new (std::nothrow) uint8_t[RECORD0_MAX]);
  if (!rec0) return Status::Oom;
  const size_t len0 = readRecord(reader, 0, rec0.get(), RECORD0_MAX);
  if (len0 < 16) return Status::BadHeader;

  info.compression = be16(rec0.get());
  info.textLength = be32(rec0.get() + 4);
  info.textRecordCount = be16(rec0.get() + 8);
  info.recordSize = be16(rec0.get() + 10);
  info.encryption = be16(rec0.get() + 12);
  if (info.textRecordCount == 0 || info.textRecordCount >= info.numRecords) return Status::BadHeader;
  if (info.recordSize == 0 || info.recordSize > MAX_RECORD_SIZE) return Status::BadHeader;

  if (bookMobi && len0 >= 16 + 24 && memcmp(rec0.get() + 16, "MOBI", 4) == 0) {
    const uint8_t* m = rec0.get() + 16;
    info.hasMobiHeader = true;
    info.mobiHeaderLength = be32(m + 4);
    if (info.mobiHeaderLength < 24 || 16 + info.mobiHeaderLength > len0) return Status::BadHeader;
    const auto field = [&](uint32_t off) -> uint32_t { return off + 4 <= info.mobiHeaderLength ? be32(m + off) : 0; };
    info.mobiType = field(8);
    info.textEncoding = field(12);
    info.fileVersion = field(20);
    const uint32_t fullNameOffset = field(0x44);
    const uint32_t fullNameLength = field(0x48);
    info.firstImageIndex = field(0x5C);
    info.huffRecordIndex = field(0x60);
    info.huffRecordCount = field(0x64);
    const uint32_t exthFlags = field(0x70);
    if (info.mobiHeaderLength >= 0xE4 && info.fileVersion >= 5) info.extraFlags = be16(m + 0xE2);
    if (info.textEncoding != ENCODING_UTF8) info.textEncoding = ENCODING_CP1252;

    if (fullNameLength > 0 && fullNameOffset + fullNameLength <= len0) {
      info.title = exthString(rec0.get() + fullNameOffset, fullNameLength, info.textEncoding);
    }
    if (exthFlags & 0x40) {
      const uint32_t exthAt = 16 + info.mobiHeaderLength;
      if (exthAt + 12 <= len0 && memcmp(rec0.get() + exthAt, "EXTH", 4) == 0) {
        const uint32_t count = be32(rec0.get() + exthAt + 8);
        uint32_t pos = exthAt + 12;
        for (uint32_t i = 0; i < count && pos + 8 <= len0; i++) {
          const uint32_t type = be32(rec0.get() + pos);
          const uint32_t size = be32(rec0.get() + pos + 4);
          if (size < 8 || pos + size > len0) break;
          const uint8_t* data = rec0.get() + pos + 8;
          const uint32_t dataLen = size - 8;
          switch (type) {
            case 100:
              if (info.author.empty()) info.author = exthString(data, dataLen, info.textEncoding);
              break;
            case 503:
              info.title = exthString(data, dataLen, info.textEncoding);
              break;
            case 524:
              info.language = exthString(data, dataLen, info.textEncoding);
              break;
            case 121:
              info.hasKf8Boundary = true;
              break;
            default:
              break;
          }
          pos += size;
        }
      }
    }
  }

  // Record 0 can carry an encryption type even for TEXtREAd files; refuse both before any text read.
  if (info.encryption != 0) return Status::Drm;
  if (info.compression == COMPRESSION_HUFF) {
    // One HUFF record and 1..MAX_CDIC_RECORDS CDIC records, all inside the record table.
    if (!info.hasMobiHeader || info.huffRecordCount < 2 || info.huffRecordCount > HuffCdic::MAX_CDIC_RECORDS + 1 ||
        info.huffRecordIndex <= info.textRecordCount || info.huffRecordIndex + info.huffRecordCount > info.numRecords) {
      return Status::BadHeader;
    }
  } else if (info.compression != COMPRESSION_NONE && info.compression != COMPRESSION_PALMDOC) {
    return Status::UnsupportedCompression;
  }
  return Status::Ok;
}

// --- HUFF/CDIC ---
//
// Layout (as read by KindleUnpack's mobi_huffcdic.py and libmobi):
//   HUFF: "HUFF", header length, offset of dict1, offset of dict2. dict1 is 256 big-endian uint32,
//   indexed by the top 8 bits of the next 32 bits of input: bits 0-4 the code length, bit 7 set when
//   the code is at most 8 bits long ("terminal"), bits 8-31 the maxcode of that length. dict2 is 32
//   (mincode, maxcode) pairs for code lengths 1..32. Codes are assigned from the top down, so a longer
//   code is numerically smaller than any shorter one: a non-terminal entry's length grows while the
//   input is below mincode[length].
//   The phrase index is maxcode[length] - code (both as length-bit values).
//   CDIC: "CDIC", header length (16), total phrase count, code bits (phrases per record = 1 << bits),
//   then uint16 offsets (relative to byte 16) to each phrase: uint16 length (bit 15 set = literal,
//   clear = the phrase is itself HUFF-coded) followed by the bytes.

namespace {
constexpr uint32_t HUFF_HEADER = 24;
constexpr uint32_t CDIC_HEADER = 16;
constexpr int MAX_PHRASE_DEPTH = 8;
constexpr size_t PHRASE_SCRATCH = 256;  // stack bytes for a HUFF-coded phrase read from the file (uncached mode)

uint64_t be64Padded(const uint8_t* in, size_t inLen, size_t pos) {
  uint64_t x = 0;
  for (size_t k = 0; k < 8; k++) x = (x << 8) | (pos + k < inLen ? in[pos + k] : 0);
  return x;
}
}  // namespace

Status HuffCdic::load(const Reader& reader, const Info& info, size_t cacheLimit) {
  reader_ = &reader;
  cache_.reset();
  cacheSize_ = 0;
  if (info.huffRecordCount < 2 || info.huffRecordCount > MAX_CDIC_RECORDS + 1 || info.huffRecordIndex > 0xFFFF) {
    return Status::BadHeader;
  }
  uint32_t huffOffset, huffLength;
  if (!recordSpan(reader, static_cast<uint16_t>(info.huffRecordIndex), huffOffset, huffLength))
    return Status::BadHeader;
  uint8_t head[HUFF_HEADER];
  if (huffLength < HUFF_HEADER || !readExact(reader, huffOffset, head, sizeof(head))) return Status::IoError;
  if (memcmp(head, "HUFF", 4) != 0) return Status::BadHeader;
  const uint32_t off1 = be32(head + 8), off2 = be32(head + 12);
  if (off1 + sizeof(dict1_) > huffLength || off2 + 64 * 4 > huffLength) return Status::BadHeader;
  uint8_t* raw = reinterpret_cast<uint8_t*>(dict1_);
  if (!readExact(reader, huffOffset + off1, raw, sizeof(dict1_))) return Status::IoError;
  for (int i = 0; i < 256; i++) dict1_[i] = be32(raw + i * 4);
  uint8_t dict2[64 * 4];
  if (!readExact(reader, huffOffset + off2, dict2, sizeof(dict2))) return Status::IoError;
  mincode_[0] = maxcode_[0] = 0;
  for (int len = 1; len <= 32; len++) {
    const uint64_t mincode = be32(dict2 + (len - 1) * 8), maxcode = be32(dict2 + (len - 1) * 8 + 4);
    mincode_[len] = static_cast<uint32_t>(mincode << (32 - len));
    maxcode_[len] = static_cast<uint32_t>(((maxcode + 1) << (32 - len)) - 1);
  }

  cdicCount_ = info.huffRecordCount - 1;
  cdicSpans_.reset(new (std::nothrow) uint32_t[cdicCount_ * 2]);
  if (!cdicSpans_) return Status::Oom;
  size_t total = 0;
  phraseCount_ = 0;
  for (uint32_t i = 0; i < cdicCount_; i++) {
    uint32_t offset, length;
    const uint32_t index = info.huffRecordIndex + 1 + i;
    if (index > 0xFFFF || !recordSpan(reader, static_cast<uint16_t>(index), offset, length)) return Status::BadHeader;
    uint8_t ch[CDIC_HEADER];
    if (length < CDIC_HEADER || !readExact(reader, offset, ch, sizeof(ch))) return Status::IoError;
    if (memcmp(ch, "CDIC", 4) != 0 || be32(ch + 4) != CDIC_HEADER) return Status::BadHeader;
    const uint32_t phrases = be32(ch + 8), bits = be32(ch + 12);
    if (i == 0) {
      phraseCount_ = phrases;
      codeBits_ = bits;
      if (bits == 0 || bits > 16 || phrases == 0) return Status::BadHeader;
      // Every phrase index must land in a record we have.
      if ((static_cast<uint64_t>(phrases) + (1u << bits) - 1) >> bits > cdicCount_) return Status::BadHeader;
    } else if (phrases != phraseCount_ || bits != codeBits_) {
      return Status::BadHeader;
    }
    cdicSpans_[i * 2] = offset;
    cdicSpans_[i * 2 + 1] = length;
    total += length;
  }
  if (total <= cacheLimit) {
    cache_.reset(new (std::nothrow) uint8_t[total]);
    if (!cache_) return Status::Oom;
    size_t at = 0;
    for (uint32_t i = 0; i < cdicCount_; i++) {
      if (!readExact(reader, cdicSpans_[i * 2], cache_.get() + at, cdicSpans_[i * 2 + 1])) {
        cache_.reset();
        return Status::IoError;
      }
      at += cdicSpans_[i * 2 + 1];
    }
    cacheSize_ = total;
  }
  return Status::Ok;
}

bool HuffCdic::phrase(uint32_t index, Phrase& out) const {
  if (index >= phraseCount_) return false;
  const uint32_t rec = index >> codeBits_;
  const uint32_t slot = index & ((1u << codeBits_) - 1);
  if (rec >= cdicCount_) return false;
  const uint32_t recLength = cdicSpans_[rec * 2 + 1];
  const uint32_t slotAt = CDIC_HEADER + slot * 2;
  if (slotAt + 2 > recLength) return false;
  uint16_t blen;
  uint32_t off;
  if (cache_) {
    size_t base = 0;
    for (uint32_t i = 0; i < rec; i++) base += cdicSpans_[i * 2 + 1];
    const uint8_t* r = cache_.get() + base;
    off = CDIC_HEADER + be16(r + slotAt);
    if (off + 2 > recLength) return false;
    blen = be16(r + off);
    out.data = r + off + 2;
  } else {
    const uint32_t fileAt = cdicSpans_[rec * 2];
    uint8_t tmp[2];
    if (!readExact(*reader_, fileAt + slotAt, tmp, 2)) return false;
    off = CDIC_HEADER + be16(tmp);
    if (off + 2 > recLength || !readExact(*reader_, fileAt + off, tmp, 2)) return false;
    blen = be16(tmp);
    out.data = nullptr;
    out.fileOffset = fileAt + off + 2;
  }
  out.len = blen & 0x7FFF;
  out.literal = blen & 0x8000;
  return off + 2 + out.len <= recLength;
}

size_t HuffCdic::decode(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap, int depth) const {
  if (depth > MAX_PHRASE_DEPTH) return 0;
  size_t pos = 0, o = 0;
  uint64_t x = be64Padded(in, inLen, 0);
  int n = 32;
  int64_t bitsLeft = static_cast<int64_t>(inLen) * 8;
  while (true) {
    if (n <= 0) {
      pos += 4;
      x = be64Padded(in, inLen, pos);
      n += 32;
    }
    const uint32_t code = static_cast<uint32_t>(x >> n);
    const uint32_t v = dict1_[code >> 24];
    uint32_t codeLen = v & 0x1F;
    uint32_t maxcode;
    if (v & 0x80) {
      maxcode = static_cast<uint32_t>(((static_cast<uint64_t>(v >> 8) + 1) << (32 - codeLen)) - 1);
    } else {
      while (codeLen < 32 && code < mincode_[codeLen]) codeLen++;
      maxcode = maxcode_[codeLen];
    }
    if (codeLen == 0) return 0;
    n -= static_cast<int>(codeLen);
    bitsLeft -= codeLen;
    if (bitsLeft < 0) break;
    if (maxcode < code) return 0;
    const uint32_t index = (maxcode - code) >> (32 - codeLen);
    Phrase p;
    if (!phrase(index, p)) return 0;
    if (p.literal) {
      if (o + p.len > outCap) return 0;
      if (p.data)
        memcpy(out + o, p.data, p.len);
      else if (!readExact(*reader_, p.fileOffset, out + o, p.len))
        return 0;
      o += p.len;
    } else {
      uint8_t scratch[PHRASE_SCRATCH];
      const uint8_t* data = p.data;
      if (!data) {
        if (p.len > sizeof(scratch) || !readExact(*reader_, p.fileOffset, scratch, p.len)) return 0;
        data = scratch;
      }
      const size_t n2 = decode(data, p.len, out + o, outCap - o, depth + 1);
      if (n2 == 0 && p.len != 0) return 0;
      o += n2;
    }
  }
  return o;
}

size_t HuffCdic::decompress(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap) const {
  return decode(in, inLen, out, outCap, 0);
}

size_t palmdocDecompress(const uint8_t* in, size_t inLen, uint8_t* out, size_t outCap) {
  size_t i = 0, o = 0;
  while (i < inLen) {
    const uint8_t c = in[i++];
    if (c == 0 || (c >= 9 && c <= 0x7F)) {
      if (o >= outCap) return 0;
      out[o++] = c;
    } else if (c <= 8) {
      if (i + c > inLen || o + c > outCap) return 0;
      memcpy(out + o, in + i, c);
      i += c;
      o += c;
    } else if (c >= 0xC0) {
      if (o + 2 > outCap) return 0;
      out[o++] = ' ';
      out[o++] = static_cast<uint8_t>(c ^ 0x80);
    } else {
      if (i >= inLen) return 0;
      const uint16_t pair = static_cast<uint16_t>((c << 8) | in[i++]);
      const size_t distance = (pair >> 3) & 0x7FF;
      const size_t length = (pair & 7) + 3;
      if (distance == 0 || distance > o || o + length > outCap) return 0;
      for (size_t k = 0; k < length; k++) {
        out[o] = out[o - distance];
        o++;
      }
    }
  }
  return o;
}

size_t trailingSize(const uint8_t* record, size_t length, uint16_t extraFlags) {
  size_t trimmed = 0;
  for (int bit = 15; bit >= 1; bit--) {
    if (!(extraFlags & (1u << bit))) continue;
    // Backward variable-width integer: 7 bits per byte from the end, the byte with 0x80 set is the
    // first of the number. Its value counts the entry's own bytes.
    size_t avail = length - trimmed;
    if (avail == 0) return length;
    uint32_t value = 0;
    int bitpos = 0;
    size_t used = 0;
    while (avail > 0) {
      const uint8_t v = record[length - trimmed - 1 - used];
      value |= static_cast<uint32_t>(v & 0x7F) << bitpos;
      bitpos += 7;
      used++;
      avail--;
      if ((v & 0x80) || bitpos >= 28) break;
    }
    if (value > length - trimmed) return length;
    trimmed += value;
  }
  if (extraFlags & 1) {
    if (trimmed >= length) return length;
    const size_t n = (record[length - trimmed - 1] & 0x3) + 1;
    trimmed += n > length - trimmed ? length - trimmed : n;
  }
  return trimmed;
}

Status streamHtml(const Reader& reader, const Info& info, std::string_view title, const Sink& out,
                  const ImageResolver* images) {
  struct Ctx {
    const Sink* sink;
    bool ok = true;
  } ctx{&out};
  const auto emit = [](void* c, const char* data, size_t size) {
    auto* self = static_cast<Ctx*>(c);
    if (!self->ok) return false;
    self->ok = self->sink->write(self->sink->ctx, data, size);
    return self->ok;
  };
  const auto write = [&](std::string_view s) {
    if (ctx.ok && !s.empty()) emit(&ctx, s.data(), s.size());
  };

  std::unique_ptr<uint8_t[]> raw(new (std::nothrow) uint8_t[MAX_RECORD_SIZE + 64]);
  std::unique_ptr<uint8_t[]> text(new (std::nothrow) uint8_t[MAX_RECORD_SIZE]);
  std::unique_ptr<char[]> utf8(info.textEncoding == ENCODING_UTF8 ? nullptr
                                                                  : new (std::nothrow) char[MAX_RECORD_SIZE * 3]);
  std::unique_ptr<HtmlFilter> filter(new (std::nothrow) HtmlFilter(emit, &ctx));
  if (!raw || !text || !filter || (info.textEncoding != ENCODING_UTF8 && !utf8)) return Status::Oom;
  if (images && images->resolve) filter->setImageResolver(images->resolve, images->ctx);
  std::unique_ptr<HuffCdic> huff;
  if (info.compression == COMPRESSION_HUFF) {
    huff.reset(new (std::nothrow) HuffCdic);
    if (!huff) return Status::Oom;
    const Status s = huff->load(reader, info);
    if (s != Status::Ok) return s;
  }

  write("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
  write(CACHE_VERSION_TAG);
  write("\n<!DOCTYPE html>\n<html>\n<head><title>");
  for (const char c : title) {
    if (c == '&')
      write("&amp;");
    else if (c == '<')
      write("&lt;");
    else if (c == '>')
      write("&gt;");
    else
      write(std::string_view(&c, 1));
  }
  write("</title></head>\n<body>\n");

  const bool plain = !info.hasMobiHeader;  // bare PalmDOC text: escape it, newline = line break
  if (plain) write("<p>");
  uint32_t remaining = info.textLength;
  for (uint16_t i = 1; i <= info.textRecordCount && ctx.ok; i++) {
    size_t rawLen = readRecord(reader, i, raw.get(), MAX_RECORD_SIZE + 64);
    if (rawLen == 0) return Status::IoError;
    const size_t trailing = trailingSize(raw.get(), rawLen, info.extraFlags);
    rawLen -= trailing;
    size_t textLen;
    if (info.compression == COMPRESSION_PALMDOC || huff) {
      textLen = huff ? huff->decompress(raw.get(), rawLen, text.get(), MAX_RECORD_SIZE)
                     : palmdocDecompress(raw.get(), rawLen, text.get(), MAX_RECORD_SIZE);
      if (textLen == 0 && rawLen != 0) continue;  // one damaged record: skip it, keep the book readable
    } else {
      textLen = rawLen > MAX_RECORD_SIZE ? MAX_RECORD_SIZE : rawLen;
      memcpy(text.get(), raw.get(), textLen);
    }
    if (textLen > remaining) textLen = remaining;  // textLength caps the last record
    remaining -= static_cast<uint32_t>(textLen);

    std::string_view chunk;
    if (info.textEncoding == ENCODING_UTF8) {
      chunk = std::string_view(reinterpret_cast<const char*>(text.get()), textLen);
    } else {
      size_t n = 0;
      for (size_t k = 0; k < textLen; k++) n += cp1252ToUtf8(text[k], utf8.get() + n);
      chunk = std::string_view(utf8.get(), n);
    }
    if (plain) {
      for (const char c : chunk) {
        if (c == '&')
          write("&amp;");
        else if (c == '<')
          write("&lt;");
        else if (c == '>')
          write("&gt;");
        else if (c == '\n')
          write("<br />\n");
        else if (c != '\r')
          write(std::string_view(&c, 1));
      }
    } else if (!filter->feed(chunk)) {
      break;
    }
  }
  if (plain)
    write("</p>");
  else
    filter->finish();
  write("\n</body>\n</html>\n");
  return ctx.ok ? Status::Ok : Status::IoError;
}

}  // namespace mobi
