#pragma once

// The XHTML page the converted book formats (FB2, HTML, RTF) hand to the reader: a small buffered writer over a
// sink that can refuse, the page head with the cache version tag (Txt::validateCache reads it back), and text
// escaping. Header-only and platform-free.

#include <cstddef>
#include <cstring>
#include <string_view>

namespace xhtml {

using Emit = bool (*)(void* ctx, const char* data, size_t size);

// The most one converted book may write. Past it the writer stops (ok() false, tooBig() true) and the
// converter fails with its TooBig status, so the half-written page is thrown away: a small file that unpacks
// or repeats itself into gigabytes can't fill the card.
inline constexpr size_t MAX_PAGE_BYTES = 16 * 1024 * 1024;

class Out {
 public:
  Out(Emit emit, void* ctx) : emit_(emit), ctx_(ctx) {}

  bool ok() const { return ok_; }
  bool tooBig() const { return tooBig_; }

  void write(std::string_view s) {
    if (ok_ && s.size() > MAX_PAGE_BYTES - total_) {
      tooBig_ = true;
      ok_ = false;
    }
    if (ok_) total_ += s.size();
    while (ok_ && !s.empty()) {
      if (len_ == sizeof(buf_)) flush();
      const size_t n = s.size() < sizeof(buf_) - len_ ? s.size() : sizeof(buf_) - len_;
      memcpy(buf_ + len_, s.data(), n);
      len_ += n;
      s.remove_prefix(n);
    }
  }

  void escaped(std::string_view s) {
    for (const char c : s) {
      if (c == '&')
        write("&amp;");
      else if (c == '<')
        write("&lt;");
      else if (c == '>')
        write("&gt;");
      else
        write(std::string_view(&c, 1));
    }
  }

  // False once the sink refused a write.
  bool flush() {
    if (ok_ && len_ > 0 && (!emit_ || !emit_(ctx_, buf_, len_))) ok_ = false;
    len_ = 0;
    return ok_;
  }

  void head(std::string_view versionTag, std::string_view title) {
    write("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    write(versionTag);
    write("\n<!DOCTYPE html>\n<html>\n<head><title>");
    escaped(title);
    write("</title></head>\n<body>\n");
  }

  bool tail() {
    write("\n</body>\n</html>\n");
    return flush();
  }

 private:
  Emit emit_;
  void* ctx_;
  bool ok_ = true;
  bool tooBig_ = false;
  size_t total_ = 0;
  size_t len_ = 0;
  char buf_[256];
};

}  // namespace xhtml
