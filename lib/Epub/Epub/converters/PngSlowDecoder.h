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
// Memory: the canvas, two source scanlines, and the inflate window (about 40 KB).
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

// Widest scanline taken: 8192 px of 16-bit RGBA. Two of them are held while decoding.
constexpr uint64_t MAX_ROW_BYTES = 64 * 1024;

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

enum class Result { Ok, BadFile, TooLarge, NoMemory, BadArguments };
const char* describe(Result result);

// Decodes `region` of the picture into canvas (outWidth * outHeight bytes, 8-bit gray, row by row).
// Each output pixel takes the one source pixel at its top-left sample point (nearest neighbour), so
// the result does not depend on the order of the passes.
// `read` must start at the first byte of the file.
Result decodeGray(ReadFn read, void* user, const Header& header, const Region& region, uint8_t* canvas, int outWidth,
                  int outHeight, TickFn tick = nullptr, void* tickUser = nullptr);

}  // namespace pngslow
