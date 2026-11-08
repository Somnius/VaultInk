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
  // Markdown tasks as ☐ / ☑ (the built-in reading fonts have them) or [ ] / [x] (an SD-card font
  // may not). Part of the cache tag, so switching fonts rebuilds the notes.
  static void setTaskBoxes(bool on) { taskBoxes = on; }
  // A PDF's converted text (/.vaultink/pdf/..., src/util/PdfConvert.cpp) carries style marks: headings, bold and italic
  // as control bytes (lib/Pdf/PdfText.cpp). Styled turns them into tags, plain drops them. Part of the cache tag, so
  // switching Style rebuilds the book's HTML (kos25k #29, Txt::syncPdfStyle).
  static void setPdfStyled(bool on) { pdfStyled = on; }
  static bool isPdfText(std::string_view filename);

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

 private:
  static inline bool taskBoxes = false;
  static inline bool pdfStyled = true;
};
