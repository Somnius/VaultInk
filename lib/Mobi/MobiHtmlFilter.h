#pragma once

// Streaming Mobipocket HTML -> well-formed XHTML for the reader's expat layout parser.
// Input arrives in arbitrary chunks (one decompressed record at a time); tags and entities may
// straddle chunks. Unknown tags and all attributes but align/src are dropped; unclosed <p>/<li>
// are closed; every open tag is closed at finish(). Named entities become UTF-8.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace mobi {

class HtmlFilter {
 public:
  using Emit = bool (*)(void* ctx, const char* data, size_t size);
  using ResolveImage = bool (*)(void* ctx, uint32_t recindex, std::string& path);

  HtmlFilter(Emit emit, void* emitCtx) : emit_(emit), emitCtx_(emitCtx) {}
  void setImageResolver(ResolveImage fn, void* ctx) {
    resolveImage_ = fn;
    imageCtx_ = ctx;
  }

  // Feeds UTF-8 text. False once the sink refused a write.
  bool feed(std::string_view text);
  // Closes every open element. False once the sink refused a write.
  bool finish();
  bool ok() const { return ok_; }

 private:
  enum class Mode : uint8_t { Text, Tag, Comment, Entity };
  static constexpr size_t MAX_TAG = 384;  // longer tags are dropped
  static constexpr size_t MAX_ENTITY = 12;
  static constexpr size_t MAX_DEPTH = 24;

  void out(std::string_view s);
  void text(char c);
  void flushEntity(bool complete);
  void handleTag();
  void openTag(uint8_t id, std::string_view attrs);
  void closeTag(uint8_t id);
  void closeTo(size_t index);  // pops the stack down to and including `index`
  int find(uint8_t id) const;  // stack index of the innermost `id`, or -1
  static std::string_view attrValue(std::string_view attrs, std::string_view name);

  Emit emit_;
  void* emitCtx_;
  ResolveImage resolveImage_ = nullptr;
  void* imageCtx_ = nullptr;
  bool ok_ = true;
  Mode mode_ = Mode::Text;
  char tag_[MAX_TAG];
  size_t tagLen_ = 0;
  bool tagOverflow_ = false;
  char entity_[MAX_ENTITY];
  size_t entityLen_ = 0;
  uint8_t stack_[MAX_DEPTH];
  size_t depth_ = 0;
  uint8_t skipId_ = 0;  // inside <head>/<style>/<script>/<title>: text dropped until it closes
  int skipNest_ = 0;
  int commentDashes_ = 0;
};

}  // namespace mobi
