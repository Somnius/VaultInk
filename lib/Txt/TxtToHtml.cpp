#include "TxtToHtml.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <MarkdownToHtml.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

const char* TxtToHtml::cacheVersionTag(std::string_view filename) {
  return FsHelpers::hasMarkdownExtension(filename) ? "<!-- MD_CACHE_VERSION: 2 -->" : "<!-- TXT_CACHE_VERSION: 1 -->";
}

size_t TxtToHtml::escapedHtmlSize(const std::string_view text) {
  size_t size = 0;
  for (const char c : text) {
    size += c == '&' ? 5 : (c == '<' || c == '>') ? 4 : 1;
  }
  return size;
}

void TxtToHtml::appendEscapedHtml(std::string& out, const std::string_view text) {
  TxtToHtml::streamEscapedHtml([&out](std::string_view fragment) { out.append(fragment); }, text);
}

bool TxtToHtml::stream(std::string_view filename, void* readerCtx, int (*readFn)(void* ctx, uint8_t* buf, size_t size),
                       Print& out) {
  constexpr size_t IN_BUF_SIZE = 8192;
  constexpr size_t OUT_BUF_SIZE = 8192;

  auto inBuf = makeUniqueNoThrow<uint8_t[]>(IN_BUF_SIZE);
  auto outBuf = makeUniqueNoThrow<uint8_t[]>(OUT_BUF_SIZE);
  if (!inBuf || !outBuf) {
    LOG_ERR("TXT", "OOM: TXT/MD HTML streaming buffers");
    return false;
  }

  if (FsHelpers::hasMarkdownExtension(filename)) {
    return streamMarkdown(filename, readerCtx, readFn, inBuf.get(), IN_BUF_SIZE, out);
  }

  size_t outPos = 0;
  bool outputOk = true;
  auto flushOut = [&]() {
    if (outPos > 0) {
      const size_t written = out.write(outBuf.get(), outPos);
      outputOk = outputOk && (written == outPos);
      outPos = 0;
    }
  };

  auto writeByte = [&](uint8_t b) {
    outBuf[outPos++] = b;
    if (outPos == OUT_BUF_SIZE) flushOut();
  };

  auto writeStr = [&](std::string_view s) {
    for (char c : s) {
      writeByte(static_cast<uint8_t>(c));
    }
  };

  writeStr("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
  writeStr(cacheVersionTag(filename));
  writeByte('\n');
  writeStr("<!DOCTYPE html>\n<html>\n<head><title>");
  std::string title = FsHelpers::getFileNameWithoutExtension(filename);
  TxtToHtml::streamEscapedHtml(writeStr, title);
  writeStr("</title></head>\n<body>\n");

  bool isStart = true;
  bool atLineStart = true;
  size_t pendingSpaces = 0;
  int bytesRead = 0;

  while ((bytesRead = readFn(readerCtx, inBuf.get(), IN_BUF_SIZE)) > 0) {
    int startIdx = 0;
    if (isStart) {
      isStart = false;
      if (bytesRead >= 3 && inBuf[0] == 0xEF && inBuf[1] == 0xBB && inBuf[2] == 0xBF) {
        startIdx = 3;
      }
    }

    for (int i = startIdx; i < bytesRead; i++) {
      uint8_t b = inBuf[i];
      if (b == '\r') continue;

      if (b == '\n') {
        pendingSpaces = 0;
        writeStr("<br />");
        atLineStart = true;
        continue;
      }

      if (b == ' ') {
        if (atLineStart) {
          writeStr("&#160;");
        } else {
          pendingSpaces++;
        }
        continue;
      }

      if (pendingSpaces > 0) {
        for (size_t s = 0; s < pendingSpaces - 1; s++) {
          writeStr("&#160;");
        }
        writeByte(' ');
        pendingSpaces = 0;
      }
      atLineStart = false;

      if (b == '&' || b == '<' || b == '>') {
        const char special = static_cast<char>(b);
        TxtToHtml::streamEscapedHtml(writeStr, std::string_view(&special, 1));
      } else if (b < 0x20 && b != '\t') {
        writeByte(' ');
      } else {
        writeByte(b);
      }
    }
  }

  if (bytesRead < 0) {
    LOG_ERR("TXT", "Read error while streaming TXT/MD: %.*s", static_cast<int>(filename.size()), filename.data());
    return false;
  }

  writeStr("\n</body>\n</html>\n");
  flushOut();
  if (!outputOk) {
    LOG_ERR("TXT", "Failed to stream complete HTML (write error or disk full)");
    return false;
  }
  return true;
}

bool TxtToHtml::stream(std::string_view filename, std::string_view content, Print& out) {
  struct ViewReader {
    std::string_view s;
    size_t pos = 0;
  } reader{content, 0};

  return stream(
      filename, &reader,
      [](void* ctx, uint8_t* buf, size_t size) -> int {
        auto* r = static_cast<ViewReader*>(ctx);
        if (r->pos >= r->s.size()) return 0;
        const size_t n = std::min(size, r->s.size() - r->pos);
        memcpy(buf, r->s.data() + r->pos, n);
        r->pos += n;
        return static_cast<int>(n);
      },
      out);
}

// v2 of the .md cache: Obsidian Markdown rendered by lib/Vault instead of shown as plain text.
bool TxtToHtml::streamMarkdown(std::string_view filename, void* readerCtx,
                               int (*readFn)(void* ctx, uint8_t* buf, size_t size), uint8_t* inBuf,
                               const size_t inBufSize, Print& out) {
  struct PrintSink final : MarkdownToHtml::Sink {
    Print& out;
    bool ok = true;
    explicit PrintSink(Print& out) : out(out) {}
    void write(std::string_view text) override {
      ok = ok && out.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == text.size();
    }
  } sink(out);
  MarkdownToHtml converter(sink);
  converter.begin(FsHelpers::getFileNameWithoutExtension(filename), cacheVersionTag(filename));
  bool first = true;
  int bytesRead = 0;
  while ((bytesRead = readFn(readerCtx, inBuf, inBufSize)) > 0) {
    std::string_view chunk(reinterpret_cast<const char*>(inBuf), static_cast<size_t>(bytesRead));
    if (first && chunk.size() >= 3 && chunk.compare(0, 3, "\xEF\xBB\xBF") == 0) chunk.remove_prefix(3);
    first = false;
    converter.feed(chunk);
  }
  if (bytesRead < 0) {
    LOG_ERR("TXT", "Read error while streaming MD: %.*s", static_cast<int>(filename.size()), filename.data());
    return false;
  }
  converter.finish();
  if (!sink.ok) LOG_ERR("TXT", "Failed to stream complete MD HTML (write error or disk full)");
  return sink.ok;
}
