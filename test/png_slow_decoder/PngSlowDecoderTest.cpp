#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "PngSlowDecoder.h"

namespace {

struct Fixture {
  const char* name;
  int width;
  int height;
  int colorType;
  int bitDepth;
  int interlace;
  const uint8_t* png;
  size_t pngSize;
  const uint8_t* gray;  // width * height, what each pixel must decode to
};

#include "fixtures.inc"

// The file as the decoder sees it: forward reads only, in pieces of at most `piece` bytes.
struct Source {
  const uint8_t* data;
  size_t size;
  size_t pos = 0;
  size_t piece = 4096;

  static int read(void* user, uint8_t* dst, int len) {
    auto* self = static_cast<Source*>(user);
    size_t count = static_cast<size_t>(len);
    if (count > self->piece) count = self->piece;
    if (count > self->size - self->pos) count = self->size - self->pos;
    memcpy(dst, self->data + self->pos, count);
    self->pos += count;
    return static_cast<int>(count);
  }
};

pngslow::Header headerOf(const Fixture& f) {
  pngslow::Header header;
  EXPECT_TRUE(pngslow::parseHeader(f.png, f.pngSize, header)) << f.name;
  return header;
}

std::vector<uint8_t> decode(const Fixture& f, const pngslow::Region& region, const int outWidth, const int outHeight,
                            const size_t piece = 4096) {
  Source source{f.png, f.pngSize, 0, piece};
  std::vector<uint8_t> canvas(static_cast<size_t>(outWidth) * outHeight, 7);
  const auto result =
      pngslow::decodeGray(&Source::read, &source, headerOf(f), region, canvas.data(), outWidth, outHeight);
  EXPECT_EQ(result, pngslow::Result::Ok) << f.name << ": " << pngslow::describe(result);
  return canvas;
}

// What nearest-neighbour sampling of the reference pixels gives for the same region and size.
std::vector<uint8_t> sampled(const Fixture& f, const pngslow::Region& region, const int outWidth, const int outHeight) {
  std::vector<uint8_t> out(static_cast<size_t>(outWidth) * outHeight);
  for (int y = 0; y < outHeight; y++) {
    for (int x = 0; x < outWidth; x++) {
      const int sx = region.left + x * region.width / outWidth;
      const int sy = region.top + y * region.height / outHeight;
      out[static_cast<size_t>(y) * outWidth + x] = f.gray[sy * f.width + sx];
    }
  }
  return out;
}

TEST(PngSlowDecoder, HeaderCarriesSizeDepthAndInterlace) {
  for (const Fixture& f : kFixtures) {
    const pngslow::Header header = headerOf(f);
    EXPECT_EQ(header.width, static_cast<uint32_t>(f.width)) << f.name;
    EXPECT_EQ(header.height, static_cast<uint32_t>(f.height)) << f.name;
    EXPECT_EQ(header.colorType, f.colorType) << f.name;
    EXPECT_EQ(header.bitDepth, f.bitDepth) << f.name;
    EXPECT_EQ(header.interlace, f.interlace) << f.name;
  }
}

TEST(PngSlowDecoder, NamesWhatTheFastDecoderRefuses) {
  pngslow::Header header;
  header.bitDepth = 8;
  EXPECT_EQ(pngslow::fastDecoderRefusal(header), nullptr);
  header.interlace = 1;
  EXPECT_STREQ(pngslow::fastDecoderRefusal(header), "interlaced");
  header.bitDepth = 16;
  EXPECT_STREQ(pngslow::fastDecoderRefusal(header), "interlaced, 16 bits per channel");
  header.interlace = 0;
  EXPECT_STREQ(pngslow::fastDecoderRefusal(header), "16 bits per channel");
  // Every fixture but the plain 8-bit one is a picture PNGdec turns away.
  for (const Fixture& f : kFixtures) {
    const bool refused = pngslow::fastDecoderRefusal(headerOf(f)) != nullptr;
    EXPECT_EQ(refused, f.interlace != 0 || f.bitDepth == 16) << f.name;
  }
}

TEST(PngSlowDecoder, RejectsHeadersThatAreNotPng) {
  const Fixture& f = kFixtures[0];
  pngslow::Header header;
  EXPECT_FALSE(pngslow::parseHeader(f.png, pngslow::HEADER_BYTES - 1, header));
  EXPECT_FALSE(pngslow::parseHeader(nullptr, 64, header));

  std::vector<uint8_t> bytes(f.png, f.png + pngslow::HEADER_BYTES);
  const auto broken = [&bytes, &header](const size_t at, const uint8_t value) {
    std::vector<uint8_t> copy = bytes;
    copy[at] = value;
    return !pngslow::parseHeader(copy.data(), copy.size(), header);
  };
  EXPECT_TRUE(broken(0, 0xFF));  // JPEG start
  EXPECT_TRUE(broken(12, 'J'));  // first chunk is not IHDR
  EXPECT_TRUE(broken(24, 3));    // truecolor has no 3-bit depth
  EXPECT_TRUE(broken(25, 5));    // no such colour type
  EXPECT_TRUE(broken(28, 2));    // no such interlace method
  std::vector<uint8_t> zeroWide = bytes;
  memset(zeroWide.data() + 16, 0, 4);
  EXPECT_FALSE(pngslow::parseHeader(zeroWide.data(), zeroWide.size(), header));
}

TEST(PngSlowDecoder, DecodesEveryPixelAtFullSize) {
  for (const Fixture& f : kFixtures) {
    const pngslow::Region whole{0, 0, f.width, f.height};
    const std::vector<uint8_t> canvas = decode(f, whole, f.width, f.height);
    const std::vector<uint8_t> want(f.gray, f.gray + static_cast<size_t>(f.width) * f.height);
    EXPECT_EQ(canvas, want) << f.name;
  }
}

TEST(PngSlowDecoder, ReadsInAnyPieceSize) {
  // One byte at a time crosses every chunk header, CRC and IDAT boundary mid-read.
  for (const Fixture& f : kFixtures) {
    const pngslow::Region whole{0, 0, f.width, f.height};
    for (const size_t piece : {size_t{1}, size_t{3}, size_t{1000}}) {
      EXPECT_EQ(decode(f, whole, f.width, f.height, piece), sampled(f, whole, f.width, f.height))
          << f.name << " piece " << piece;
    }
  }
}

TEST(PngSlowDecoder, ScalesDownAndUpByNearestSample) {
  for (const Fixture& f : kFixtures) {
    const pngslow::Region whole{0, 0, f.width, f.height};
    const int sizes[][2] = {{1, 1},
                            {(f.width + 1) / 2, (f.height + 1) / 2},
                            {f.width, 1},
                            {3, 4},
                            {f.width * 2, f.height * 3},
                            {f.width + 5, f.height + 2}};
    for (const auto& size : sizes) {
      EXPECT_EQ(decode(f, whole, size[0], size[1]), sampled(f, whole, size[0], size[1]))
          << f.name << " to " << size[0] << "x" << size[1];
    }
  }
}

TEST(PngSlowDecoder, ShowsOnlyTheAskedRegion) {
  for (const Fixture& f : kFixtures) {
    if (f.width < 4 || f.height < 4) continue;
    // The right half of a spread, and a centre crop.
    const pngslow::Region right{f.width / 2, 0, f.width - f.width / 2, f.height};
    EXPECT_EQ(decode(f, right, right.width, right.height), sampled(f, right, right.width, right.height)) << f.name;
    const pngslow::Region centre{1, 2, f.width - 2, f.height - 3};
    EXPECT_EQ(decode(f, centre, 5, 4), sampled(f, centre, 5, 4)) << f.name;
  }
}

TEST(PngSlowDecoder, ACutOffFileIsReported) {
  for (const Fixture& f : kFixtures) {
    const pngslow::Header header = headerOf(f);
    const pngslow::Region whole{0, 0, f.width, f.height};
    // Cut after the signature, after the header, and half way through the file.
    for (const size_t keep : {size_t{8}, pngslow::HEADER_BYTES + 4, f.pngSize / 2}) {
      Source source{f.png, keep, 0, 4096};
      std::vector<uint8_t> canvas(static_cast<size_t>(f.width) * f.height, 7);
      EXPECT_EQ(pngslow::decodeGray(&Source::read, &source, header, whole, canvas.data(), f.width, f.height),
                pngslow::Result::BadFile)
          << f.name << " cut at " << keep;
    }
  }
}

TEST(PngSlowDecoder, DamagedPictureDataIsReportedNotCrashedOn) {
  for (const Fixture& f : kFixtures) {
    const pngslow::Header header = headerOf(f);
    const pngslow::Region whole{0, 0, f.width, f.height};
    // Flip bytes across the file (after the header): any result is fine but a crash or a hang.
    for (size_t at = pngslow::HEADER_BYTES + 4; at < f.pngSize; at += 5) {
      std::vector<uint8_t> bytes(f.png, f.png + f.pngSize);
      bytes[at] ^= 0x5A;
      Source source{bytes.data(), bytes.size(), 0, 4096};
      std::vector<uint8_t> canvas(static_cast<size_t>(f.width) * f.height, 7);
      const auto result = pngslow::decodeGray(&Source::read, &source, header, whole, canvas.data(), f.width, f.height);
      EXPECT_TRUE(result == pngslow::Result::Ok || result == pngslow::Result::BadFile) << f.name << " at " << at;
    }
  }
}

TEST(PngSlowDecoder, RefusesBadArgumentsAndPicturesTooWide) {
  const Fixture& f = kFixtures[0];
  const pngslow::Header header = headerOf(f);
  std::vector<uint8_t> canvas(64);
  Source source{f.png, f.pngSize, 0, 4096};
  const pngslow::Region whole{0, 0, f.width, f.height};
  EXPECT_EQ(pngslow::decodeGray(nullptr, &source, header, whole, canvas.data(), 4, 4), pngslow::Result::BadArguments);
  EXPECT_EQ(pngslow::decodeGray(&Source::read, &source, header, whole, nullptr, 4, 4), pngslow::Result::BadArguments);
  EXPECT_EQ(pngslow::decodeGray(&Source::read, &source, header, whole, canvas.data(), 0, 4),
            pngslow::Result::BadArguments);
  const pngslow::Region outside{1, 0, f.width, f.height};
  EXPECT_EQ(pngslow::decodeGray(&Source::read, &source, header, outside, canvas.data(), 4, 4),
            pngslow::Result::BadArguments);
  EXPECT_EQ(source.pos, 0u);  // nothing was read for any of them

  pngslow::Header wide = header;
  wide.colorType = 6;
  wide.bitDepth = 16;
  wide.width = 8193;  // one pixel past the 64 KB scanline
  const pngslow::Region wideRegion{0, 0, 8193, f.height};
  EXPECT_EQ(pngslow::decodeGray(&Source::read, &source, wide, wideRegion, canvas.data(), 4, 4),
            pngslow::Result::TooLarge);
  wide.width = 8192;
  EXPECT_EQ(pngslow::rowBytes(wide), pngslow::MAX_ROW_BYTES);
}

TEST(PngSlowDecoder, TicksOncePerScanline) {
  for (const Fixture& f : kFixtures) {
    if (f.interlace) continue;
    Source source{f.png, f.pngSize, 0, 4096};
    std::vector<uint8_t> canvas(static_cast<size_t>(f.width) * f.height);
    int ticks = 0;
    const pngslow::Region whole{0, 0, f.width, f.height};
    ASSERT_EQ(pngslow::decodeGray(
                  &Source::read, &source, headerOf(f), whole, canvas.data(), f.width, f.height,
                  [](void* user) { ++*static_cast<int*>(user); }, &ticks),
              pngslow::Result::Ok);
    EXPECT_EQ(ticks, f.height) << f.name;
  }
}

}  // namespace
