#include "SshInput.h"

namespace sshinput {

namespace {
void appendUtf8(std::string& out, const uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x110000) {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::string arrow(const char letter, const bool applicationCursor) {
  std::string out = applicationCursor ? "\x1bO" : "\x1b[";
  out += letter;
  return out;
}
}  // namespace

uint8_t controlOf(const uint32_t codepoint) {
  if (codepoint >= 'a' && codepoint <= 'z') return static_cast<uint8_t>(codepoint - 'a' + 1);
  if (codepoint >= 'A' && codepoint <= 'Z') return static_cast<uint8_t>(codepoint - 'A' + 1);
  if (codepoint >= '[' && codepoint <= '_') return static_cast<uint8_t>(codepoint - '[' + 0x1B);
  if (codepoint == '?') return 0x7F;
  return 0;
}

std::string bytesFor(const textinput::Event& event, const bool ctrl, const bool applicationCursor) {
  switch (event.special) {
    case textinput::Special::Enter:
      return "\r";
    case textinput::Special::Backspace:
      return "\x7f";
    case textinput::Special::Tab:
      return "\t";
    case textinput::Special::Escape:
      return "\x1b";
    case textinput::Special::Delete:
      return "\x1b[3~";
    case textinput::Special::Up:
      return arrow('A', applicationCursor);
    case textinput::Special::Down:
      return arrow('B', applicationCursor);
    case textinput::Special::Right:
      return arrow('C', applicationCursor);
    case textinput::Special::Left:
      return arrow('D', applicationCursor);
    case textinput::Special::Home:
      return arrow('H', applicationCursor);
    case textinput::Special::End:
      return arrow('F', applicationCursor);
    case textinput::Special::PageUp:
    case textinput::Special::PageDown:
      return {};
    case textinput::Special::None:
      break;
  }
  std::string out;
  const uint32_t cp = event.codepoint;
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return out;
  if (ctrl) {
    if (isControlNul(cp)) {
      out += '\0';
      return out;
    }
    const uint8_t control = controlOf(cp);
    if (control != 0) {
      out += static_cast<char>(control);
      return out;
    }
  }
  appendUtf8(out, cp);
  return out;
}

std::string bytesForLine(const std::string_view text, const bool ctrl, const bool enter) {
  std::string out;
  size_t start = 0;
  if (ctrl && !text.empty()) {
    const unsigned char first = static_cast<unsigned char>(text[0]);
    if (first < 0x80) {
      out = bytesFor(textinput::Event{first, textinput::Special::None}, true, false);
      start = 1;
    }
  }
  out.append(text.substr(start));
  if (enter) out += '\r';
  return out;
}

}  // namespace sshinput
