#pragma once

#include "ImageToFramebufferDecoder.h"

class PngToFramebufferConverter final : public ImageToFramebufferDecoder {
 public:
  static bool getDimensionsStatic(const std::string& imagePath, ImageDimensions& out);

  bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) override;

  bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const override {
    return getDimensionsStatic(imagePath, dims);
  }

  static bool supportsFormat(const std::string& extension);
  const char* getFormatName() const override { return "PNG"; }

 private:
  // PNGdec, row by row. Sets needsSlowDecoder (and returns false) for a picture it refuses at the
  // header: interlaced, or 16 bits per channel.
  bool decodeWithPngdec(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config,
                        bool& needsSlowDecoder);
  // PngSlowDecoder: any PNG, into a canvas of the output size first.
  bool decodeSlow(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config);
};