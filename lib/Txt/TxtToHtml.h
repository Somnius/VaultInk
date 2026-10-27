#pragma once

#include <MarkdownToHtml.h>
#include <Print.h>

#include <cstddef>
#include <string>
#include <string_view>

class TxtToHtml {
 public:
  template <typename Append>
  static void streamEscapedHtml(Append&& append, std::string_view text) {
    for (const char c : text) {
      switch (c) {
        case '&':
          append("&amp;");
          break;
        case '<':
          append("&lt;");
          break;
        case '>':
          append("&gt;");
          break;
        default:
          append(std::string_view(&c, 1));
          break;
      }
    }
  }

  static const char* cacheVersionTag(std::string_view filename);

 private:
  static bool streamMarkdown(std::string_view filename, void* readerCtx,
                             int (*readFn)(void* ctx, uint8_t* buf, size_t size), uint8_t* inBuf, size_t inBufSize,
                             Print& out, const MarkdownToHtml::ImageResolver* images,
                             const MarkdownToHtml::NoteLoader* notes);

 public:
  static size_t escapedHtmlSize(std::string_view text);
  static void appendEscapedHtml(std::string& out, std::string_view text);
  // `images` (Markdown only) maps embedded image names to SD paths; `notes` reads embedded notes.
  static bool stream(std::string_view filename, void* readerCtx, int (*readFn)(void* ctx, uint8_t* buf, size_t size),
                     Print& out, const MarkdownToHtml::ImageResolver* images = nullptr,
                     const MarkdownToHtml::NoteLoader* notes = nullptr);
  static bool stream(std::string_view filename, std::string_view content, Print& out);
};
