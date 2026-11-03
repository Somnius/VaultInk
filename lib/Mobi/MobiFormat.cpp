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
  if (info.compression != COMPRESSION_NONE && info.compression != COMPRESSION_PALMDOC) {
    return Status::UnsupportedCompression;
  }
  return Status::Ok;
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
    if (info.compression == COMPRESSION_PALMDOC) {
      textLen = palmdocDecompress(raw.get(), rawLen, text.get(), MAX_RECORD_SIZE);
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
