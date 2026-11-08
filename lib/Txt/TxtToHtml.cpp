#include "TxtToHtml.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <MarkdownToHtml.h>
#include <Memory.h>
#include <MobiFormat.h>

#include <algorithm>
#include <cstring>

const char* TxtToHtml::cacheVersionTag(std::string_view filename) {
  if (FsHelpers::hasMobiExtension(filename)) return mobi::CACHE_VERSION_TAG;
  if (isPdfText(filename)) return "<!-- TXT_CACHE_VERSION: 2 -->";  // the Style setting: Txt::syncPdfStyle
  if (!FsHelpers::hasMarkdownExtension(filename)) return "<!-- TXT_CACHE_VERSION: 1 -->";
  return taskBoxes ? "<!-- MD_CACHE_VERSION: 8 boxes -->" : "<!-- MD_CACHE_VERSION: 8 -->";
}

bool TxtToHtml::isPdfText(const std::string_view filename) {
  constexpr std::string_view FOLDER = "/.vaultink/pdf/";
  return filename.size() > FOLDER.size() && filename.compare(0, FOLDER.size(), FOLDER) == 0 &&
         FsHelpers::hasTxtExtension(filename);
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
                       Print& out, const MarkdownToHtml::ImageResolver* images,
                       const MarkdownToHtml::NoteLoader* notes) {
  constexpr size_t IN_BUF_SIZE = 8192;
  constexpr size_t OUT_BUF_SIZE = 8192;

  auto inBuf = makeUniqueNoThrow<uint8_t[]>(IN_BUF_SIZE);
  auto outBuf = makeUniqueNoThrow<uint8_t[]>(OUT_BUF_SIZE);
  if (!inBuf || !outBuf) {
    LOG_ERR("TXT", "OOM: TXT/MD HTML streaming buffers");
    return false;
  }

  if (FsHelpers::hasMarkdownExtension(filename)) {
    return streamMarkdown(filename, readerCtx, readFn, inBuf.get(), IN_BUF_SIZE, out, images, notes);
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

  // A PDF's text carries style marks (lib/Pdf/PdfText.cpp): 1 + digit opens a heading, 4 closes it, 2 and 3 switch bold
  // and italic. Styled makes them tags, plain drops them.
  const bool marks = isPdfText(filename);
  const bool styled = marks && pdfStyled;
  bool bold = false, italic = false, levelNext = false;
  int heading = 0;
  size_t skipBreaks = 0;          // the blank lines a heading already has round it
  const auto flushSpaces = [&] {  // spaces wait for the next letter; a tag goes after them
    if (pendingSpaces == 0) return;
    for (size_t k = 0; k + 1 < pendingSpaces; ++k) writeStr("&#160;");
    writeByte(' ');
    pendingSpaces = 0;
  };
  const auto closeHeading = [&] {
    if (heading == 0) return;
    writeStr(heading == 1 ? "</h1>" : (heading == 2 ? "</h2>" : "</h3>"));
    heading = 0;
  };
  const auto closeInline = [&] {
    if (italic) writeStr("</i>");
    if (bold) writeStr("</b>");
  };
  const auto openInline = [&] {
    if (bold) writeStr("<b>");
    if (italic) writeStr("<i>");
  };

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

      if (marks) {
        if (levelNext) {
          levelNext = false;
          if (styled && b >= '1' && b <= '3') {
            flushSpaces();
            closeInline();  // a heading never sits inside bold or italic, nor inside another heading
            closeHeading();
            bold = italic = false;
            heading = b - '0';
            writeStr(heading == 1 ? "<h1>" : (heading == 2 ? "<h2>" : "<h3>"));
          }
          continue;
        }
        if (b >= 1 && b <= 4) {
          if (b == 1) {
            levelNext = true;
          } else if (styled && b == 4) {
            if (heading > 0) {
              flushSpaces();
              closeHeading();
              skipBreaks = 2;
            }
          } else if (styled && heading == 0) {  // a heading is bold already
            flushSpaces();
            closeInline();
            if (b == 2) bold = !bold;
            if (b == 3) italic = !italic;
            openInline();
          }
          continue;
        }
      }

      if (b == '\n') {
        pendingSpaces = 0;
        if (skipBreaks > 0) {
          --skipBreaks;
          continue;
        }
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
      skipBreaks = 0;

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

  closeInline();
  closeHeading();
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
                               const size_t inBufSize, Print& out, const MarkdownToHtml::ImageResolver* images,
                               const MarkdownToHtml::NoteLoader* notes) {
  struct PrintSink final : MarkdownToHtml::Sink {
    Print& out;
    bool ok = true;
    explicit PrintSink(Print& out) : out(out) {}
    void write(std::string_view text) override {
      ok = ok && out.write(reinterpret_cast<const uint8_t*>(text.data()), text.size()) == text.size();
    }
  } sink(out);
  MarkdownToHtml converter(sink);
  if (images) converter.setImageResolver(*images);
  if (notes) converter.setNoteLoader(*notes);
  converter.setTaskLinks(true);
  converter.setTaskBoxes(taskBoxes);
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
