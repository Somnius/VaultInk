#pragma once

// Which reader options the open book's format can use (kos25k): the book menu leaves out what would do nothing.
// Platform-free so it is host-tested (test/format_features).
//
// What decides it is what the format's converter writes into the page the reader lays out:
//  - EPUB has its own style sheets.
//  - MOBI / AZW, HTML and CHM keep a paragraph's align attribute (lib/Mobi/MobiHtmlFilter.cpp), FB2 and RTF write
//    a text-align of their own (lib/Fb2, lib/Rtf).
//  - A PDF's text carries heading / bold / italic marks, which the Style row switches (lib/Txt/TxtToHtml.cpp).
//  - TXT, Markdown notes and DOC write plain paragraphs: no style at all to switch on or off, or to follow.

#include <cctype>
#include <cstdint>
#include <string_view>

namespace formatfeatures {

enum class BookKind : uint8_t { Epub, Txt, Note, PdfText, Mobi, Fb2, Html, Rtf, Doc, Chm };
inline constexpr BookKind ALL_KINDS[] = {BookKind::Epub, BookKind::Txt, BookKind::Note, BookKind::PdfText,
                                         BookKind::Mobi, BookKind::Fb2, BookKind::Html, BookKind::Rtf,
                                         BookKind::Doc,  BookKind::Chm};

// The Text panel's rows by number, the same numbers as textpanel::ROWS (TextPanelRows.h checks the count).
inline constexpr int TEXT_ROW_COUNT = 15;
inline constexpr int TEXT_ROW_ALIGNMENT = 3;
inline constexpr int TEXT_ROW_EMBEDDED_STYLE = 12;

// The last alignment choice is "Book's style" (CrossPointSettings::BOOK_STYLE).
inline constexpr int ALIGNMENT_CHOICES = 5;

inline bool hasExtension(const std::string_view path, const std::string_view extension) {
  if (path.size() < extension.size()) return false;
  const size_t offset = path.size() - extension.size();
  for (size_t i = 0; i < extension.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(path[offset + i])) != extension[i]) return false;
  }
  return true;
}

// The kind of the book EpubReaderActivity has open. A PDF is read from its converted text in /.vaultink/pdf/
// (TxtToHtml::isPdfText). Anything not known here counts as an EPUB, which shows every option.
inline BookKind bookKind(const std::string_view path) {
  constexpr std::string_view PDF_FOLDER = "/.vaultink/pdf/";
  if (hasExtension(path, ".txt")) {
    return path.size() > PDF_FOLDER.size() && path.compare(0, PDF_FOLDER.size(), PDF_FOLDER) == 0 ? BookKind::PdfText
                                                                                                  : BookKind::Txt;
  }
  if (hasExtension(path, ".md")) return BookKind::Note;
  if (hasExtension(path, ".mobi") || hasExtension(path, ".prc") || hasExtension(path, ".azw") ||
      hasExtension(path, ".azw3")) {
    return BookKind::Mobi;
  }
  if (hasExtension(path, ".fb2")) return BookKind::Fb2;
  if (hasExtension(path, ".html") || hasExtension(path, ".htm") || hasExtension(path, ".xhtml")) return BookKind::Html;
  if (hasExtension(path, ".rtf")) return BookKind::Rtf;
  if (hasExtension(path, ".doc")) return BookKind::Doc;
  if (hasExtension(path, ".chm")) return BookKind::Chm;
  return BookKind::Epub;
}

// The Style row (Embedded Style) changes something: the book's own styling, or a PDF's heading / bold / italic marks.
inline constexpr bool embeddedStyleApplies(const BookKind kind) {
  return kind != BookKind::Txt && kind != BookKind::Note && kind != BookKind::Doc;
}

// The page can carry a paragraph alignment of its own, for the "Book's style" alignment to follow. Without one
// that choice lays the text out exactly as Justify does.
inline constexpr bool bookAlignmentApplies(const BookKind kind) {
  return embeddedStyleApplies(kind) && kind != BookKind::PdfText;
}

// How many of the alignment choices to offer: "Book's style", the last one, is left out where it has nothing to
// follow.
inline constexpr int alignmentChoiceCount(const BookKind kind) {
  return bookAlignmentApplies(kind) ? ALIGNMENT_CHOICES : ALIGNMENT_CHOICES - 1;
}

// The choice to mark in the alignment list: a stored "Book's style" that is not offered shows as Justify (0),
// which is how the page is laid out then.
inline constexpr int alignmentChoiceShown(const BookKind kind, const int stored) {
  return stored >= 0 && stored < alignmentChoiceCount(kind) ? stored : 0;
}

// One bit per Text panel row this kind of book shows. Never empty.
inline constexpr uint32_t textRowMask(const BookKind kind) {
  uint32_t mask = (1u << TEXT_ROW_COUNT) - 1u;
  if (!embeddedStyleApplies(kind)) mask &= ~(1u << TEXT_ROW_EMBEDDED_STYLE);
  return mask;
}

inline constexpr bool showsTextRow(const BookKind kind, const int row) {
  return row >= 0 && row < TEXT_ROW_COUNT && (textRowMask(kind) & (1u << row)) != 0;
}

// Contents (the toolbar tool, "Select chapter" in the list menu): a list of one entry has nowhere to go. That is
// every TXT, PDF, MOBI, FB2, HTML, RTF, DOC, CHM and Markdown note (one entry, the title), and an EPUB whose table
// of contents has a single entry or none.
inline constexpr bool showContents(const int tocCount) { return tocCount > 1; }

}  // namespace formatfeatures
