#include "Lzx.h"

#include <cstdlib>
#include <cstring>

namespace lzx {
namespace {

constexpr uint8_t BLOCK_VERBATIM = 1;
constexpr uint8_t BLOCK_ALIGNED = 2;
constexpr uint8_t BLOCK_UNCOMPRESSED = 3;

// Position slots for a window of 2^15 .. 2^21 bytes.
constexpr uint8_t SLOTS[7] = {30, 32, 34, 36, 38, 42, 50};

// The extra bits a position slot reads: 0 0 0 0 1 1 2 2 ... up to 17.
unsigned extraBits(const unsigned slot) {
  if (slot < 4) return 0;
  const unsigned bits = (slot >> 1) - 1;
  return bits > 17 ? 17 : bits;
}

// The first formatted offset of a slot: 0 1 2 3 4 6 8 12 16 24 ...
struct Bases {
  uint32_t base[51];
  constexpr Bases() : base{} {
    uint32_t next = 0;
    for (unsigned slot = 0; slot < 51; slot++) {
      base[slot] = next;
      const unsigned half = slot >> 1;
      const unsigned bits = slot < 4 ? 0 : (half - 1 > 17 ? 17 : half - 1);
      next += 1u << bits;
    }
  }
};
constexpr Bases BASES;

}  // namespace

Decoder::~Decoder() {
  if (window_) release_(window_);
}

bool Decoder::init(const unsigned windowBits, void* (*alloc)(size_t), void (*release)(void*)) {
  if (windowBits < MIN_WINDOW_BITS || windowBits > MAX_WINDOW_BITS) return false;
  if (window_) release_(window_);
  if (!alloc || !release) {
    alloc = malloc;
    release = free;
  }
  release_ = release;
  windowSize_ = 1u << windowBits;
  window_ = static_cast<uint8_t*>(alloc(windowSize_));
  if (!window_) return false;
  mainSymbols_ = 256 + SLOTS[windowBits - MIN_WINDOW_BITS] * 8u;
  reset();
  return true;
}

void Decoder::reset() {
  headerRead_ = false;
  blockType_ = 0;
  blockLeft_ = 0;
  padPending_ = false;
  r0_ = r1_ = r2_ = 1;
  history_ = 0;
  pos_ = 0;
  unsupported_ = false;
  memset(mainLens_, 0, sizeof(mainLens_));
  memset(lengthLens_, 0, sizeof(lengthLens_));
}

bool Decoder::Tree::build(const uint8_t* lens, const unsigned symbols) {
  memset(count, 0, sizeof(count));
  for (unsigned s = 0; s < symbols; s++) count[lens[s]]++;
  count[0] = 0;
  int left = 1;  // codes still free at this length: below 0 means more codes than the length can hold
  uint16_t offset[MAX_CODE + 2];
  offset[1] = 0;
  for (unsigned len = 1; len <= MAX_CODE; len++) {
    left = left * 2 - count[len];
    if (left < 0) return false;
    offset[len + 1] = static_cast<uint16_t>(offset[len] + count[len]);
  }
  for (unsigned s = 0; s < symbols; s++) {
    if (lens[s] != 0) sorted[offset[lens[s]]++] = static_cast<uint16_t>(s);
  }
  return true;
}

// The next `n` bits (0 to 17). The stream is 16-bit little-endian words, each read from its top bit down.
bool Decoder::bits(const unsigned n, uint32_t& value) {
  while (bitCount_ < n) {
    if (inSize_ - inPos_ < 2) return false;
    bitBuf_ = (bitBuf_ << 16) | static_cast<uint64_t>(in_[inPos_] | (in_[inPos_ + 1] << 8));
    inPos_ += 2;
    bitCount_ += 16;
  }
  bitCount_ -= n;
  value = static_cast<uint32_t>((bitBuf_ >> bitCount_) & ((1u << n) - 1));
  return true;
}

// One symbol: the code is read a bit at a time and looked for among the codes of its length (they are numbered
// in order, the shortest first).
bool Decoder::symbol(const Tree& tree, uint32_t& value) {
  uint32_t code = 0;
  uint32_t first = 0;
  uint32_t index = 0;
  for (unsigned len = 1; len <= MAX_CODE; len++) {
    uint32_t bit = 0;
    if (!bits(1, bit)) return false;
    code |= bit;
    const uint32_t count = tree.count[len];
    if (code - first < count) {
      value = tree.sorted[index + (code - first)];
      return true;
    }
    index += count;
    first = (first + count) << 1;
    code <<= 1;
  }
  return false;  // a code the tree does not have
}

// Code lengths [first, last) of the main or the length tree: a pre-tree of 20 symbols (4 bits each), then the
// lengths as differences from the lengths of the block before, with runs.
bool Decoder::readLengths(uint8_t* lens, const unsigned first, const unsigned last) {
  uint8_t preLens[PRE_SYMBOLS];
  for (uint8_t& len : preLens) {
    uint32_t v = 0;
    if (!bits(4, v)) return false;
    len = static_cast<uint8_t>(v);
  }
  if (!pre_.build(preLens, PRE_SYMBOLS)) return false;
  unsigned x = first;
  while (x < last) {
    uint32_t z = 0;
    if (!symbol(pre_, z)) return false;
    if (z <= 16) {
      lens[x] = static_cast<uint8_t>((lens[x] + 17 - z) % 17);
      x++;
      continue;
    }
    uint32_t run = 0;
    uint8_t value = 0;
    bool same = false;
    if (z == 17) {
      if (!bits(4, run)) return false;
      run += 4;
    } else if (z == 18) {
      if (!bits(5, run)) return false;
      run += 20;
    } else {
      if (!bits(1, run)) return false;
      run += 4;
      if (!symbol(pre_, z) || z > 16) return false;
      value = static_cast<uint8_t>((lens[x] + 17 - z) % 17);
      same = true;
    }
    if (run > last - x) return false;
    for (; run > 0; run--) lens[x++] = same ? value : 0;
  }
  return true;
}

bool Decoder::blockHeader() {
  if (padPending_) {
    if (inPos_ >= inSize_) return false;
    inPos_++;
    padPending_ = false;
  }
  uint32_t v = 0;
  if (!headerRead_) {
    // The stream header: one bit, and when it is set the 32-bit size the E8 translation works with.
    if (!bits(1, v)) return false;
    if (v) {
      uint32_t high = 0;
      uint32_t low = 0;
      if (!bits(16, high) || !bits(16, low)) return false;
      if (high != 0 || low != 0) {
        unsupported_ = true;
        return false;
      }
    }
    headerRead_ = true;
  }
  uint32_t type = 0;
  uint32_t high = 0;
  uint32_t low = 0;
  if (!bits(3, type) || !bits(16, high) || !bits(8, low)) return false;
  blockType_ = static_cast<uint8_t>(type);
  blockLeft_ = (high << 8) | low;
  if (blockLeft_ == 0) return false;
  blockOdd_ = (blockLeft_ & 1) != 0;
  if (blockType_ == BLOCK_UNCOMPRESSED) {
    // 1 to 16 bits of padding up to the next word, then R0 R1 R2 as bytes.
    if (bitCount_ == 0) {
      if (inSize_ - inPos_ < 2) return false;
      inPos_ += 2;
    }
    bitCount_ = 0;
    bitBuf_ = 0;
    if (inSize_ - inPos_ < 12) return false;
    const auto le32 = [this](const size_t at) {
      return static_cast<uint32_t>(in_[at]) | (static_cast<uint32_t>(in_[at + 1]) << 8) |
             (static_cast<uint32_t>(in_[at + 2]) << 16) | (static_cast<uint32_t>(in_[at + 3]) << 24);
    };
    r0_ = le32(inPos_);
    r1_ = le32(inPos_ + 4);
    r2_ = le32(inPos_ + 8);
    inPos_ += 12;
    return true;
  }
  if (blockType_ != BLOCK_VERBATIM && blockType_ != BLOCK_ALIGNED) return false;
  if (blockType_ == BLOCK_ALIGNED) {
    uint8_t lens[ALIGNED_SYMBOLS];
    for (uint8_t& len : lens) {
      if (!bits(3, v)) return false;
      len = static_cast<uint8_t>(v);
    }
    if (!aligned_.build(lens, ALIGNED_SYMBOLS)) return false;
  }
  return readLengths(mainLens_, 0, 256) && readLengths(mainLens_, 256, mainSymbols_) &&
         main_.build(mainLens_, mainSymbols_) && readLengths(lengthLens_, 0, LENGTH_SYMBOLS) &&
         length_.build(lengthLens_, LENGTH_SYMBOLS);
}

Result Decoder::fail(const Result result, const uint32_t start, const uint32_t needed, const uint8_t*& out) {
  const bool enough = pos_ - start >= needed && result == Result::Corrupt;
  blockLeft_ = 0;  // nothing after this can be trusted until a reset()
  blockType_ = 0;
  headerRead_ = true;
  history_ = 0;
  pos_ = 0;
  if (!enough) return result;
  out = window_ + start;
  return Result::Ok;
}

Result Decoder::decodeFrame(const uint8_t* in, const size_t inSize, const uint32_t needed, const uint8_t*& out) {
  if (!window_ || needed == 0 || needed > FRAME || pos_ + FRAME > windowSize_) return Result::Corrupt;
  in_ = in;
  inSize_ = inSize;
  inPos_ = 0;
  bitBuf_ = 0;
  bitCount_ = 0;
  padPending_ = false;  // the pad byte of a block that ended with the frame before is behind us
  const uint32_t start = pos_;
  const uint32_t mask = windowSize_ - 1;
  uint32_t left = FRAME;
  while (left > 0) {
    if (blockLeft_ == 0 && !blockHeader()) {
      return fail(unsupported_ ? Result::Unsupported : Result::Corrupt, start, needed, out);
    }
    uint32_t run = blockLeft_ < left ? blockLeft_ : left;
    blockLeft_ -= run;
    left -= run;
    if (blockType_ == BLOCK_UNCOMPRESSED) {
      if (run > inSize_ - inPos_) return fail(Result::Corrupt, start, needed, out);
      memcpy(window_ + pos_, in_ + inPos_, run);
      inPos_ += run;
      pos_ += run;
      history_ = history_ + run > windowSize_ ? windowSize_ : history_ + run;
      if (blockLeft_ == 0) padPending_ = blockOdd_;
      continue;
    }
    while (run > 0) {
      uint32_t sym = 0;
      if (!symbol(main_, sym)) return fail(Result::Corrupt, start, needed, out);
      if (sym < 256) {
        window_[pos_++] = static_cast<uint8_t>(sym);
        if (history_ < windowSize_) history_++;
        run--;
        continue;
      }
      sym -= 256;
      uint32_t length = sym & 7;
      const unsigned slot = sym >> 3;
      if (length == 7) {
        uint32_t more = 0;
        if (!symbol(length_, more)) return fail(Result::Corrupt, start, needed, out);
        length += more;
      }
      length += 2;
      uint32_t offset = 0;
      if (slot == 0) {
        offset = r0_;
      } else if (slot == 1) {
        offset = r1_;
        r1_ = r0_;
        r0_ = offset;
      } else if (slot == 2) {
        offset = r2_;
        r2_ = r0_;
        r0_ = offset;
      } else {
        const unsigned extra = extraBits(slot);
        uint32_t add = 0;
        if (blockType_ == BLOCK_ALIGNED && extra >= 3) {
          uint32_t top = 0;
          uint32_t low = 0;
          if (!bits(extra - 3, top) || !symbol(aligned_, low)) return fail(Result::Corrupt, start, needed, out);
          add = (top << 3) + low;
        } else if (extra > 0) {
          if (!bits(extra, add)) return fail(Result::Corrupt, start, needed, out);
        }
        offset = BASES.base[slot] + add - 2;
        r2_ = r1_;
        r1_ = r0_;
        r0_ = offset;
      }
      if (offset == 0 || offset > history_ || length > run) return fail(Result::Corrupt, start, needed, out);
      for (uint32_t i = 0; i < length; i++) {
        window_[pos_] = window_[(pos_ - offset) & mask];
        pos_++;
      }
      history_ = history_ + length > windowSize_ ? windowSize_ : history_ + length;
      run -= length;
    }
  }
  out = window_ + start;
  pos_ &= mask;
  return Result::Ok;
}

}  // namespace lzx
