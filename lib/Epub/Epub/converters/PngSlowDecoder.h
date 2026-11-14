#pragma once

#include <cstddef>
#include <cstdint>

// A second PNG decoder for the pictures PNGdec refuses: interlaced (Adam7) ones and ones with 16
// bits per channel. PNGdec answers both with PNG_UNSUPPORTED_FEATURE (5) as soon as it has read
// the header, so those pictures showed as an empty frame in the Gallery and nothing in a book.
//
// It trades speed for reach. PNGdec hands out finished rows top to bottom, which is what lets the
// caller scale, dither and cache one row at a time. An interlaced picture arrives in seven passes
// over the whole area, so this decoder samples straight into a canvas of the output size (one gray
// byte per output pixel, transparency blended onto white) and the caller draws that afterwards.
// Memory: the caller's canvas; about 43 KB of heap for the inflate window, its state and the chunk
// reader; and two source scanlines, each up to MAX_ROW_BYTES in PSRAM, or up to MAX_HEAP_ROW_BYTES
// on the heap of a board without PSRAM.
namespace pngslow {

constexpr size_t HEADER_BYTES = 29;  // signature, IHDR length and name, the 13 IHDR bytes

struct Header {
  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t bitDepth = 0;
  uint8_t colorType = 0;
  uint8_t interlace = 0;
};

// Reads the first HEADER_BYTES of a file. False when it is not a PNG or the IHDR values are not
// ones the format allows.
bool parseHeader(const uint8_t* bytes, size_t len, Header& out);

// Why PNGdec will not take this picture ("interlaced", "16 bits per channel", or both), or nullptr
// when it will.
const char* fastDecoderRefusal(const Header& header);

// Bytes of one full-width source scanline, without its filter byte.
uint64_t rowBytes(const Header& header);

// Widest scanline taken when the two scanline buffers come from PSRAM: 8192 px of 16-bit RGBA.
constexpr uint64_t MAX_ROW_BYTES = 64 * 1024;
// And when they have to come from the heap: 4096 px of 8-bit RGBA, 2048 px of 16-bit RGBA.
constexpr uint64_t MAX_HEAP_ROW_BYTES = 16 * 1024;

// Where the scanline buffers come from. The default asks PSRAM first, then the heap; tests pass their own.
struct RowMemory {
  uint8_t* (*allocate)(size_t bytes, bool psram);
  void (*release)(uint8_t* block, bool psram);
};

// Sequential read from the start of the file: up to len bytes into dst, returns the count (0 at the end).
using ReadFn = int (*)(void* user, uint8_t* dst, int len);
// Called once per decoded scanline, so a long decode can yield.
using TickFn = void (*)(void* user);

// The part of the picture to show, in source pixels.
struct Region {
  int left = 0;
  int top = 0;
  int width = 0;
  int height = 0;
};

// TooLarge: a scanline over MAX_ROW_BYTES, or over MAX_HEAP_ROW_BYTES with no PSRAM to hold it.
enum class Result { Ok, BadFile, TooLarge, NoMemory, BadArguments };
const char* describe(Result result);

// Decodes `region` of the picture into canvas (outWidth * outHeight bytes, 8-bit gray, row by row).
// Each output pixel takes the one source pixel at its top-left sample point (nearest neighbour), so
// the result does not depend on the order of the passes.
// `read` must start at the first byte of the file.
Result decodeGray(ReadFn read, void* user, const Header& header, const Region& region, uint8_t* canvas, int outWidth,
                  int outHeight, TickFn tick = nullptr, void* tickUser = nullptr, const RowMemory* memory = nullptr);

}  // namespace pngslow
