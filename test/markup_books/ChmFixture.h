#pragma once

// Builds small .chm files for the tests, and holds the LZX encoder they need. Written for these tests from the
// same reading of the formats as lib/Chm, so a mistake in that reading would be in both: what a real file
// showed is in docs/book-formats.md.
//
// The encoder is simple, not good: one match candidate per position, Huffman lengths from plain counts. It can
// write all three block types, blocks of any size (so they run across frames), odd uncompressed blocks, the
// repeated offsets and far matches in big windows.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "DocFixture.h"

namespace chmfixture {

using docfixture::add16;
using docfixture::add32;
using docfixture::put32;

constexpr uint32_t FRAME = 32768;

// ---------------------------------------------------------------------------------------------- LZX encoder

enum class BlockKind : uint8_t { Verbatim = 1, Aligned = 2, Uncompressed = 3 };

struct LzxOptions {
  unsigned windowBits = 16;
  uint32_t resetFrames = 2;
  // The blocks, used in turn and again from the start: a kind and a size in bytes (cut at a reset point).
  std::vector<std::pair<BlockKind, uint32_t>> blocks = {{BlockKind::Verbatim, 32768}};
  bool e8Header = false;       // the stream header asks for the E8 translation
  bool fillLastFrame = false;  // zeros up to a whole frame are compressed too, as Microsoft's compiler does
};

struct LzxStream {
  std::string bytes;                 // the frames one after the other
  std::vector<uint32_t> frameStart;  // where each frame starts in `bytes`
};

class BitWriter {
 public:
  explicit BitWriter(std::string& out) : out_(out) {}
  void bits(uint32_t value, unsigned n) {
    for (unsigned i = n; i-- > 0;) {
      word_ = static_cast<uint16_t>((word_ << 1) | ((value >> i) & 1));
      if (++count_ == 16) flushWord();
    }
  }
  // Zero bits up to the next word; `atLeastOne` adds a whole word when already there.
  void align(bool atLeastOne) {
    if (count_ == 0) {
      if (atLeastOne) add16(out_, 0);
      return;
    }
    while (count_ != 0) bits(0, 1);
  }

 private:
  void flushWord() {
    add16(out_, word_);
    word_ = 0;
    count_ = 0;
  }
  std::string& out_;
  uint16_t word_ = 0;
  unsigned count_ = 0;
};

// Huffman code lengths from counts, no longer than `maxLen`; a symbol never seen gets 0. The code is always a
// full one (other decoders refuse a code with gaps): with one symbol in use a second gets a code too, and with
// none, when `neverEmpty`, two do.
inline std::vector<uint8_t> huffmanLengths(std::vector<uint32_t> freq, unsigned maxLen, bool neverEmpty = true) {
  const size_t n = freq.size();
  size_t used = 0;
  for (const uint32_t f : freq) used += f != 0;
  if (used == 1 || (used == 0 && neverEmpty)) {
    for (size_t s = 0; s < n && used < 2; s++) {
      if (freq[s] == 0) {
        freq[s] = 1;
        used++;
      }
    }
  }
  for (;;) {
    std::vector<uint8_t> lens(n, 0);
    struct Node {
      uint64_t weight;
      int left, right;
    };
    std::vector<Node> nodes;
    std::vector<int> live;
    for (size_t s = 0; s < n; s++) {
      if (freq[s] == 0) continue;
      nodes.push_back({freq[s], -1, static_cast<int>(s)});
      live.push_back(static_cast<int>(nodes.size() - 1));
    }
    if (live.empty()) return lens;
    if (live.size() == 1) {
      lens[nodes[0].right] = 1;
      return lens;
    }
    while (live.size() > 1) {
      std::sort(live.begin(), live.end(), [&](int a, int b) { return nodes[a].weight > nodes[b].weight; });
      const int a = live.back();
      live.pop_back();
      const int b = live.back();
      live.pop_back();
      nodes.push_back({nodes[a].weight + nodes[b].weight, a, b});
      live.push_back(static_cast<int>(nodes.size() - 1));
    }
    unsigned deepest = 0;
    std::vector<std::pair<int, unsigned>> stack = {{live[0], 0}};
    while (!stack.empty()) {
      const auto [node, depth] = stack.back();
      stack.pop_back();
      if (nodes[node].left < 0) {
        lens[nodes[node].right] = static_cast<uint8_t>(depth);
        deepest = std::max(deepest, depth);
      } else {
        stack.push_back({nodes[node].left, depth + 1});
        stack.push_back({nodes[node].right, depth + 1});
      }
    }
    if (deepest <= maxLen) return lens;
    for (uint32_t& f : freq) {
      if (f != 0) f = f / 2 + 1;  // flatter counts, shorter longest code
    }
  }
}

// The codes for a set of lengths: numbered in order, the shortest length first.
inline std::vector<uint32_t> canonicalCodes(const std::vector<uint8_t>& lens) {
  std::vector<uint32_t> codes(lens.size(), 0);
  uint32_t code = 0;
  for (unsigned len = 1; len <= 16; len++) {
    for (size_t s = 0; s < lens.size(); s++) {
      if (lens[s] == len) codes[s] = code++;
    }
    code <<= 1;
  }
  return codes;
}

struct LzxToken {
  uint32_t offset = 0;  // 0: a literal
  uint32_t length = 1;
  uint8_t literal = 0;
};

inline unsigned lzxExtraBits(unsigned slot) { return slot < 4 ? 0 : std::min(17u, (slot >> 1) - 1); }
inline uint32_t lzxSlotBase(unsigned slot) {
  uint32_t base = 0;
  for (unsigned s = 0; s < slot; s++) base += 1u << lzxExtraBits(s);
  return base;
}

class LzxEncoder {
 public:
  explicit LzxEncoder(const LzxOptions& options) : o_(options) {
    static const unsigned slots[7] = {30, 32, 34, 36, 38, 42, 50};
    slots_ = slots[o_.windowBits - 15];
    mainSymbols_ = 256 + slots_ * 8;
    for (unsigned s = 0; s <= slots_; s++) bases_.push_back(lzxSlotBase(s));
  }

  LzxStream encode(const std::string& real) {
    std::string data = real;
    if (o_.fillLastFrame) data.resize((data.size() + FRAME - 1) / FRAME * FRAME, '\0');
    LzxStream stream;
    size_t blockIndex = 0;
    const uint32_t span = o_.resetFrames * FRAME;
    for (size_t spanStart = 0; spanStart < data.size(); spanStart += span) {
      const size_t spanEnd = std::min(data.size(), spanStart + span);
      // A reset: nothing before it is known any more.
      prevMain_.assign(mainSymbols_, 0);
      prevLength_.assign(249, 0);
      r_[0] = r_[1] = r_[2] = 1;
      bool header = true;
      std::string frame;
      auto writer = std::make_unique<BitWriter>(frame);
      const auto nextFrame = [&]() {
        writer->align(false);
        stream.frameStart.push_back(static_cast<uint32_t>(stream.bytes.size()));
        stream.bytes += frame;
        frame.clear();
        writer = std::make_unique<BitWriter>(frame);
      };
      // A full frame is closed only when something more is written, so the pad byte of an odd uncompressed
      // block that ends with the frame stays in it.
      bool full = false;
      const auto beforeWrite = [&]() {
        if (full) nextFrame();
        full = false;
      };
      size_t pos = spanStart;
      while (pos < spanEnd) {
        beforeWrite();
        const auto [kind, wanted] = o_.blocks[blockIndex++ % o_.blocks.size()];
        const size_t size = std::min<size_t>(wanted, spanEnd - pos);
        if (header) {
          writer->bits(o_.e8Header ? 1 : 0, 1);
          if (o_.e8Header) {
            writer->bits(0x0001, 16);
            writer->bits(0x0000, 16);
          }
          header = false;
        }
        writer->bits(static_cast<uint32_t>(kind), 3);
        writer->bits(static_cast<uint32_t>(size >> 8), 16);
        writer->bits(static_cast<uint32_t>(size & 0xFF), 8);
        if (kind == BlockKind::Uncompressed) {
          writer->align(true);
          for (const uint32_t r : r_) add32(frame, r);
          for (size_t i = 0; i < size; i++) {
            beforeWrite();
            frame += data[pos++];
            full = pos % FRAME == 0;
          }
          if (size & 1) frame += '\0';
          continue;
        }
        const std::vector<LzxToken> tokens = tokenise(data, spanStart, pos, size);
        writeTrees(*writer, kind, tokens);
        for (const LzxToken& token : tokens) {
          beforeWrite();
          writeToken(*writer, kind, token);
          pos += token.length;
          full = pos % FRAME == 0;
        }
      }
      nextFrame();
    }
    return stream;
  }

 private:
  struct Formatted {
    uint32_t mainSymbol;
    uint32_t lengthSymbol;  // 0xFFFFFFFF: none
    unsigned slot;
    uint32_t extra;
  };

  // Greedy matches from the last place the same three bytes were seen, and from R0. No token crosses a frame.
  std::vector<LzxToken> tokenise(const std::string& data, size_t spanStart, size_t from, size_t size) {
    std::vector<LzxToken> tokens;
    if (hashHead_.empty() || hashSpan_ != spanStart) {
      hashHead_.assign(1 << 16, SIZE_MAX);
      hashSpan_ = spanStart;
      hashedTo_ = spanStart;
    }
    const size_t end = from + size;
    const size_t maxOffset = (1u << o_.windowBits) - 3;
    const auto hashAt = [&](size_t p) {
      return (static_cast<uint8_t>(data[p]) * 2654435761u ^ static_cast<uint8_t>(data[p + 1]) * 40503u ^
              static_cast<uint8_t>(data[p + 2]) * 257u) &
             0xFFFF;
    };
    const auto insertUpTo = [&](size_t p) {
      for (; hashedTo_ < p; hashedTo_++) {
        if (hashedTo_ + 2 < data.size()) hashHead_[hashAt(hashedTo_)] = hashedTo_;
      }
    };
    size_t pos = from;
    uint32_t r0 = 0;  // the offset of the match before, to try again
    while (pos < end) {
      insertUpTo(pos);
      const size_t limit = std::min({end - pos, static_cast<size_t>(257), FRAME - pos % FRAME});
      size_t best = 0;
      uint32_t bestOffset = 0;
      const auto tryOffset = [&](size_t offset) {
        if (offset == 0 || offset > pos - spanStart || offset > maxOffset) return;
        size_t len = 0;
        while (len < limit && data[pos + len] == data[pos + len - offset]) len++;
        if (len > best) {
          best = len;
          bestOffset = static_cast<uint32_t>(offset);
        }
      };
      if (r0 != 0) tryOffset(r0);
      if (pos + 2 < data.size() && hashHead_[hashAt(pos)] != SIZE_MAX) tryOffset(pos - hashHead_[hashAt(pos)]);
      if (best >= 3 || (best == 2 && bestOffset == r0)) {
        tokens.push_back({bestOffset, static_cast<uint32_t>(best), 0});
        r0 = bestOffset;
        pos += best;
      } else {
        tokens.push_back({0, 1, static_cast<uint8_t>(data[pos])});
        pos++;
      }
    }
    return tokens;
  }

  // What a match is written as, and the repeated offsets after it. `apply` false leaves them as they were.
  Formatted format(const LzxToken& token, bool apply) {
    uint32_t r[3] = {r_[0], r_[1], r_[2]};
    Formatted f{};
    f.lengthSymbol = 0xFFFFFFFF;
    if (token.offset == r[0]) {
      f.slot = 0;
    } else if (token.offset == r[1]) {
      f.slot = 1;
      std::swap(r[0], r[1]);
    } else if (token.offset == r[2]) {
      f.slot = 2;
      std::swap(r[0], r[2]);
    } else {
      const uint32_t formatted = token.offset + 2;
      f.slot = 3;
      while (f.slot + 1 < slots_ && bases_[f.slot + 1] <= formatted) f.slot++;
      f.extra = formatted - bases_[f.slot];
      r[2] = r[1];
      r[1] = r[0];
      r[0] = token.offset;
    }
    const uint32_t len = token.length - 2;
    f.mainSymbol = 256 + f.slot * 8 + std::min<uint32_t>(len, 7);
    if (len >= 7) f.lengthSymbol = len - 7;
    if (apply) memcpy(r_, r, sizeof(r));
    return f;
  }

  void writeLengths(BitWriter& w, const std::vector<uint8_t>& lens, std::vector<uint8_t>& prev, size_t first,
                    size_t last) {
    // The pre-tree symbols: a difference from the block before, or a run (17 / 18 zeros, 19 the same value).
    struct Op {
      uint8_t symbol;
      uint8_t extra;
      uint8_t second;
    };
    std::vector<Op> ops;
    size_t x = first;
    while (x < last) {
      size_t run = 1;
      while (x + run < last && lens[x + run] == lens[x]) run++;
      const auto delta = [&](size_t at) { return static_cast<uint8_t>((prev[at] + 17 - lens[at]) % 17); };
      if (lens[x] == 0 && run >= 20) {
        const size_t n = std::min<size_t>(run, 51);
        ops.push_back({18, static_cast<uint8_t>(n - 20), 0});
        x += n;
      } else if (lens[x] == 0 && run >= 4) {
        const size_t n = std::min<size_t>(run, 19);
        ops.push_back({17, static_cast<uint8_t>(n - 4), 0});
        x += n;
      } else if (run >= 4) {
        // 19 writes one value for 4 or 5 places, worked out from the first place's old length.
        const size_t n = std::min<size_t>(run, 5);
        ops.push_back({19, static_cast<uint8_t>(n - 4), delta(x)});
        x += n;
      } else {
        ops.push_back({delta(x), 0, 0});
        x++;
      }
    }
    std::vector<uint32_t> freq(20, 0);
    for (const Op& op : ops) {
      freq[op.symbol]++;
      if (op.symbol == 19) freq[op.second]++;
    }
    const std::vector<uint8_t> preLens = huffmanLengths(freq, 15);
    const std::vector<uint32_t> preCodes = canonicalCodes(preLens);
    for (const uint8_t len : preLens) w.bits(len, 4);
    for (const Op& op : ops) {
      w.bits(preCodes[op.symbol], preLens[op.symbol]);
      if (op.symbol == 17) w.bits(op.extra, 4);
      if (op.symbol == 18) w.bits(op.extra, 5);
      if (op.symbol == 19) {
        w.bits(op.extra, 1);
        w.bits(preCodes[op.second], preLens[op.second]);
      }
    }
    for (size_t i = first; i < last; i++) prev[i] = lens[i];
  }

  void writeTrees(BitWriter& w, BlockKind kind, const std::vector<LzxToken>& tokens) {
    std::vector<uint32_t> mainFreq(mainSymbols_, 0);
    std::vector<uint32_t> lengthFreq(249, 0);
    std::vector<uint32_t> alignedFreq(8, 0);
    uint32_t saved[3];
    memcpy(saved, r_, sizeof(saved));
    for (const LzxToken& token : tokens) {
      if (token.offset == 0) {
        mainFreq[token.literal]++;
        continue;
      }
      const Formatted f = format(token, true);
      mainFreq[f.mainSymbol]++;
      if (f.lengthSymbol != 0xFFFFFFFF) lengthFreq[f.lengthSymbol]++;
      if (f.slot > 2 && lzxExtraBits(f.slot) >= 3) alignedFreq[f.extra & 7]++;
    }
    memcpy(r_, saved, sizeof(saved));
    mainLens_ = huffmanLengths(mainFreq, 16);
    lengthLens_ = huffmanLengths(lengthFreq, 16, false);  // a length tree nobody uses may be empty
    alignedLens_ = huffmanLengths(alignedFreq, 7);
    mainCodes_ = canonicalCodes(mainLens_);
    lengthCodes_ = canonicalCodes(lengthLens_);
    alignedCodes_ = canonicalCodes(alignedLens_);
    if (kind == BlockKind::Aligned) {
      for (const uint8_t len : alignedLens_) w.bits(len, 3);
    }
    writeLengths(w, mainLens_, prevMain_, 0, 256);
    writeLengths(w, mainLens_, prevMain_, 256, mainSymbols_);
    writeLengths(w, lengthLens_, prevLength_, 0, 249);
  }

  void writeToken(BitWriter& w, BlockKind kind, const LzxToken& token) {
    if (token.offset == 0) {
      w.bits(mainCodes_[token.literal], mainLens_[token.literal]);
      return;
    }
    const Formatted f = format(token, true);
    w.bits(mainCodes_[f.mainSymbol], mainLens_[f.mainSymbol]);
    if (f.lengthSymbol != 0xFFFFFFFF) w.bits(lengthCodes_[f.lengthSymbol], lengthLens_[f.lengthSymbol]);
    if (f.slot <= 2) return;
    const unsigned extra = lzxExtraBits(f.slot);
    if (kind == BlockKind::Aligned && extra >= 3) {
      w.bits(f.extra >> 3, extra - 3);
      w.bits(alignedCodes_[f.extra & 7], alignedLens_[f.extra & 7]);
    } else {
      w.bits(f.extra, extra);
    }
  }

  LzxOptions o_;
  unsigned slots_ = 0;
  unsigned mainSymbols_ = 0;
  std::vector<uint32_t> bases_;
  uint32_t r_[3] = {1, 1, 1};
  std::vector<uint8_t> prevMain_, prevLength_;
  std::vector<uint8_t> mainLens_, lengthLens_, alignedLens_;
  std::vector<uint32_t> mainCodes_, lengthCodes_, alignedCodes_;
  std::vector<size_t> hashHead_;
  size_t hashSpan_ = 0;
  size_t hashedTo_ = 0;
};

// ---------------------------------------------------------------------------------------------- CHM writer

struct ChmFile {
  std::string name;  // as in the directory: "/page.htm"
  std::string data;
  bool compressed = true;  // in the MSCompressed section
};

struct ChmOptions {
  LzxOptions lzx;
  uint32_t version = 3;
  uint32_t lcid = 0x0409;
  uint32_t chunkSize = 4096;
  bool withCompression = true;  // the ControlData / ResetTable / Content entries are there
  uint32_t controlVersion = 2;
  uint32_t controlInterval = 0;       // 0: the reset interval the encoder used, in the version's units
  uint32_t controlCache = 0;          // 0: the number that fits the reset interval
  uint32_t controlWindow = 0;         // 0: the window, in the version's units
  std::vector<ChmFile> extraEntries;  // directory entries written as they are, pointing where they say
  uint32_t overlapCount = 0;          // this many more pages, all pointing at the first page's bytes
};

inline void addEncint(std::string& s, uint64_t v) {
  char buf[10];
  int n = 0;
  do {
    buf[n++] = static_cast<char>(v & 0x7F);
    v >>= 7;
  } while (v != 0);
  while (n-- > 0) s += static_cast<char>(buf[n] | (n > 0 ? 0x80 : 0));
}

inline void add64(std::string& s, uint64_t v) {
  add32(s, static_cast<uint32_t>(v));
  add32(s, static_cast<uint32_t>(v >> 32));
}

// #SYSTEM: the contents file's name (code 0), the title (3) and the language (4).
inline std::string buildSystem(const std::string& title, const std::string& hhc = "", uint32_t lcid = 0) {
  std::string s;
  add32(s, 3);
  const auto record = [&](uint16_t code, const std::string& data) {
    add16(s, code);
    add16(s, static_cast<uint16_t>(data.size()));
    s += data;
  };
  if (!hhc.empty()) record(0, hhc + std::string(1, '\0'));
  record(3, title + std::string(1, '\0'));
  if (lcid != 0) {
    std::string data;
    add32(data, lcid);
    data.append(32, '\0');
    record(4, data);
  }
  return s;
}

inline std::string buildChm(const std::vector<ChmFile>& files, const ChmOptions& options = {}) {
  struct DirEntry {
    std::string name;
    uint32_t section;
    uint64_t offset;
    uint64_t length;
  };
  std::vector<DirEntry> dir;
  std::string section0;
  std::string plain;
  for (const ChmFile& file : files) {
    if (file.compressed) {
      dir.push_back({file.name, 1, plain.size(), file.data.size()});
      plain += file.data;
    } else {
      dir.push_back({file.name, 0, section0.size(), file.data.size()});
      section0 += file.data;
    }
  }
  for (uint32_t i = 0; i < options.overlapCount; i++) {
    DirEntry copy = dir[0];
    copy.name = "/overlap" + std::to_string(i) + ".htm";
    dir.push_back(copy);
  }
  for (const ChmFile& extra : options.extraEntries) {
    dir.push_back({extra.name, extra.compressed ? 1u : 0u, 0, extra.data.size()});
  }
  if (options.withCompression) {
    const LzxStream stream = LzxEncoder(options.lzx).encode(plain);
    const uint32_t windowUnits = (1u << options.lzx.windowBits) / FRAME;
    std::string control;
    add32(control, 6);
    control += "LZXC";
    add32(control, options.controlVersion);
    const uint32_t unit = options.controlVersion == 2 ? 1 : FRAME;
    add32(control, options.controlInterval ? options.controlInterval : options.lzx.resetFrames * unit);
    add32(control, options.controlWindow ? options.controlWindow : windowUnits * unit);
    // The third number, which lib/Chm does not use: with this value the other published reading of the three,
    // (interval / half the window) * it, gives the same reset interval.
    add32(control, options.controlCache ? options.controlCache : std::max(1u, windowUnits / 2));
    add32(control, 0);
    std::string table;
    add32(table, 2);
    add32(table, static_cast<uint32_t>(stream.frameStart.size()));
    add32(table, 8);
    add32(table, 0x28);
    add64(table, plain.size());
    add64(table, stream.bytes.size());
    add64(table, FRAME);
    for (const uint32_t start : stream.frameStart) add64(table, start);
    // The three entries below are not read by lib/Chm; they are here, laid out as in a real file, so that other
    // programs (7-Zip) take the file for what it is.
    std::string nameList;
    add16(nameList, 30);
    add16(nameList, 2);
    for (const char* name : {"Uncompressed", "MSCompressed"}) {
      add16(nameList, static_cast<uint16_t>(strlen(name)));
      for (const char* c = name; *c; c++) add16(nameList, static_cast<uint8_t>(*c));
      add16(nameList, 0);
    }
    dir.push_back({"::DataSpace/NameList", 0, section0.size(), nameList.size()});
    section0 += nameList;
    std::string transforms;
    for (const char* c = "{7FC28940-9D31-11D0"; *c; c++) add16(transforms, static_cast<uint8_t>(*c));
    dir.push_back({"::DataSpace/Storage/MSCompressed/Transform/List", 0, section0.size(), transforms.size()});
    section0 += transforms;
    std::string spanInfo;
    add64(spanInfo, plain.size());
    dir.push_back({"::DataSpace/Storage/MSCompressed/SpanInfo", 0, section0.size(), spanInfo.size()});
    section0 += spanInfo;
    dir.push_back({"::DataSpace/Storage/MSCompressed/ControlData", 0, section0.size(), control.size()});
    section0 += control;
    dir.push_back(
        {"::DataSpace/Storage/MSCompressed/Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/"
         "ResetTable",
         0, section0.size(), table.size()});
    section0 += table;
    dir.push_back({"::DataSpace/Storage/MSCompressed/Content", 0, section0.size(), stream.bytes.size()});
    section0 += stream.bytes;
  }
  const auto lowered = [](std::string s) {
    for (char& c : s) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return s;
  };
  std::stable_sort(dir.begin(), dir.end(),
                   [&](const DirEntry& a, const DirEntry& b) { return lowered(a.name) < lowered(b.name); });

  // PMGL chunks: a 20-byte header, the entries, and the unused rest (its size is in the header).
  std::vector<std::string> chunks;
  std::string body;
  const auto closeChunk = [&]() {
    std::string chunk = "PMGL";
    add32(chunk, options.chunkSize - 0x14 - static_cast<uint32_t>(body.size()));
    add32(chunk, 0);
    add32(chunk, chunks.empty() ? 0xFFFFFFFF : static_cast<uint32_t>(chunks.size() - 1));
    add32(chunk, 0xFFFFFFFF);
    chunk += body;
    chunk.resize(options.chunkSize, '\0');
    if (!chunks.empty()) put32(chunks.back(), 0x10, static_cast<uint32_t>(chunks.size()));
    chunks.push_back(chunk);
    body.clear();
  };
  for (const DirEntry& entry : dir) {
    std::string e;
    addEncint(e, entry.name.size());
    e += entry.name;
    addEncint(e, entry.section);
    addEncint(e, entry.offset);
    addEncint(e, entry.length);
    if (body.size() + e.size() > options.chunkSize - 0x14 - 2) closeChunk();
    body += e;
  }
  closeChunk();

  std::string itsp = "ITSP";
  add32(itsp, 1);
  add32(itsp, 0x54);
  add32(itsp, 0x0A);
  add32(itsp, options.chunkSize);
  add32(itsp, 2);
  add32(itsp, 1);
  add32(itsp, 0xFFFFFFFF);
  add32(itsp, 0);
  add32(itsp, static_cast<uint32_t>(chunks.size() - 1));
  add32(itsp, 0xFFFFFFFF);
  add32(itsp, static_cast<uint32_t>(chunks.size()));
  add32(itsp, options.lcid);
  itsp.append(16, '\x11');
  add32(itsp, 0x54);
  itsp.append(12, '\xFF');
  std::string directory = itsp;
  for (const std::string& chunk : chunks) directory += chunk;

  const uint32_t headerLen = options.version >= 3 ? 0x60 : 0x58;
  const uint64_t dirOffset = headerLen + 0x18;
  const uint64_t contentOffset = dirOffset + directory.size();
  std::string file = "ITSF";
  add32(file, options.version);
  add32(file, headerLen);
  add32(file, 1);
  add32(file, 0x12345678);
  add32(file, options.lcid);
  file.append(32, '\x22');
  add64(file, headerLen);
  add64(file, 0x18);
  add64(file, dirOffset);
  add64(file, directory.size());
  if (options.version >= 3) add64(file, contentOffset);
  add32(file, 0x01FE);
  add32(file, 0);
  add64(file, contentOffset + section0.size());
  add64(file, 0);
  return file + directory + section0;
}

// An HTML contents file from (title, page) pairs.
inline std::string buildHhc(const std::vector<std::pair<std::string, std::string>>& entries) {
  std::string s =
      "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n<HTML><HEAD>\n<!-- Sitemap 1.0 -->\n</HEAD><BODY>\n<UL>\n";
  for (const auto& [title, local] : entries) {
    s += "<LI> <OBJECT type=\"text/sitemap\">\n<param name=\"Name\" value=\"" + title +
         "\">\n<param name=\"Local\" value=\"" + local + "\">\n</OBJECT>\n";
  }
  return s + "</UL>\n</BODY></HTML>\n";
}

}  // namespace chmfixture
