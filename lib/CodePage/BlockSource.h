#pragma once

// A file read at any offset, for the converted book formats that are containers (DOC in lib/Doc, CHM in lib/Chm)
// and so can't be read front to back like FB2, HTML and RTF. Header-only and platform-free.

#include <cstddef>
#include <cstdint>

namespace blocksource {

struct Source {
  void* ctx = nullptr;
  // Up to `size` bytes at `offset`: the bytes read (fewer at the end of the file), < 0 on an error.
  int (*readAt)(void* ctx, uint32_t offset, uint8_t* buf, size_t size) = nullptr;
  uint32_t size = 0;  // of the whole file
  // Set once a read inside the file failed (the card, not the file's contents): what was read so far must not
  // be kept as if the file ended there.
  mutable bool ioFailed = false;

  // Exactly `n` bytes, or false (an error, or the file ends first).
  bool readExact(const uint32_t offset, uint8_t* buf, const size_t n) const {
    if (!readAt || offset > size || n > size - offset) return false;
    size_t done = 0;
    while (done < n) {
      const int got = readAt(ctx, offset + static_cast<uint32_t>(done), buf + done, n - done);
      if (got <= 0) {
        ioFailed = true;
        return false;
      }
      done += static_cast<size_t>(got);
    }
    return true;
  }
};

}  // namespace blocksource
