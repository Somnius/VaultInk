#include "PngSlowDecoder.h"

#include <InflateStream.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace pngslow {
namespace {

constexpr uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint8_t COLOR_GRAY = 0;
constexpr uint8_t COLOR_RGB = 2;
constexpr uint8_t COLOR_INDEXED = 3;
constexpr uint8_t COLOR_GRAY_ALPHA = 4;
constexpr uint8_t COLOR_RGB_ALPHA = 6;

uint32_t be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

int channelsOf(const uint8_t colorType) {
  switch (colorType) {
    case COLOR_RGB:
      return 3;
    case COLOR_GRAY_ALPHA:
      return 2;
    case COLOR_RGB_ALPHA:
      return 4;
    default:
      return 1;
  }
}

bool depthAllowed(const uint8_t colorType, const uint8_t depth) {
  switch (colorType) {
    case COLOR_GRAY:
      return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case COLOR_INDEXED:
      return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case COLOR_RGB:
    case COLOR_GRAY_ALPHA:
    case COLOR_RGB_ALPHA:
      return depth == 8 || depth == 16;
    default:
      return false;
  }
}

uint64_t packedBytes(const uint64_t pixels, const Header& header) {
  return (pixels * channelsOf(header.colorType) * header.bitDepth + 7) / 8;
}

int64_t ceilDiv(const int64_t a, const int64_t b) { return (a + b - 1) / b; }

uint8_t luma(const uint8_t r, const uint8_t g, const uint8_t b) {
  return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
}

uint8_t overWhite(const uint8_t gray, const uint8_t alpha) {
  return static_cast<uint8_t>((gray * alpha + 255u * (255u - alpha)) / 255u);
}

// Everything the chunk walk and the inflate callback share. The file is only ever read forwards.
struct Stream {
  ReadFn read = nullptr;
  void* user = nullptr;
  const Header* header = nullptr;
  uint32_t idatLeft = 0;  // bytes of the current IDAT chunk not handed to inflate yet
  bool inIdat = false;
  bool idatDone = false;
  bool failed = false;
  uint8_t buffer[1024];

  uint8_t palette[768];
  int paletteEntries = 0;
  uint8_t paletteAlpha[256];
  bool hasKey = false;  // tRNS of a gray or truecolor picture: one fully transparent colour
  uint16_t key[3] = {0, 0, 0};

  bool readExact(uint8_t* dst, size_t len) {
    while (len > 0) {
      const int want = len > 0x4000 ? 0x4000 : static_cast<int>(len);
      const int got = read(user, dst, want);
      if (got <= 0) return false;
      dst += got;
      len -= static_cast<size_t>(got);
    }
    return true;
  }

  bool skip(uint64_t len) {
    while (len > 0) {
      const size_t step = len > sizeof(buffer) ? sizeof(buffer) : static_cast<size_t>(len);
      if (!readExact(buffer, step)) return false;
      len -= step;
    }
    return true;
  }

  // Walks the chunks up to the next IDAT with data in it. False at IEND, at the end of the file,
  // or once the IDAT run is over.
  bool nextIdat() {
    for (;;) {
      uint8_t head[8];
      if (!readExact(head, sizeof(head))) return false;
      const uint32_t length = be32(head);
      if (memcmp(head + 4, "IDAT", 4) == 0) {
        inIdat = true;
        if (length == 0) {
          if (!skip(4)) return false;
          continue;
        }
        idatLeft = length;
        return true;
      }
      if (inIdat || memcmp(head + 4, "IEND", 4) == 0) return false;  // IDAT chunks are consecutive
      if (memcmp(head + 4, "PLTE", 4) == 0 && length <= sizeof(palette) && length % 3 == 0) {
        if (!readExact(palette, length)) return false;
        paletteEntries = static_cast<int>(length / 3);
      } else if (memcmp(head + 4, "tRNS", 4) == 0 && length <= 256) {
        uint8_t data[256];
        if (!readExact(data, length)) return false;
        if (header->colorType == COLOR_INDEXED) {
          memcpy(paletteAlpha, data, length);
        } else if (header->colorType == COLOR_GRAY && length >= 2) {
          hasKey = true;
          key[0] = static_cast<uint16_t>((data[0] << 8) | data[1]);
        } else if (header->colorType == COLOR_RGB && length >= 6) {
          hasKey = true;
          for (int i = 0; i < 3; i++) key[i] = static_cast<uint16_t>((data[2 * i] << 8) | data[2 * i + 1]);
        }
      } else if (!skip(length)) {
        return false;
      }
      if (!skip(4)) return false;  // the chunk's CRC
    }
  }

  static size_t fill(void* ctx, const uint8_t** data) {
    Stream& s = *static_cast<Stream*>(ctx);
    if (s.idatDone || s.failed) return 0;
    if (s.idatLeft == 0) {
      // The CRC of the IDAT just finished, then on to the next one.
      if (!s.skip(4) || !s.nextIdat()) {
        s.idatDone = true;
        return 0;
      }
    }
    const size_t want = s.idatLeft > sizeof(s.buffer) ? sizeof(s.buffer) : s.idatLeft;
    if (!s.readExact(s.buffer, want)) {
      s.failed = true;
      return 0;
    }
    s.idatLeft -= static_cast<uint32_t>(want);
    *data = s.buffer;
    return want;
  }
};

bool unfilter(const uint8_t filter, uint8_t* row, const uint8_t* prev, const size_t len, const size_t bpp) {
  switch (filter) {
    case 0:
      return true;
    case 1:
      for (size_t i = bpp; i < len; i++) row[i] = static_cast<uint8_t>(row[i] + row[i - bpp]);
      return true;
    case 2:
      for (size_t i = 0; i < len; i++) row[i] = static_cast<uint8_t>(row[i] + prev[i]);
      return true;
    case 3:
      for (size_t i = 0; i < len; i++) {
        const unsigned left = i >= bpp ? row[i - bpp] : 0;
        row[i] = static_cast<uint8_t>(row[i] + ((left + prev[i]) >> 1));
      }
      return true;
    case 4:
      for (size_t i = 0; i < len; i++) {
        const int a = i >= bpp ? row[i - bpp] : 0;
        const int b = prev[i];
        const int c = i >= bpp ? prev[i - bpp] : 0;
        const int p = a + b - c;
        const int pa = abs(p - a);
        const int pb = abs(p - b);
        const int pc = abs(p - c);
        const int predictor = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
        row[i] = static_cast<uint8_t>(row[i] + predictor);
      }
      return true;
    default:
      return false;
  }
}

// Sample `index` of a scanline whose samples are 1, 2 or 4 bits wide, leftmost in the high bits.
uint8_t packedSample(const uint8_t* row, const size_t index, const int depth) {
  const size_t bit = index * static_cast<size_t>(depth);
  return static_cast<uint8_t>((row[bit >> 3] >> (8 - depth - static_cast<int>(bit & 7))) & ((1u << depth) - 1));
}

// Pixel `index` of an unfiltered scanline as 8-bit gray on white.
uint8_t grayAt(const uint8_t* row, const size_t index, const Header& header, const Stream& stream) {
  const int depth = header.bitDepth;
  switch (header.colorType) {
    case COLOR_GRAY: {
      if (depth == 16) {
        const uint16_t value = static_cast<uint16_t>((row[index * 2] << 8) | row[index * 2 + 1]);
        return stream.hasKey && value == stream.key[0] ? 255 : row[index * 2];
      }
      const uint8_t sample = depth == 8 ? row[index] : packedSample(row, index, depth);
      if (stream.hasKey && sample == stream.key[0]) return 255;
      return depth == 8 ? sample : static_cast<uint8_t>(sample * 255u / ((1u << depth) - 1));
    }
    case COLOR_RGB: {
      if (depth == 16) {
        const uint8_t* p = row + index * 6;
        if (stream.hasKey) {
          const uint16_t r = static_cast<uint16_t>((p[0] << 8) | p[1]);
          const uint16_t g = static_cast<uint16_t>((p[2] << 8) | p[3]);
          const uint16_t b = static_cast<uint16_t>((p[4] << 8) | p[5]);
          if (r == stream.key[0] && g == stream.key[1] && b == stream.key[2]) return 255;
        }
        return luma(p[0], p[2], p[4]);
      }
      const uint8_t* p = row + index * 3;
      if (stream.hasKey && p[0] == stream.key[0] && p[1] == stream.key[1] && p[2] == stream.key[2]) return 255;
      return luma(p[0], p[1], p[2]);
    }
    case COLOR_INDEXED: {
      const uint8_t entry = depth == 8 ? row[index] : packedSample(row, index, depth);
      if (entry >= stream.paletteEntries) return 0;  // outside the palette: black
      const uint8_t* p = stream.palette + entry * 3;
      return overWhite(luma(p[0], p[1], p[2]), stream.paletteAlpha[entry]);
    }
    case COLOR_GRAY_ALPHA:
      return depth == 16 ? overWhite(row[index * 4], row[index * 4 + 2])
                         : overWhite(row[index * 2], row[index * 2 + 1]);
    case COLOR_RGB_ALPHA: {
      if (depth == 16) {
        const uint8_t* p = row + index * 8;
        return overWhite(luma(p[0], p[2], p[4]), p[6]);
      }
      const uint8_t* p = row + index * 4;
      return overWhite(luma(p[0], p[1], p[2]), p[3]);
    }
    default:
      return 255;
  }
}

struct Pass {
  uint8_t xStart, yStart, xStep, yStep;
};
constexpr Pass ADAM7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                           {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
constexpr Pass WHOLE = {0, 0, 1, 1};

}  // namespace

bool parseHeader(const uint8_t* bytes, const size_t len, Header& out) {
  if (!bytes || len < HEADER_BYTES) return false;
  if (memcmp(bytes, SIGNATURE, sizeof(SIGNATURE)) != 0) return false;
  if (be32(bytes + 8) != 13 || memcmp(bytes + 12, "IHDR", 4) != 0) return false;
  Header header;
  header.width = be32(bytes + 16);
  header.height = be32(bytes + 20);
  header.bitDepth = bytes[24];
  header.colorType = bytes[25];
  header.interlace = bytes[28];
  if (header.width == 0 || header.height == 0 || header.width > 0x7FFFFFFFu || header.height > 0x7FFFFFFFu) {
    return false;
  }
  if (!depthAllowed(header.colorType, header.bitDepth)) return false;
  if (bytes[26] != 0 || bytes[27] != 0 || header.interlace > 1) return false;  // compression, filter method
  out = header;
  return true;
}

const char* fastDecoderRefusal(const Header& header) {
  const bool deep = header.bitDepth > 8;
  if (header.interlace && deep) return "interlaced, 16 bits per channel";
  if (header.interlace) return "interlaced";
  if (deep) return "16 bits per channel";
  return nullptr;
}

uint64_t rowBytes(const Header& header) { return packedBytes(header.width, header); }

const char* describe(const Result result) {
  switch (result) {
    case Result::Ok:
      return "ok";
    case Result::BadFile:
      return "damaged or cut-off file";
    case Result::TooLarge:
      return "picture too wide";
    case Result::NoMemory:
      return "not enough memory";
    default:
      return "bad arguments";
  }
}

Result decodeGray(const ReadFn read, void* user, const Header& header, const Region& region, uint8_t* canvas,
                  const int outWidth, const int outHeight, const TickFn tick, void* tickUser) {
  if (!read || !canvas || outWidth <= 0 || outHeight <= 0) return Result::BadArguments;
  if (!depthAllowed(header.colorType, header.bitDepth) || header.width == 0 || header.height == 0) {
    return Result::BadArguments;
  }
  if (region.left < 0 || region.top < 0 || region.width <= 0 || region.height <= 0 ||
      static_cast<uint64_t>(region.left) + static_cast<uint64_t>(region.width) > header.width ||
      static_cast<uint64_t>(region.top) + static_cast<uint64_t>(region.height) > header.height) {
    return Result::BadArguments;
  }
  const uint64_t fullRow = rowBytes(header);
  if (fullRow > MAX_ROW_BYTES) return Result::TooLarge;

  memset(canvas, 255, static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight));

  // The 32 KB inflate window first: it is the block a fragmented heap is least likely to have.
  InflateStream inflate;
  if (!inflate.init(true)) return Result::NoMemory;
  std::unique_ptr<Stream> stream(new (std::nothrow) Stream());
  // Each scanline is read with its filter byte in front.
  std::unique_ptr<uint8_t[]> lines(new (std::nothrow) uint8_t[2 * (static_cast<size_t>(fullRow) + 1)]);
  if (!stream || !lines) return Result::NoMemory;
  stream->read = read;
  stream->user = user;
  stream->header = &header;
  memset(stream->paletteAlpha, 255, sizeof(stream->paletteAlpha));

  uint8_t signature[8];
  if (!stream->readExact(signature, sizeof(signature)) || memcmp(signature, SIGNATURE, sizeof(SIGNATURE)) != 0) {
    return Result::BadFile;
  }
  if (!stream->nextIdat()) return Result::BadFile;
  if (header.colorType == COLOR_INDEXED && stream->paletteEntries == 0) return Result::BadFile;

  inflate.setFill(&Stream::fill, stream.get());
  inflate.setZlibWrapped();

  const size_t bytesPerPixel = static_cast<size_t>((channelsOf(header.colorType) * header.bitDepth + 7) / 8);
  uint8_t* current = lines.get();
  uint8_t* previous = lines.get() + fullRow + 1;

  const int passes = header.interlace ? 7 : 1;
  for (int passIndex = 0; passIndex < passes; passIndex++) {
    const Pass pass = header.interlace ? ADAM7[passIndex] : WHOLE;
    if (pass.xStart >= header.width || pass.yStart >= header.height) continue;  // no pixels, so no data either
    const uint32_t passWidth = (header.width - pass.xStart + pass.xStep - 1) / pass.xStep;
    const uint32_t passHeight = (header.height - pass.yStart + pass.yStep - 1) / pass.yStep;
    const size_t passRow = static_cast<size_t>(packedBytes(passWidth, header));
    memset(previous, 0, passRow + 1);

    for (uint32_t r = 0; r < passHeight; r++) {
      if (!inflate.read(current, passRow + 1)) return Result::BadFile;
      if (!unfilter(current[0], current + 1, previous + 1, passRow, bytesPerPixel)) return Result::BadFile;
      if (tick) tick(tickUser);

      // Which output rows sample this source row, if any.
      const int64_t ry = static_cast<int64_t>(pass.yStart) + static_cast<int64_t>(r) * pass.yStep - region.top;
      if (ry >= 0 && ry < region.height) {
        const int64_t dyFirst = ceilDiv(ry * outHeight, region.height);
        int64_t dyEnd = ceilDiv((ry + 1) * outHeight, region.height);
        if (dyEnd > outHeight) dyEnd = outHeight;
        for (int64_t dy = dyFirst; dy < dyEnd; dy++) {
          uint8_t* out = canvas + static_cast<size_t>(dy) * static_cast<size_t>(outWidth);
          for (int dx = 0; dx < outWidth; dx++) {
            const int64_t sx = region.left + (static_cast<int64_t>(dx) * region.width) / outWidth;
            if (sx < pass.xStart || (sx - pass.xStart) % pass.xStep != 0) continue;  // another pass brings it
            out[dx] = grayAt(current + 1, static_cast<size_t>((sx - pass.xStart) / pass.xStep), header, *stream);
          }
        }
      }
      uint8_t* swap = current;
      current = previous;
      previous = swap;
    }
  }
  return stream->failed ? Result::BadFile : Result::Ok;
}

}  // namespace pngslow
