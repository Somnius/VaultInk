#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Obsidian-flavoured Markdown to the XHTML subset the EPUB chapter parser reads (SPECS.md 5.1).
// Streaming: feed() any chunk of the note, then finish(); output goes to the sink as it is made.
// Memory is bounded: one line and one paragraph at a time, each capped (see the constants).
// No Arduino or SD code here, so it builds and tests on the host.
//
// Links and embeds are written with a "vault:" scheme so the reader can resolve them later
// through the vault index: [[Note#Heading|Text]] becomes <a href="vault:Note#Heading">Text</a>,
// ![[pic.png|300]] becomes <img src="vault:pic.png" width="300"/>.
class MarkdownToHtml {
 public:
  struct Sink {
    virtual ~Sink() = default;
    virtual void write(std::string_view text) = 0;
  };

  // Longest line and paragraph kept whole; longer ones are split, never dropped.
  static constexpr size_t MAX_LINE = 16 * 1024;
  static constexpr size_t MAX_PARAGRAPH = 64 * 1024;
  static constexpr size_t MAX_TABLE_ROWS = 400;

  // Maps an embedded image name to its path on the SD card ("" when not found). Without one,
  // images keep their "vault:" source.
  using ImageResolver = std::function<std::string(std::string_view name)>;

  // Reads an embedded note ("![[Note]]" on a line of its own) into `text`, at most MAX_EMBED
  // bytes; false when it isn't found. Without one, note embeds stay links.
  using NoteLoader = std::function<bool(std::string_view name, std::string& text)>;
  static constexpr size_t MAX_EMBED = 32 * 1024;
  static const char* const CALLOUT_OPEN;  // the <blockquote> that starts a callout box

  explicit MarkdownToHtml(Sink& sink) : sink(sink) {}
  void setImageResolver(ImageResolver resolver) { imageResolver = std::move(resolver); }
  void setNoteLoader(NoteLoader loader) { noteLoader = std::move(loader); }
  // Task boxes become "task:<line>:<hash>" links (TaskToggle.h) the reader can tick. Only for the
  // note itself: embedded notes keep plain boxes, their lines are in another file.
  void setTaskLinks(bool on) { taskLinks = on; }
  // Tasks as ☐ / ☑ instead of [ ] / [x]: only for fonts that have the glyphs (the built-in ones).
  void setTaskBoxes(bool on) { taskBoxes = on; }

  // Writes the XHTML head; title is the note name. `prolog` goes right after the XML declaration
  // (the reader's cache-version comment).
  void begin(std::string_view title, std::string_view prolog = {});
  void feed(std::string_view chunk);
  // Flushes the last line and closes every open element; `closeDocument` = false leaves out
  // </body></html> (an embedded note's body).
  void finish(bool closeDocument = true);

  // Inline Markdown (emphasis, code, links, tags...) to escaped XHTML. Public for tests.
  static void inlineToHtml(std::string_view text, std::string& out, const ImageResolver* images = nullptr);
  // Heading text to its anchor id: spaces become '-', case is kept (links match it case-insensitively).
  static std::string headingId(std::string_view text);
  // The part of a note that "Note#anchor" embeds: for a heading, from that heading up to the next
  // heading of the same or a higher level; for "^id", the list item or paragraph ending in " ^id".
  // The whole note when the anchor is empty or not found.
  static std::string_view section(std::string_view note, std::string_view anchor);

 private:
  enum class Block { None, Paragraph, Code, Quote, Comment, Frontmatter, Table };
  struct ListLevel {
    bool ordered;
    int indent;
  };

  void line(std::string_view text);
  void openBlock(Block block);
  void closeParagraph();
  void closeLists(int toDepth);
  void closeQuote();
  void closeAll();
  void openList(bool ordered, int number);
  void listItem(int indent, bool ordered, int number, std::string_view content);
  // more = the table goes on: write what is buffered and keep the header for the next part.
  void flushTable(bool more = false);
  void continuation(std::string_view text);
  void heading(int level, std::string_view text);
  bool embedNote(std::string_view trimmed);
  void writeEscaped(std::string_view text);

  Sink& sink;
  ImageResolver imageResolver;
  NoteLoader noteLoader;
  const ImageResolver* images() const { return imageResolver ? &imageResolver : nullptr; }
  std::string pendingLine;
  std::string paragraph;
  Block block = Block::None;
  bool firstLine = true;
  std::string fence;  // the code fence that opened the current code block
  std::vector<ListLevel> lists;
  bool listItemOpen = false;
  bool quoteOpen = false;
  bool quoteParagraph = false;
  std::vector<std::string> tableRows;
  size_t tableBytes = 0;
  bool inContinuation = false;
  bool taskLinks = false;
  bool taskBoxes = false;
  uint32_t sourceLine = 0;       // 0-based line of the source being converted
  std::string_view currentLine;  // that whole line, while line() runs
};
