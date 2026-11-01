#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Excalidraw drawings (.excalidraw JSON, and the Obsidian Excalidraw plugin's .excalidraw.md with a
// ```json or ```compressed-json block; the latter is lz-string base64, see LzString.h), read only. The file is streamed
// once into fixed-capacity pools: shapes in z-order, their points (relative to x,y as in the file) in one point pool,
// their text in one text pool. Deleted elements, images, frames and embeddables are skipped; the `files` map (base64
// images) is skipped without being stored.
namespace excalidraw {

enum class Kind : uint8_t { Rect, Ellipse, Diamond, Text, Line, Arrow, FreeDraw };

enum class Error : uint8_t {
  Ok,
  NotJson,                // no JSON object and no ```json / ```compressed-json block
  CompressedUnsupported,  // no longer returned (compressed blocks are decoded since 0.64.0); kept for callers
  BadCompressed,          // a ```compressed-json block that could not be decoded (damaged or too large)
};

struct Point {
  int16_t x = 0, y = 0;
};

struct Shape {
  Kind kind = Kind::Rect;
  float x = 0, y = 0, w = 0, h = 0;
  float angle = 0;          // radians, around the centre
  uint8_t strokeStyle = 0;  // 0 solid, 1 dashed, 2 dotted
  uint8_t fill = 0;         // 0 none (transparent background), 1 filled
  uint8_t strokeWidth = 1;
  bool startArrow = false, endArrow = false;
  uint16_t pointStart = 0, pointCount = 0;  // Drawing::points, relative to x,y
  uint16_t textOffset = 0;                  // Drawing::text
  uint8_t textLength = 0;                   // bytes, at most TEXT_CHARS
  int16_t groupId = -1;                     // first group of the element, -1 none
};

constexpr size_t MAX_SHAPES = 1500;
constexpr size_t MAX_POINTS = 12000;
constexpr size_t MAX_TEXT = 24 * 1024;
constexpr size_t TEXT_CHARS = 160;      // bytes kept per shape; longer text is cut at a UTF-8 boundary
constexpr size_t FREEDRAW_POINTS = 64;  // a freedraw stroke is thinned to this many points
constexpr size_t LINE_POINTS = 256;     // lines and arrows
constexpr size_t MAX_GROUPS = 512;

struct Drawing {
  std::vector<Shape> shapes;  // z-order, at most MAX_SHAPES
  std::vector<Point> points;  // at most MAX_POINTS
  std::string text;           // at most MAX_TEXT
  bool truncated = false;     // a cap was reached; the rest was skipped or cut
  size_t skipped = 0;         // deleted, image, frame and unknown elements
  // Bounding box of every shape (points included, rotation ignored), for fit-to-screen.
  float minX = 0, minY = 0, maxX = 0, maxY = 0;

  std::string textOf(const Shape& s) const { return text.substr(s.textOffset, s.textLength); }
};

// Pulls up to `cap` bytes into `buf`; returns 0 at the end of the file.
using Reader = std::function<size_t(char* buf, size_t cap)>;

// Parses a whole file. A damaged tail keeps what was read (still Ok).
Error parse(const Reader& read, Drawing& out);

}  // namespace excalidraw
