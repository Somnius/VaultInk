#pragma once

#include <cstddef>
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

  explicit MarkdownToHtml(Sink& sink) : sink(sink) {}

  // Writes the XHTML head; title is the note name. `prolog` goes right after the XML declaration
  // (the reader's cache-version comment).
  void begin(std::string_view title, std::string_view prolog = {});
  void feed(std::string_view chunk);
  // Flushes the last line and closes every open element.
  void finish();

  // Inline Markdown (emphasis, code, links, tags...) to escaped XHTML. Public for tests.
  static void inlineToHtml(std::string_view text, std::string& out);
  // Heading text to its anchor id: spaces become '-', case is kept (links match it case-insensitively).
  static std::string headingId(std::string_view text);

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
  void listItem(int indent, bool ordered, std::string_view content);
  void flushTable();
  void heading(int level, std::string_view text);
  void writeEscaped(std::string_view text);

  Sink& sink;
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
};
