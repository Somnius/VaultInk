#pragma once

// Builds small .doc files for the tests: an OLE2 compound file ([MS-CFB], version 3, 512-byte sectors) holding a
// WordDocument stream (a FIB and the text), a table stream (the Clx with the piece table) and, when asked, a
// SummaryInformation stream. Written for these tests from the same reading of the formats as lib/Doc, so a
// mistake in that reading would be in both: test/markup_books/fixtures/libreoffice.doc is the file from
// another writer.

#include <BlockSource.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace docfixture {

inline void put16(std::string& s, size_t at, uint16_t v) {
  s[at] = static_cast<char>(v & 0xFF);
  s[at + 1] = static_cast<char>(v >> 8);
}
inline void put32(std::string& s, size_t at, uint32_t v) {
  for (int i = 0; i < 4; i++) s[at + i] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
inline void add16(std::string& s, uint16_t v) {
  s.append(2, '\0');
  put16(s, s.size() - 2, v);
}
inline void add32(std::string& s, uint32_t v) {
  s.append(4, '\0');
  put32(s, s.size() - 4, v);
}

struct CfbStream {
  std::string name;
  std::string data;
};

struct CfbOptions {
  bool fragment = false;  // a free sector after every data sector: no two sectors of a stream are neighbours
};

inline std::string buildCfb(const std::vector<CfbStream>& streams, const CfbOptions& options = {}) {
  constexpr uint32_t SECTOR = 512;
  constexpr uint32_t FREE = 0xFFFFFFFF;
  constexpr uint32_t END = 0xFFFFFFFE;
  constexpr uint32_t FATSECT = 0xFFFFFFFD;
  constexpr uint32_t DIFSECT = 0xFFFFFFFC;
  std::vector<std::string> sectors;
  std::vector<uint32_t> fat;

  const auto place = [&](const std::string& data, const bool fragment) -> uint32_t {
    if (data.empty()) return END;
    const uint32_t first = static_cast<uint32_t>(sectors.size());
    for (size_t at = 0; at < data.size(); at += SECTOR) {
      std::string sector = data.substr(at, SECTOR);
      sector.resize(SECTOR, '\0');
      sectors.push_back(sector);
      const bool last = at + SECTOR >= data.size();
      const uint32_t self = static_cast<uint32_t>(sectors.size() - 1);
      fat.push_back(last ? END : self + (fragment ? 2 : 1));
      if (fragment && !last) {
        sectors.emplace_back(SECTOR, '\xEE');
        fat.push_back(FREE);
      }
    }
    return first;
  };

  // The streams under 4096 bytes go in the mini stream, 64 bytes a sector.
  std::string mini;
  std::vector<uint32_t> miniFat;
  std::vector<uint32_t> starts(streams.size(), END);
  for (size_t i = 0; i < streams.size(); i++) {
    const std::string& data = streams[i].data;
    if (data.empty() || data.size() >= 4096) continue;
    starts[i] = static_cast<uint32_t>(mini.size() / 64);
    const size_t count = (data.size() + 63) / 64;
    for (size_t k = 0; k < count; k++)
      miniFat.push_back(k + 1 == count ? END : starts[i] + static_cast<uint32_t>(k) + 1);
    mini += data;
    mini.resize((mini.size() + 63) / 64 * 64, '\0');
  }
  for (size_t i = 0; i < streams.size(); i++) {
    if (streams[i].data.size() >= 4096) starts[i] = place(streams[i].data, options.fragment);
  }
  const uint32_t miniStart = place(mini, options.fragment);
  std::string miniFatBytes;
  for (const uint32_t v : miniFat) add32(miniFatBytes, v);
  miniFatBytes.resize((miniFatBytes.size() + SECTOR - 1) / SECTOR * SECTOR, '\xFF');
  const uint32_t miniFatStart = place(miniFatBytes, options.fragment);

  // The directory: the root, then the streams as a chain of right siblings.
  std::string dir;
  const auto entry = [&](const std::string& name, uint8_t type, uint32_t right, uint32_t child, uint32_t start,
                         uint32_t size) {
    std::string e(128, '\0');
    for (size_t i = 0; i < name.size(); i++) e[i * 2] = name[i];
    put16(e, 0x40, static_cast<uint16_t>((name.size() + 1) * 2));
    e[0x42] = static_cast<char>(type);
    e[0x43] = 1;
    put32(e, 0x44, FREE);
    put32(e, 0x48, right);
    put32(e, 0x4C, child);
    put32(e, 0x74, start);
    put32(e, 0x78, size);
    dir += e;
  };
  entry("Root Entry", 5, FREE, streams.empty() ? FREE : 1, miniStart, static_cast<uint32_t>(mini.size()));
  for (size_t i = 0; i < streams.size(); i++) {
    entry(streams[i].name, 2, i + 1 < streams.size() ? static_cast<uint32_t>(i + 2) : FREE, FREE, starts[i],
          static_cast<uint32_t>(streams[i].data.size()));
  }
  while (dir.size() % SECTOR != 0) {
    std::string e(128, '\0');
    put32(e, 0x44, FREE);
    put32(e, 0x48, FREE);
    put32(e, 0x4C, FREE);
    dir += e;
  }
  const uint32_t dirStart = place(dir, options.fragment);

  // The FAT sectors, and DIFAT sectors past the 109 the header holds.
  uint32_t fatSectors = 1;
  uint32_t difSectors = 0;
  for (;;) {
    difSectors = fatSectors > 109 ? (fatSectors - 109 + 126) / 127 : 0;
    if (fatSectors * 128 >= sectors.size() + fatSectors + difSectors) break;
    fatSectors++;
  }
  const uint32_t fatStart = static_cast<uint32_t>(sectors.size());
  for (uint32_t i = 0; i < fatSectors; i++) fat.push_back(FATSECT);
  const uint32_t difStart = fatStart + fatSectors;
  for (uint32_t i = 0; i < difSectors; i++) fat.push_back(DIFSECT);
  fat.resize(static_cast<size_t>(fatSectors) * 128, FREE);
  for (uint32_t i = 0; i < fatSectors; i++) {
    std::string sector;
    for (uint32_t k = 0; k < 128; k++) add32(sector, fat[i * 128 + k]);
    sectors.push_back(sector);
  }
  for (uint32_t d = 0; d < difSectors; d++) {
    std::string sector;
    for (uint32_t k = 0; k < 127; k++) {
      const uint32_t index = 109 + d * 127 + k;
      add32(sector, index < fatSectors ? fatStart + index : FREE);
    }
    add32(sector, d + 1 < difSectors ? difStart + d + 1 : END);
    sectors.push_back(sector);
  }

  std::string header(SECTOR, '\0');
  static const char magic[8] = {'\xD0', '\xCF', '\x11', '\xE0', '\xA1', '\xB1', '\x1A', '\xE1'};
  memcpy(header.data(), magic, 8);
  put16(header, 0x18, 0x003E);
  put16(header, 0x1A, 3);
  put16(header, 0x1C, 0xFFFE);
  put16(header, 0x1E, 9);
  put16(header, 0x20, 6);
  put32(header, 0x2C, fatSectors);
  put32(header, 0x30, dirStart);
  put32(header, 0x38, 4096);
  put32(header, 0x3C, miniFatStart);
  put32(header, 0x40, static_cast<uint32_t>(miniFatBytes.size() / SECTOR));
  put32(header, 0x44, difSectors ? difStart : END);
  put32(header, 0x48, difSectors);
  for (uint32_t i = 0; i < 109; i++) put32(header, 0x4C + i * 4, i < fatSectors ? fatStart + i : FREE);

  std::string file = header;
  for (const std::string& sector : sectors) file += sector;
  return file;
}

// One run of text: 8-bit bytes (`bytes`) or UTF-16 (`wide`).
struct Piece {
  std::string bytes;
  std::u16string wide;
  bool compressed = true;

  static Piece narrow(std::string text) { return Piece{std::move(text), {}, true}; }
  static Piece utf16(std::u16string text) { return Piece{{}, std::move(text), false}; }
  uint32_t chars() const { return static_cast<uint32_t>(compressed ? bytes.size() : wide.size()); }
};

struct DocOptions {
  uint16_t ident = 0xA5EC;
  uint16_t nFib = 0x00C1;
  uint16_t flags = 0x0200;  // fWhichTblStm: the 1Table stream
  uint16_t lid = 0x0409;
  uint32_t ccpFtn = 0;  // the last characters of the pieces are the footnotes, then the endnotes
  uint32_t ccpHdd = 0;
  uint32_t ccpEdn = 0;
  size_t wordPad = 4096;   // the WordDocument stream is at least this long (below 4096 it is a mini stream)
  size_t extraStream = 0;  // a "Data" stream of this size before the others
  std::string summary;     // a SummaryInformation stream, see buildSummary
  bool fragment = false;
  bool wordDocument = true;
  uint32_t piecesClaimed = 0;  // not 0: the PlcPcd's size says this many pieces
  bool overlap = false;        // every piece points at the first piece's bytes, the only ones stored
};

inline std::string buildDoc(const std::vector<Piece>& pieces, const DocOptions& options = {}) {
  std::string word(0x400, '\0');
  put16(word, 0x00, options.ident);
  put16(word, 0x02, options.nFib);
  put16(word, 0x06, options.lid);
  put16(word, 0x0A, options.flags);
  put16(word, 0x20, 14);
  put16(word, 0x3E, 22);
  put16(word, 0x98, 93);
  uint32_t total = 0;
  for (const Piece& piece : pieces) total += piece.chars();
  put32(word, 0x4C, total - options.ccpFtn - options.ccpHdd - options.ccpEdn);
  put32(word, 0x50, options.ccpFtn);
  put32(word, 0x54, options.ccpHdd);
  put32(word, 0x60, options.ccpEdn);

  std::string cps;
  std::string pcds;
  uint32_t cp = 0;
  uint32_t firstFc = 0;
  for (size_t i = 0; i < pieces.size(); i++) {
    const Piece& piece = pieces[i];
    add32(cps, cp);
    cp += piece.chars();
    uint32_t fc = static_cast<uint32_t>(word.size());
    const bool store = !options.overlap || i == 0;
    if (piece.compressed) {
      if (store) word += piece.bytes;
      fc = (fc * 2) | 0x40000000;
    } else if (store) {
      for (const char16_t u : piece.wide) add16(word, static_cast<uint16_t>(u));
    }
    if (i == 0) firstFc = fc;
    add16(pcds, 0);
    add32(pcds, options.overlap ? firstFc : fc);
    add16(pcds, 0);
  }
  add32(cps, cp);
  if (word.size() < options.wordPad) word.resize(options.wordPad, '\0');

  std::string table(16, '\x55');
  const uint32_t fcClx = static_cast<uint32_t>(table.size());
  table += '\x01';  // a Prc, to be stepped over
  add16(table, 4);
  table += "\x11\x22\x33\x44";
  table += '\x02';
  add32(table,
        options.piecesClaimed ? options.piecesClaimed * 12 + 4 : static_cast<uint32_t>(cps.size() + pcds.size()));
  table += cps + pcds;
  if (options.piecesClaimed) table.resize(fcClx + 12 + options.piecesClaimed * 12 + 4, '\0');
  put32(word, 0x1A2, fcClx);
  put32(word, 0x1A6, static_cast<uint32_t>(table.size()) - fcClx);
  table.resize(table.size() + 40, '\x66');

  std::vector<CfbStream> streams;
  if (options.extraStream) streams.push_back({"Data", std::string(options.extraStream, '\x77')});
  if (options.wordDocument) streams.push_back({"WordDocument", word});
  streams.push_back({(options.flags & 0x0200) ? "1Table" : "0Table", table});
  if (!options.summary.empty()) streams.push_back({"\005SummaryInformation", options.summary});
  CfbOptions cfb;
  cfb.fragment = options.fragment;
  return buildCfb(streams, cfb);
}

// A property set with the code page, the title (property 2) and the author (4); `wide` writes them as VT_LPWSTR.
inline std::string buildSummary(uint16_t codePage, const std::string& title, const std::string& author,
                                const std::u16string& wideTitle = {}) {
  std::string props[3];
  add32(props[0], 2);
  add16(props[0], codePage);
  add16(props[0], 0);
  if (wideTitle.empty()) {
    add32(props[1], 30);
    add32(props[1], static_cast<uint32_t>(title.size() + 1));
    props[1] += title;
    props[1] += '\0';
  } else {
    add32(props[1], 31);
    add32(props[1], static_cast<uint32_t>(wideTitle.size() + 1));
    for (const char16_t u : wideTitle) add16(props[1], static_cast<uint16_t>(u));
    add16(props[1], 0);
  }
  add32(props[2], 30);
  add32(props[2], static_cast<uint32_t>(author.size() + 1));
  props[2] += author;
  props[2] += '\0';
  for (std::string& prop : props) prop.resize((prop.size() + 3) / 4 * 4, '\0');

  std::string set;
  add32(set, 0);
  add32(set, 3);
  const uint32_t ids[3] = {1, 2, 4};
  uint32_t off = 8 + 3 * 8;
  for (int i = 0; i < 3; i++) {
    add32(set, ids[i]);
    add32(set, off);
    off += static_cast<uint32_t>(props[i].size());
  }
  for (const std::string& prop : props) set += prop;
  put32(set, 0, static_cast<uint32_t>(set.size()));

  std::string stream;
  add16(stream, 0xFFFE);
  add16(stream, 0);
  add32(stream, 0x00020005);
  stream.append(16, '\0');
  add32(stream, 1);
  stream.append(16, '\x0F');
  add32(stream, 48);
  return stream + set;
}

// A file in memory as a blocksource::Source that hands out at most `step` bytes a call.
struct MemSource {
  std::string data;
  size_t step = 4096;
  size_t reads = 0;
  size_t failAfter = SIZE_MAX;  // the reads after this many fail, like a card that stops answering

  static int readAt(void* ctx, uint32_t offset, uint8_t* buf, size_t size) {
    auto* self = static_cast<MemSource*>(ctx);
    self->reads++;
    if (self->reads > self->failAfter) return -1;
    if (offset >= self->data.size()) return 0;
    const size_t n = std::min({size, self->step, self->data.size() - offset});
    memcpy(buf, self->data.data() + offset, n);
    return static_cast<int>(n);
  }

  blocksource::Source source() {
    return blocksource::Source{this, &MemSource::readAt, static_cast<uint32_t>(data.size())};
  }
};

}  // namespace docfixture
