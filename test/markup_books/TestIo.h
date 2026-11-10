#pragma once

#include <expat.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

// A file in memory, handed out `step` bytes at a time so tags and characters straddle the reads.
struct StringReader {
  std::string_view data;
  size_t step = 4096;
  size_t at = 0;
  size_t maxRequest = 0;

  static int read(void* ctx, uint8_t* buf, size_t size) {
    auto* self = static_cast<StringReader*>(ctx);
    self->maxRequest = std::max(self->maxRequest, size);
    const size_t n = std::min({size, self->step, self->data.size() - self->at});
    memcpy(buf, self->data.data() + self->at, n);
    self->at += n;
    return static_cast<int>(n);
  }
};

struct StringSink {
  std::string text;
  size_t failAfter = SIZE_MAX;  // refuses the write that would pass this many bytes

  static bool write(void* ctx, const char* data, size_t size) {
    auto* self = static_cast<StringSink*>(ctx);
    if (self->text.size() + size > self->failAfter) return false;
    self->text.append(data, size);
    return true;
  }
};

// The reader lays the page out with expat: a page that is not well-formed XML does not open.
inline bool wellFormed(const std::string& xml) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  const bool ok = XML_Parse(parser, xml.data(), static_cast<int>(xml.size()), XML_TRUE) == XML_STATUS_OK;
  XML_ParserFree(parser);
  return ok;
}

// What is between <body> and </body>, without the line ends the converters add for readability.
inline std::string bodyOf(const std::string& page) {
  const size_t start = page.find("<body>\n");
  const size_t end = page.rfind("\n</body>");
  if (start == std::string::npos || end == std::string::npos || end < start + 7) return "<no body>";
  std::string body = page.substr(start + 7, end - start - 7);
  body.erase(std::remove(body.begin(), body.end(), '\n'), body.end());
  return body;
}
