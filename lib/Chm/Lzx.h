#pragma once

// LZX decompression as CHM files use it (the "MSCompressed" section). Platform-free, host-tested
// (test/markup_books). Our own code, not taken from chmlib, libmspack or 7-Zip. It was written from what is
// published about the format:
//  - Microsoft, [MS-PATCH] "LZX DELTA Compression and Decompression" (the bitstream of 16-bit little-endian words
//    read from the top bit, the block types 1 verbatim / 2 aligned offset / 3 uncompressed, the pre-tree with its
//    run codes 17 / 18 / 19, the main tree of 256 literals plus 8 length headers per position slot, the length
//    tree of 249 symbols, the aligned tree of 8, the three repeated offsets R0 R1 R2, the E8 call translation);
//  - Microsoft's "LZX data format" text in the Cabinet SDK, for the same things in the older wording;
//  - Matthew Russotto's "Microsoft's HTML Help (.chm) format" notes, for how a CHM cuts the stream into frames of
//    32768 bytes and resets it.
// No network was at hand while writing, so those were used from memory, and then checked two ways:
//  - a real .chm made by Microsoft's compiler (one frame, an aligned offset block, a 64 KB window) decodes to
//    the same bytes 7-Zip gets from it;
//  - the files the tests' own encoder writes (all three block types, odd uncompressed blocks, blocks that run
//    across frames, resets, windows of 128 KB and 2 MB) are read by 7-Zip to the same bytes as by this decoder
//    (test ChmBook.WritesItsFixturesWhenAsked writes them out for that).
// So the padding before an uncompressed block (1 to 16 bits, not 0 to 15) and the rest of the bitstream have
// 7-Zip's decoder agreeing with ours. Not checked with a real file: verbatim and uncompressed blocks, more
// than one frame, a reset. docs/book-formats.md says the same for readers.
//
// Not done: the E8 call translation (for x86 programs; a stream that asks for it is Unsupported) and a match
// that reaches back past the last reset (Corrupt).
//
// Memory: the window (32 KB to 2 MB, from the allocator given to init()) and about 3 KB of tables. A window above
// MAX_WINDOW_BITS is refused by init().

#include <cstddef>
#include <cstdint>

namespace lzx {

inline constexpr uint32_t FRAME = 32768;
inline constexpr unsigned MIN_WINDOW_BITS = 15;
inline constexpr unsigned MAX_WINDOW_BITS = 21;  // 2 MB
// The most compressed bytes one frame is given (a frame of uncompressed blocks is a little over FRAME).
inline constexpr size_t MAX_FRAME_INPUT = FRAME + 6144;

enum class Result : uint8_t { Ok, Corrupt, Unsupported };

class Decoder {
 public:
  Decoder() = default;
  ~Decoder();
  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  // Allocates the window with `alloc` (malloc when null). False when there is no memory or the size is not
  // 2^15 .. 2^21.
  bool init(unsigned windowBits, void* (*alloc)(size_t) = nullptr, void (*release)(void*) = nullptr);
  // A reset point of the stream: the trees and the repeated offsets are forgotten and the stream header is read
  // again. The first frame after init() needs no reset().
  void reset();
  // One frame: `in` is all of its compressed bytes. `out` points into the window and stays good until the next
  // call. A frame is FRAME bytes, but the last one of a stream may be written either way: as its real bytes
  // only, or filled up to FRAME (Microsoft's compiler does that, seen in a real file). So the last frame is
  // asked for with `needed` below FRAME: if the stream ends or breaks once `needed` bytes are out, that is Ok,
  // and the next call must follow a reset().
  Result decodeFrame(const uint8_t* in, size_t inSize, uint32_t needed, const uint8_t*& out);

 private:
  static constexpr unsigned MAX_SLOTS = 50;
  static constexpr unsigned MAIN_MAX = 256 + MAX_SLOTS * 8;
  static constexpr unsigned LENGTH_SYMBOLS = 249;
  static constexpr unsigned PRE_SYMBOLS = 20;
  static constexpr unsigned ALIGNED_SYMBOLS = 8;
  static constexpr unsigned MAX_CODE = 16;

  // A canonical Huffman code: the symbols sorted by code length, and how many codes each length has.
  struct Tree {
    uint16_t* sorted;
    uint16_t count[MAX_CODE + 1];
    bool build(const uint8_t* lens, unsigned symbols);
  };

  bool bits(unsigned n, uint32_t& value);
  bool symbol(const Tree& tree, uint32_t& value);
  bool readLengths(uint8_t* lens, unsigned first, unsigned last);
  bool blockHeader();
  Result fail(Result result, uint32_t start, uint32_t needed, const uint8_t*& out);

  uint8_t* window_ = nullptr;
  void (*release_)(void*) = nullptr;
  uint32_t windowSize_ = 0;
  uint32_t pos_ = 0;      // where the next frame goes in the window
  uint32_t history_ = 0;  // bytes decoded since the last reset, up to the window size
  unsigned mainSymbols_ = 0;

  // The stream state, kept from frame to frame.
  bool headerRead_ = false;
  uint8_t blockType_ = 0;
  uint32_t blockLeft_ = 0;
  bool blockOdd_ = false;
  bool padPending_ = false;  // an uncompressed block of odd size just ended: one byte to skip
  uint32_t r0_ = 1, r1_ = 1, r2_ = 1;
  bool unsupported_ = false;

  // The input of the frame being decoded.
  const uint8_t* in_ = nullptr;
  size_t inSize_ = 0;
  size_t inPos_ = 0;
  uint64_t bitBuf_ = 0;
  unsigned bitCount_ = 0;

  uint8_t mainLens_[MAIN_MAX] = {};
  uint8_t lengthLens_[LENGTH_SYMBOLS] = {};
  uint16_t mainSorted_[MAIN_MAX] = {};
  uint16_t lengthSorted_[LENGTH_SYMBOLS] = {};
  uint16_t preSorted_[PRE_SYMBOLS] = {};
  uint16_t alignedSorted_[ALIGNED_SYMBOLS] = {};
  Tree main_{mainSorted_, {}};
  Tree length_{lengthSorted_, {}};
  Tree pre_{preSorted_, {}};
  Tree aligned_{alignedSorted_, {}};
};

}  // namespace lzx
