#include "PaperTexture.h"

#include <GfxRenderer.h>

#include <cstdlib>

namespace paper_texture {
namespace {

uint32_t mix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return h;
}

uint32_t hash(const int x, const int y, const uint32_t seed) {
  return mix(static_cast<uint32_t>(x) * 0x9E3779B1u ^ static_cast<uint32_t>(y) * 0x85EBCA77u ^ seed);
}

// Papyrus: two layers of plant strips, one across and one down, pressed together. Strip edges
// show as faint dotted lines, fibres as short streaks along each layer. `level` 1..3.
bool papyrus(const int x, const int y, const int level) {
  const uint32_t h = hash(x, y, 1);
  constexpr int STRIP = 36;
  // Strip edges, each band nudged a little so the lines aren't perfectly even.
  if (y % STRIP == static_cast<int>(mix(y / STRIP) % 4) && h % 100 < 14u + 8u * level) return true;
  if (x % (STRIP + 6) == static_cast<int>(mix(x / (STRIP + 6) + 77) % 4) && h % 100 < 4u * level) return true;
  // Fibres: 32px streaks, offset per row (across) or per column (down), broken up by gaps.
  if (h % 3 == 0) return false;
  const int across = (x + static_cast<int>(mix(y + 11) & 31)) >> 5;
  if (hash(across, y, 2) % 1000 < 12u * level) return true;
  const int down = (y + static_cast<int>(mix(x + 23) & 31)) >> 5;
  if (hash(x, down, 3) % 1000 < 5u * level) return true;
  return h % 1000 < 2u * level;
}

// Value noise: hashed lattice values, smoothly blended in between (0..255).
int smoothNoise(const int x, const int y, const int cell, const uint32_t seed) {
  const int cx = x / cell, cy = y / cell;
  const int fx = (x % cell) * 256 / cell, fy = (y % cell) * 256 / cell;
  // Smoothstep, so the cell edges don't show as lines.
  const int sx = fx * fx * (768 - 2 * fx) >> 16, sy = fy * fy * (768 - 2 * fy) >> 16;
  const int a = hash(cx, cy, seed) & 255, b = hash(cx + 1, cy, seed) & 255;
  const int c = hash(cx, cy + 1, seed) & 255, d = hash(cx + 1, cy + 1, seed) & 255;
  const int top = a + ((b - a) * sx >> 8), bottom = c + ((d - c) * sx >> 8);
  return top + ((bottom - top) * sy >> 8);
}

// Cloudy papyrus: soft blotches, denser and lighter in patches, with no strips or straight lines.
bool cloudy(const int x, const int y) {
  const int cloud = (smoothNoise(x, y, 96, 8) * 5 + smoothNoise(x, y, 31, 9) * 3) >> 3;  // 0..255
  // Only the darker part of the clouds gets dots, densest at the darkest.
  const int density = cloud > 115 ? cloud - 115 : 0;  // 0..140 per 1000, light enough to read over
  return hash(x, y, 10) % 1000 < static_cast<uint32_t>(density);
}

bool inked(const int x, const int y, const uint8_t style) {
  switch (style) {
    case PAPER:
      return hash(x, y, 4) % 1000 < 14;
    case SANDPAPER:
      return hash(x, y, 5) % 100 < 9;
    case PAPYRUS_SOFT:
      return papyrus(x, y, 1);
    case PAPYRUS_MEDIUM:
      return papyrus(x, y, 2);
    case PAPYRUS_HARD:
      return papyrus(x, y, 3);
    case TOILET_PAPER: {
      // Quilted diamonds, dotted, with an embossed dot in each.
      const int dx = x % 20 - 10, dy = y % 20 - 10;
      if (dx == 0 && dy == 0) return true;
      return std::abs(dx) + std::abs(dy) == 10 && (x & 1) == 0 && (y & 1) == 0;
    }
    case KITCHEN_PAPER: {
      // A dotted lattice with a small embossed ring in every cell.
      const int dx = x % 28 - 14, dy = y % 28 - 14;
      const int d2 = dx * dx + dy * dy;
      if (d2 >= 30 && d2 <= 42 && (x & 1)) return true;
      return std::abs(dx) + std::abs(dy) == 14 && (x & 1) == 0 && (y & 1) == 0;
    }
    case PAPYRUS_CLOUDY:
      return cloudy(x, y);
    case BATH_TOWEL:
      // Terry loops: dense 2x2 tufts.
      return hash(x >> 1, y >> 1, 6) % 100 < 12 && hash(x, y, 7) % 4 != 0;
    default:
      return false;
  }
}

}  // namespace

void draw(const GfxRenderer& renderer, const uint8_t style) {
  if (style == PLAIN || style >= STYLE_COUNT) return;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (inked(x, y, style)) renderer.drawPixel(x, y, true);
    }
  }
}

}  // namespace paper_texture
