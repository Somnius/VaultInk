#pragma once

#include <string>

class GfxRenderer;

// A PDF read as text (Phase 7): the text of its pages is written once to a .txt file on the card and
// that file opens in the book reader, so it reflows, scrolls and remembers its place like any text.
// No layout, pictures or scans (see lib/Pdf).
namespace pdfconvert {

enum class Status { Ok, NoText, Encrypted, Unreadable, SdError };

// Where the text of a PDF lives: /.vaultink/pdf/<hash of the path>/<name>.txt.
std::string textPathFor(const std::string& pdfPath);

// Makes sure the text file is there and matches the PDF (its size and date), converting with a
// progress popup when it isn't. On Ok, `textPath` is the file to open.
Status ensureText(GfxRenderer& renderer, const std::string& pdfPath, std::string& textPath);

// A popup naming why a PDF didn't open.
void showFailure(GfxRenderer& renderer, Status status);

}  // namespace pdfconvert
