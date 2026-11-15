#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "InflateStream.h"
#include "PdfText.h"

namespace {

std::string slurp(const std::string& name) {
  std::ifstream in(std::string(PDF_TEST_DATA) + "/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

pdf::Inflate inflater() {
  return [](const uint8_t* in, size_t len, std::string& out, size_t maxOut) {
    out.clear();
    InflateStream stream;
    if (!stream.init(true)) return false;
    stream.setSource(in, len);
    stream.setZlibWrapped();
    uint8_t buf[8192];
    while (true) {
      size_t produced = 0;
      const auto status = stream.readAtMost(buf, sizeof(buf), &produced);
      out.append(reinterpret_cast<char*>(buf), produced);
      if (out.size() > maxOut) return false;
      if (status == InflateStream::Status::Done) return true;
      if (status == InflateStream::Status::Error) return false;
    }
  };
}

struct Extracted {
  bool ok = false;
  pdf::Result result;
  std::string text;  // all pages, separated by a form feed
  size_t pages = 0;
};

Extracted extract(const std::string& bytes) {
  Extracted run;
  pdf::Source src;
  src.size = bytes.size();
  src.read = [&bytes](uint64_t off, uint8_t* out, size_t len) {
    if (off + len > bytes.size()) return false;
    std::copy(bytes.begin() + off, bytes.begin() + off + len, out);
    return true;
  };
  run.ok = pdf::extractText(
      src, inflater(),
      [&run](size_t, size_t, const std::string& t) {
        if (run.pages++) run.text += '\f';
        run.text += t;
      },
      &run.result);
  return run;
}

// The words of a text, whitespace collapsed.
std::string squeeze(const std::string& s) {
  std::string out;
  bool space = false;
  for (const char c : s) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f') {
      space = true;
      continue;
    }
    if (space && !out.empty()) out += ' ';
    space = false;
    out += c;
  }
  return out;
}

void expectSample(const std::string& file) {
  const Extracted run = extract(slurp(file));
  ASSERT_TRUE(run.ok) << file;
  EXPECT_EQ(run.result.pages, 1u);
  const std::string text = squeeze(run.text);
  EXPECT_NE(text.find("The Haunted Lighthouse"), std::string::npos) << text;
  EXPECT_NE(text.find("A storm rolled in from the west, and the keeper climbed the stairs"), std::string::npos) << text;
  EXPECT_NE(text.find("ninety-nine, one hundred, and then the iron door."), std::string::npos) << text;
  EXPECT_NE(text.find("Η καλημέρα είναι ευχή."), std::string::npos) << text;
  EXPECT_NE(text.find("Καλή ανάγνωση!"), std::string::npos) << text;
  EXPECT_NE(text.find("numbers 1234 and punctuation: (brackets), \"quotes\""), std::string::npos) << text;
}

}  // namespace

TEST(PdfText, LibreOfficeFile) { expectSample("libreoffice.pdf"); }
TEST(PdfText, ClassicCrossReferenceTable) { expectSample("classic-xref.pdf"); }
TEST(PdfText, CrossReferenceStreamAndObjectStreams) { expectSample("xref-stream.pdf"); }

TEST(PdfText, NotAPdf) {
  EXPECT_FALSE(extract("hello, this is not a pdf at all, just some text of enough length").ok);
  EXPECT_FALSE(extract("%PDF-1.4\n no trailer and no xref in this broken little file, nothing to read").ok);
}

TEST(PdfText, TruncatedFileFailsCleanly) {
  const std::string full = slurp("libreoffice.pdf");
  const Extracted run = extract(full.substr(0, full.size() / 2));
  EXPECT_FALSE(run.ok);
}

TEST(PdfText, Utf8Encoding) {
  std::string s;
  pdf::appendUtf8(s, 0x41);
  pdf::appendUtf8(s, 0x3B1);
  pdf::appendUtf8(s, 0x20AC);
  pdf::appendUtf8(s, 0x1F600);
  EXPECT_EQ(s, "A\xCE\xB1\xE2\x82\xAC\xF0\x9F\x98\x80");
}

TEST(PdfText, HandMadeFileCoversFiltersFormsAndPositions) {
  const Extracted run = extract(slurp("hand-made.pdf"));
  ASSERT_TRUE(run.ok);
  ASSERT_EQ(run.pages, 3u);
  const std::string text = squeeze(run.text);
  // ASCII85 + Flate stream, kerns as word spaces, a Differences encoding for the control codes.
  EXPECT_NE(text.find("Hello world from ASCII85"), std::string::npos) << text;
  EXPECT_NE(text.find("a caf\xC3\xA9 and \xC3\xA9\xCE\xB1"), std::string::npos) << text;
  // A far lower line starts a paragraph, a form XObject's text is read.
  EXPECT_NE(text.find("a fresh paragraph far below"), std::string::npos) << text;
  EXPECT_NE(text.find("inside a form"), std::string::npos) << text;
  // Words drawn one at a time through cm: the spaces come from the positions.
  EXPECT_NE(text.find("Quartz draws each word"), std::string::npos) << text;
}

TEST(PdfText, ParagraphBreaksFromLineGaps) {
  const Extracted run = extract(slurp("hand-made.pdf"));
  ASSERT_TRUE(run.ok);
  EXPECT_NE(run.text.find("\n\na fresh paragraph far below"), std::string::npos);
  EXPECT_EQ(run.text.find("\n\nsecond line"), std::string::npos);  // a normal line step joins the line
}

TEST(PdfText, FilledFormFieldsFollowThePageText) {
  const Extracted run = extract(slurp("hand-made.pdf"));
  ASSERT_TRUE(run.ok);
  const std::string text = squeeze(run.text);
  EXPECT_NE(text.find("Quartz draws each word Name: Ceren Yurdakul Clan: \xCE\x91\xCE\x9B\xCE\xA6"), std::string::npos)
      << text;
}

namespace {

// A small uncompressed PDF from object bodies, with a correct classic cross-reference table.
std::string buildPdf(const std::vector<std::string>& bodies, const std::string& trailerExtra = "") {
  std::string out = "%PDF-1.4\n";
  std::vector<size_t> offsets;
  for (size_t i = 0; i < bodies.size(); ++i) {
    offsets.push_back(out.size());
    out += std::to_string(i + 1) + " 0 obj\n" + bodies[i] + "\nendobj\n";
  }
  const size_t xref = out.size();
  out += "xref\n0 " + std::to_string(bodies.size() + 1) + "\n0000000000 65535 f \n";
  for (const size_t o : offsets) {
    char line[32];
    snprintf(line, sizeof(line), "%010zu 00000 n \n", o);
    out += line;
  }
  out += "trailer\n<< /Size " + std::to_string(bodies.size() + 1) + " /Root 1 0 R " + trailerExtra + ">>\nstartxref\n" +
         std::to_string(xref) + "\n%%EOF\n";
  return out;
}

std::string contentObject(const std::string& content) {
  return "<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream";
}

}  // namespace

TEST(PdfText, PlainUncompressedFile) {
  const std::string pdf =
      buildPdf({"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>",
                contentObject("BT /F1 12 Tf 72 700 Td (Plain text) Tj ET"),
                "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"});
  const Extracted run = extract(pdf);
  ASSERT_TRUE(run.ok);
  EXPECT_EQ(run.text, "Plain text");
}

TEST(PdfText, PageTreeThatLoopsBackDoesNotHang) {
  // Pages 2 lists itself, twice, as its kids.
  const std::string pdf =
      buildPdf({"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [2 0 R 2 0 R] /Count 2 >>"});
  const Extracted run = extract(pdf);
  EXPECT_TRUE(run.ok);
  EXPECT_EQ(run.pages, 0u);
}

bool extractEncrypted(const std::string& bytes) {
  const Extracted run = extract(bytes);
  return !run.ok && run.result.encrypted;
}

TEST(PdfText, EncryptedFileIsReportedNotRead) {
  const std::string pdf =
      buildPdf({"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [] /Count 0 >>"}, "/Encrypt 3 0 R ");
  const Extracted run = extract(pdf);
  EXPECT_FALSE(run.ok);
  EXPECT_TRUE(run.result.encrypted);
}

TEST(PdfText, WrongStreamLengthIsRecovered) {
  std::string content = "BT /F1 12 Tf 72 700 Td (Short length) Tj ET";
  const std::string pdf =
      buildPdf({"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>",
                "<< /Length 5 >>\nstream\n" + content + "\nendstream",
                "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"});
  const Extracted run = extract(pdf);
  ASSERT_TRUE(run.ok);
  EXPECT_EQ(run.text, "Short length");
}

TEST(PdfText, PagesJoinInTheMiddleOfASentence) {
  std::string prev = "the keeper climbed the";
  EXPECT_EQ(pdf::pageSeparator(prev, "stairs and"), " ");
  prev = "He counted the steps.";
  EXPECT_EQ(pdf::pageSeparator(prev, "then the door"), "\n\n");  // a full stop ends the paragraph
  prev = "a sentence ends here";
  EXPECT_EQ(pdf::pageSeparator(prev, "The next one"), "\n\n");  // the next page starts a new sentence
  prev = "a hyphen-";
  EXPECT_EQ(pdf::pageSeparator(prev, "ated word"), "");
  EXPECT_EQ(prev, "a hyphen");
  prev = "an en dash -";
  EXPECT_EQ(pdf::pageSeparator(prev, "dash"), "\n\n");  // a lone dash isn't a hyphenated word
  prev = "ο φάρος στέκεται στην άκρη του";
  EXPECT_EQ(pdf::pageSeparator(prev, "νησιού, και"), " ");  // Greek lower case continues
  prev = "ο φάρος";
  EXPECT_EQ(pdf::pageSeparator(prev, "Η καλημέρα"), "\n\n");
  prev = "";
  EXPECT_EQ(pdf::pageSeparator(prev, "text"), "");
}

TEST(PdfText, TwoColumnsReadLeftThenRight) {
  const Extracted run = extract(slurp("hand-made.pdf"));
  ASSERT_TRUE(run.ok);
  const std::string text = squeeze(run.text);
  const size_t title = text.find("A title across both columns of this page");
  const size_t left1 = text.find("Left 01");
  const size_t left12 = text.find("Left 12");
  const size_t right1 = text.find("Right 01");
  const size_t right12 = text.find("Right 12");
  ASSERT_NE(title, std::string::npos) << text;
  ASSERT_NE(right12, std::string::npos) << text;
  EXPECT_LT(title, left1);
  EXPECT_LT(left1, left12);
  EXPECT_LT(left12, right1);  // the whole left column comes first
  EXPECT_LT(right1, right12);
}

// Text of page `index` (the pages of one file are separated by form feeds).
std::string pageOf(const Extracted& run, size_t index) {
  size_t from = 0;
  for (size_t i = 0; i < index; ++i) from = run.text.find('\f', from) + 1;
  return run.text.substr(from, run.text.find('\f', from) - from);
}

void expectColumnsInOrder(const std::string& page, const std::string& tag, int columns) {
  size_t last = 0;
  for (int k = 1; k <= columns; ++k) {
    for (int row = 1; row <= 14; ++row) {
      char needle[32];
      std::snprintf(needle, sizeof(needle), "%s%d line %02d", tag.c_str(), k, row);
      const size_t at = page.find(needle, last);
      ASSERT_NE(at, std::string::npos) << needle << "\n" << page;
      last = at;
    }
  }
}

TEST(PdfText, ThreeAndFourColumnsReadColumnByColumn) {
  const Extracted run = extract(slurp("columns.pdf"));
  ASSERT_TRUE(run.ok);
  ASSERT_EQ(run.pages, 4u);
  expectColumnsInOrder(pageOf(run, 0), "C", 3);
  expectColumnsInOrder(pageOf(run, 1), "D", 4);
}

TEST(PdfText, TwoLineLongColumnsUnderATitleStillWork) {
  const Extracted run = extract(slurp("columns.pdf"));
  ASSERT_TRUE(run.ok);
  const std::string page = squeeze(pageOf(run, 3));
  EXPECT_LT(page.find("A title across both columns"), page.find("Left 01"));
  EXPECT_LT(page.find("Left 14"), page.find("Right 01"));
}

TEST(PdfText, CidFontsWithUnicodeCMapsAreReadAndOthersAreCounted) {
  const Extracted run = extract(slurp("cid.pdf"));
  ASSERT_TRUE(run.ok);
  ASSERT_EQ(run.pages, 4u);
  EXPECT_EQ(squeeze(pageOf(run, 0)), "Hello 世界 Ωμέγα");
  EXPECT_NE(pageOf(run, 1).find("Pair \xF0\x9F\x98\x80 done"), std::string::npos) << pageOf(run, 1);
  EXPECT_EQ(pageOf(run, 2), "");  // Identity-H with no ToUnicode: nothing, no raw bytes, no question marks
  const std::string mixed = pageOf(run, 3);
  EXPECT_NE(mixed.find("Plain text 5 stays readable"), std::string::npos) << mixed;
  EXPECT_EQ(mixed.find('?'), std::string::npos);
  EXPECT_EQ(run.result.textPages, 3u);
  EXPECT_EQ(run.result.droppedGlyphs, 6u * 5u + 3u);
}

TEST(PdfText, AFileOfOnlyUnreadableGlyphsReportsThemDropped) {
  const Extracted run = extract(slurp("cid-only.pdf"));
  ASSERT_TRUE(run.ok);
  EXPECT_EQ(run.result.textPages, 0u);
  EXPECT_EQ(run.result.droppedGlyphs, 30u);
  EXPECT_EQ(squeeze(run.text), "");
}

TEST(PdfText, EncryptedWithAnEmptyUserPasswordIsRead) {
  for (const char* file : {"rc4-40.pdf", "rc4-128.pdf"}) {
    SCOPED_TRACE(file);
    const Extracted run = extract(slurp(file));
    ASSERT_TRUE(run.ok);
    EXPECT_FALSE(run.result.encrypted);
    const std::string text = squeeze(run.text);
    EXPECT_NE(text.find("The Haunted Lighthouse"), std::string::npos) << text;
    EXPECT_NE(text.find("Η καλημέρα είναι ευχή."), std::string::npos) << text;
  }
}

TEST(PdfText, Aes128StreamsAndStringsAreDecrypted) {
  const Extracted run = extract(slurp("aes128.pdf"));
  ASSERT_TRUE(run.ok);
  const std::string text = squeeze(run.text);
  EXPECT_NE(text.find("Encrypted with AES-128 and read anyway"), std::string::npos) << text;
  EXPECT_NE(text.find("Clan: Ventrue"), std::string::npos) << text;
}

TEST(PdfText, Aes256Revisions5And6AreDecrypted) {
  for (const char* file : {"aes256-r5.pdf", "aes256-r6.pdf"}) {
    SCOPED_TRACE(file);
    const Extracted run = extract(slurp(file));
    ASSERT_TRUE(run.ok);
    EXPECT_FALSE(run.result.encrypted);
    EXPECT_NE(squeeze(run.text).find("read anyway"), std::string::npos) << run.text;
  }
}

TEST(PdfText, Aes256WithAWrongOrBrokenKeyDataIsReportedEncrypted) {
  for (const char* file : {"aes256-r5.pdf", "aes256-r6.pdf"}) {
    SCOPED_TRACE(file);
    const std::string good = slurp(file);
    // A different validation salt means the empty password no longer matches /U.
    std::string wrong = good;
    const size_t u = wrong.find("/U <");
    ASSERT_NE(u, std::string::npos);
    wrong[u + 4 + 64 + 2] = wrong[u + 4 + 64 + 2] == '0' ? '1' : '0';
    EXPECT_TRUE(extractEncrypted(wrong));
    // Truncated /U, /UE and /O (and a missing /UE) must not crash and must not open the file.
    for (const char* key : {"/U <", "/UE <", "/O <"}) {
      std::string cut = good;
      const size_t at = cut.find(key);
      ASSERT_NE(at, std::string::npos);
      const size_t close = cut.find('>', at);
      const size_t from = at + std::string(key).size() + 8;
      cut.replace(from, close - from, close - from, ' ');  // spaces keep the xref offsets valid
      const Extracted run = extract(cut);
      if (std::string(key) != "/O <") {
        EXPECT_FALSE(run.ok);
        EXPECT_TRUE(run.result.encrypted);
      }
    }
    std::string noUe = good;
    noUe.replace(noUe.find("/UE"), 3, "/XX");
    EXPECT_TRUE(extractEncrypted(noUe));
  }
}

TEST(PdfText, AFileThatNeedsAPasswordIsNotRead) {
  const Extracted run = extract(slurp("needs-password.pdf"));
  EXPECT_FALSE(run.ok);
  EXPECT_TRUE(run.result.encrypted);
}

// kos25k #29: a tinted callout is drawn as a filled rectangle behind the text. It gets a line of em dashes
// above and below, so the box shows in the plain text.
TEST(PdfText, FilledBoxesGetRuleLines) {
  const auto run = extract(slurp("reflow-boxes-gr.pdf"));
  ASSERT_TRUE(run.ok);
  const std::string rule =
      "\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94"
      "\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94";
  size_t count = 0;
  for (size_t at = run.text.find(rule); at != std::string::npos; at = run.text.find(rule, at + rule.size())) ++count;
  EXPECT_EQ(count, 4u);  // two boxes: the callout on page 1 and "Τέλος δοκιμής" on page 3
  const size_t open = run.text.find(rule);
  ASSERT_NE(open, std::string::npos);
  EXPECT_EQ(run.text.compare(open + rule.size(), 3, "\n\xCE\x91"), 0);  // the callout's first word, Αν, follows
  EXPECT_NE(run.text.find(rule + "\n\xCE\xA4\xCE\xAD\xCE\xBB\xCE\xBF\xCF\x82"), std::string::npos);  // Τέλος
}

TEST(PdfText, PagesWithoutBoxesStayPlain) {
  const auto run = extract(slurp("columns.pdf"));
  ASSERT_TRUE(run.ok);
  EXPECT_EQ(run.text.find("\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94\xE2\x80\x94"), std::string::npos);
}

Extracted extractStyled(const std::string& bytes) {
  Extracted run;
  pdf::Source src;
  src.size = bytes.size();
  src.read = [&bytes](uint64_t off, uint8_t* out, size_t len) {
    if (off + len > bytes.size()) return false;
    std::copy(bytes.begin() + off, bytes.begin() + off + len, out);
    return true;
  };
  pdf::Limits limits;
  limits.styleMarks = true;
  run.ok = pdf::extractText(
      src, inflater(),
      [&run](size_t, size_t, const std::string& t) {
        if (run.pages++) run.text += '\f';
        run.text += t;
      },
      &run.result, limits);
  return run;
}

// kos25k #29 (0.73.0): the title is a heading and the callout's bold text is marked; the words are the same.
TEST(PdfText, StyleMarksMakeHeadingsAndBold) {
  const auto plain = extract(slurp("reflow-boxes-gr.pdf"));
  const auto styled = extractStyled(slurp("reflow-boxes-gr.pdf"));
  ASSERT_TRUE(styled.ok);
  EXPECT_NE(styled.text.find("\x01"), std::string::npos);  // a heading starts
  EXPECT_NE(styled.text.find("\x04"), std::string::npos);  // and ends
  EXPECT_NE(styled.text.find("\x02"), std::string::npos);  // bold on
  // Without the marks it is the text it was.
  std::string stripped;
  for (size_t i = 0; i < styled.text.size(); ++i) {
    const char c = styled.text[i];
    if (c == 1) {
      ++i;  // the heading level after it
    } else if (c < 1 || c > 4) {
      stripped += c;
    }
  }
  EXPECT_TRUE(squeeze(stripped) == squeeze(plain.text));  // not EXPECT_EQ: a failure would print the whole text
  // Marks off by default: plain text has none of the bytes.
  for (const char c : plain.text) EXPECT_TRUE(c < 1 || c > 4);
}

namespace {

// Extracts with `limits`, and notes how deep the stack went below the caller when the file was read (the library
// reads objects from inside the content-stream interpreter, so the deepest read is under its nested frames).
Extracted extractWith(const std::string& bytes, const pdf::Limits& limits, size_t* stackUsed = nullptr) {
  Extracted run;
  char base = 0;
  const char* const top = &base;
  size_t deepest = 0;
  pdf::Source src;
  src.size = bytes.size();
  src.read = [&bytes, &deepest, top](uint64_t off, uint8_t* out, size_t len) {
    char here = 0;
    const char* const now = &here;
    if (now < top) deepest = std::max(deepest, static_cast<size_t>(top - now));
    if (off + len > bytes.size()) return false;
    std::copy(bytes.begin() + off, bytes.begin() + off + len, out);
    return true;
  };
  run.ok = pdf::extractText(
      src, inflater(),
      [&run](size_t, size_t, const std::string& t) {
        if (run.pages++) run.text += '\f';
        run.text += t;
      },
      &run.result, limits);
  if (stackUsed) *stackUsed = deepest;
  return run;
}

std::string formObject(const std::string& content, const std::string& resources) {
  return "<< /Type /XObject /Subtype /Form /BBox [0 0 600 800] /Resources " + resources + " /Length " +
         std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream";
}

// A page that draws a form, that draws a form, that draws a form: each level picks a font of its own, so the
// innermost font is loaded with all the content streams above it still running.
std::string nestedFormsPdf() {
  const std::string font = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
  return buildPdf(
      {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
       "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /Font << /F1 8 0 R >> /XObject << /A 5 0 R >> >> >>",
       contentObject("BT /F1 12 Tf 72 700 Td (page) Tj ET /A Do"),
       formObject("BT /F2 12 Tf 72 660 Td (outer) Tj ET /B Do", "<< /Font << /F2 9 0 R >> /XObject << /B 6 0 R >> >>"),
       formObject("BT /F3 12 Tf 72 620 Td (middle) Tj ET /C Do",
                  "<< /Font << /F3 10 0 R >> /XObject << /C 7 0 R >> >>"),
       formObject("BT /F4 12 Tf 72 580 Td (inner) Tj ET", "<< /Font << /F4 11 0 R >> >>"), font, font, font, font});
}

}  // namespace

// The reader's task stack is 24 KB. With the font loader inside the interpreter's frame (8.4 KB a level on the
// reader) a page of forms inside forms ran over it and the reader reset while opening the book. The frame is a
// different size on the computer that runs this test, so the limit here is only a guard against the same mistake:
// this file took 48 KB before and takes 14 KB now (an optimised build; other builds are not checked).
TEST(PdfText, FormsInsideFormsStayOnASmallStack) {
  size_t stackUsed = 0;
  const Extracted run = extractWith(nestedFormsPdf(), pdf::Limits(), &stackUsed);
  ASSERT_TRUE(run.ok);
  EXPECT_EQ(squeeze(run.text), "page outer middle inner");
#if !defined(__SANITIZE_ADDRESS__) && defined(NDEBUG)
  EXPECT_LT(stackUsed, 24u * 1024u);
#endif
}

TEST(PdfText, AFormThatDrawsItselfStops) {
  const std::string pdf = buildPdf(
      {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
       "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /XObject << /A 5 0 R >> >> >>",
       contentObject("/A Do"),
       formObject("BT /F1 12 Tf 72 700 Td (again) Tj ET /A Do", "<< /Font << /F1 6 0 R >> /XObject << /A 5 0 R >> >>"),
       "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"});
  const Extracted run = extract(pdf);
  ASSERT_TRUE(run.ok);
  EXPECT_EQ(squeeze(run.text), "again again again");  // three forms deep, no further
}

// A font written inside the resources has no object number: it is loaded once for the stream, not at every Tf.
TEST(PdfText, AFontWrittenInTheResourcesIsRead) {
  std::string content = "BT 72 700 Td ";
  for (int k = 0; k < 400; ++k) content += "/F1 12 Tf (ab) Tj /F2 12 Tf (cd) Tj ";
  content += "ET";
  const std::string pdf = buildPdf(
      {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
       "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /Font << "
       "/F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> "
       "/F2 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding << /Differences [99 /x /y] >> >> >> >> >>",
       contentObject(content)});
  const Extracted run = extract(pdf);
  ASSERT_TRUE(run.ok);
  EXPECT_EQ(run.text.size(), 1600u);
  EXPECT_EQ(run.text.compare(0, 8, "abxyabxy"), 0);
}

// The memory kept between pages has limits (fonts, cached objects). With both at nothing the text is the same.
TEST(PdfText, TextIsTheSameWithNoFontsOrObjectsKeptBetweenPages) {
  pdf::Limits tight;
  tight.maxFontBytes = 0;
  tight.maxCacheBytes = 0;
  for (const char* file : {"libreoffice.pdf", "xref-stream.pdf", "cid.pdf", "columns.pdf", "hand-made.pdf"}) {
    const Extracted loose = extract(slurp(file));
    const Extracted run = extractWith(slurp(file), tight);
    ASSERT_TRUE(run.ok) << file;
    EXPECT_FALSE(run.text.empty()) << file;
    EXPECT_TRUE(run.text == loose.text) << file;
    EXPECT_EQ(run.result.droppedGlyphs, loose.result.droppedGlyphs) << file;
  }
}

TEST(PdfText, APageTreeDeeperThanTheLimitIsNotWalked) {
  const auto tree = [](const int levels) {
    std::vector<std::string> bodies = {"<< /Type /Catalog /Pages 2 0 R >>"};
    for (int k = 0; k < levels; ++k) {
      bodies.push_back("<< /Type /Pages /Kids [" + std::to_string(k + 3) + " 0 R] /Count 1 >>");
    }
    const int page = levels + 2;
    bodies.push_back("<< /Type /Page /Contents " + std::to_string(page + 1) + " 0 R /Resources << /Font << /F1 " +
                     std::to_string(page + 2) + " 0 R >> >> >>");
    bodies.push_back(contentObject("BT /F1 12 Tf 72 700 Td (deep) Tj ET"));
    bodies.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    return buildPdf(bodies);
  };
  const Extracted fine = extract(tree(12));
  ASSERT_TRUE(fine.ok);
  EXPECT_EQ(fine.text, "deep");
  const Extracted tooDeep = extract(tree(40));
  EXPECT_TRUE(tooDeep.ok);
  EXPECT_EQ(tooDeep.pages, 0u);
}

TEST(PdfText, APagesStreamsTogetherStayUnderTheStreamLimit) {
  const std::string pdf = buildPdf(
      {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
       "<< /Type /Page /Parent 2 0 R /Contents [4 0 R 5 0 R] /Resources << /Font << /F1 6 0 R >> >> >>",
       contentObject("BT /F1 12 Tf 72 700 Td (first) Tj ET"), contentObject("BT /F1 12 Tf 72 600 Td (second) Tj ET"),
       "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"});
  EXPECT_EQ(squeeze(extract(pdf).text), "first second");
  pdf::Limits limits;
  limits.maxStreamBytes = 60;  // each stream fits, both together don't
  const Extracted run = extractWith(pdf, limits);
  ASSERT_TRUE(run.ok);
  EXPECT_EQ(run.text, "first");
}

// One glyph per operator is one piece of text each: the page stops at a fixed number of them.
TEST(PdfText, APageOfVeryManyPiecesStopsAtTheLimit) {
  std::string content = "BT /F1 12 Tf 72 700 Td ";
  for (int k = 0; k < 25000; ++k) content += "(a) Tj ";
  content += "ET";
  const std::string pdf =
      buildPdf({"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>",
                contentObject(content), "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"});
  const Extracted run = extract(pdf);
  ASSERT_TRUE(run.ok);
  EXPECT_EQ(std::count(run.text.begin(), run.text.end(), 'a'), 20000);
}

// The memory guard (Limits::freeMemory). With memory to spare nothing changes.
TEST(PdfText, TheMemoryGuardChangesNothingWithMemoryToSpare) {
  pdf::Limits guarded;
  size_t asked = 0;
  guarded.freeMemory = [&asked] {
    ++asked;
    return size_t{144000};
  };
  for (const char* file : {"libreoffice.pdf", "xref-stream.pdf", "classic-xref.pdf", "cid.pdf", "columns.pdf"}) {
    const Extracted loose = extract(slurp(file));
    const Extracted run = extractWith(slurp(file), guarded);
    ASSERT_TRUE(run.ok) << file;
    EXPECT_FALSE(run.result.outOfMemory) << file;
    EXPECT_TRUE(run.text == loose.text) << file;
  }
  EXPECT_GT(asked, 20u);
}

// Under the limit but over half of it: the cached objects and the fonts are dropped at every look and read again,
// and the text is whole.
TEST(PdfText, LowMemoryDropsWhatCanBeReadAgain) {
  pdf::Limits guarded;
  guarded.freeMemory = [] { return size_t{30000}; };
  for (const char* file : {"libreoffice.pdf", "xref-stream.pdf", "cid.pdf", "columns.pdf", "hand-made.pdf"}) {
    const Extracted loose = extract(slurp(file));
    const Extracted run = extractWith(slurp(file), guarded);
    ASSERT_TRUE(run.ok) << file;
    EXPECT_FALSE(run.result.outOfMemory) << file;
    EXPECT_TRUE(run.text == loose.text) << file;
  }
  const Extracted forms = extractWith(nestedFormsPdf(), guarded);
  ASSERT_TRUE(forms.ok);
  EXPECT_EQ(squeeze(forms.text), "page outer middle inner");
}

std::string manyPagesPdf(const int pages) {
  std::vector<std::string> bodies = {"<< /Type /Catalog /Pages 2 0 R >>", ""};
  std::string kids;
  for (int k = 0; k < pages; ++k) {
    const int page = 3 + 2 * k;
    kids += std::to_string(page) + " 0 R ";
    bodies.push_back("<< /Type /Page /Parent 2 0 R /Contents " + std::to_string(page + 1) +
                     " 0 R /Resources << /Font << /F1 " + std::to_string(3 + 2 * pages) + " 0 R >> >> >>");
    bodies.push_back(contentObject("BT /F1 12 Tf 72 700 Td (page" + std::to_string(k) + ") Tj ET"));
  }
  bodies[1] = "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages) + " >>";
  bodies.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
  return buildPdf(bodies);
}

// Memory that only shrinks: first the cache and the fonts go, then the extraction stops with what it has, and says so.
TEST(PdfText, MemoryThatRunsOutStopsTheExtractionCleanly) {
  const std::string pdf = manyPagesPdf(40);
  const Extracted loose = extract(pdf);
  ASSERT_TRUE(loose.ok);
  ASSERT_EQ(loose.pages, 40u);

  pdf::Limits guarded;
  size_t freeNow = 60000;
  size_t lowLooks = 0;
  guarded.freeMemory = [&freeNow, &lowLooks, &guarded] {
    freeNow -= std::min<size_t>(freeNow, 150);
    if (freeNow < guarded.minFreeMemory) ++lowLooks;
    return freeNow;
  };
  const Extracted run = extractWith(pdf, guarded);
  EXPECT_FALSE(run.ok);
  EXPECT_TRUE(run.result.outOfMemory);
  EXPECT_FALSE(run.result.encrypted);
  EXPECT_FALSE(run.result.xrefFailed);
  EXPECT_GT(run.pages, 0u);
  EXPECT_LT(run.pages, 40u);
  EXPECT_GT(lowLooks, 50u);  // it went on, with the cache dropped, between the limit and half of it
  // What was given before it stopped is the start of the whole text, page for page.
  EXPECT_EQ(loose.text.compare(0, run.text.size(), run.text), 0);

  // Nothing free from the start: no page at all.
  pdf::Limits none;
  none.freeMemory = [] { return size_t{0}; };
  const Extracted nothing = extractWith(pdf, none);
  EXPECT_FALSE(nothing.ok);
  EXPECT_TRUE(nothing.result.outOfMemory);
  EXPECT_EQ(nothing.pages, 0u);
}

// One look that finds almost nothing free, at every place the guard looks in turn: the page that was being read is
// read again from its start, so the text is whole, or the extraction says it ran out. Never a wrong text.
TEST(PdfText, AMomentOfNoMemoryNeverGivesAWrongText) {
  const std::vector<std::string> files = {slurp("libreoffice.pdf"), slurp("cid.pdf"), slurp("xref-stream.pdf"),
                                          nestedFormsPdf(), manyPagesPdf(6)};
  for (size_t f = 0; f < files.size(); ++f) {
    const Extracted loose = extract(files[f]);
    ASSERT_TRUE(loose.ok);
    size_t looks = 0;
    pdf::Limits counting;
    counting.freeMemory = [&looks] {
      ++looks;
      return size_t{144000};
    };
    ASSERT_TRUE(extractWith(files[f], counting).ok);
    const size_t total = looks;
    size_t whole = 0;
    for (size_t dip = 1; dip <= total; ++dip) {
      size_t look = 0;
      pdf::Limits guarded;
      guarded.freeMemory = [&look, dip] { return ++look == dip ? size_t{100} : size_t{144000}; };
      const Extracted run = extractWith(files[f], guarded);
      if (run.ok) {
        ++whole;
        EXPECT_FALSE(run.result.outOfMemory) << f << " dip " << dip;
        EXPECT_TRUE(run.text == loose.text) << f << " dip " << dip;
      } else {
        EXPECT_TRUE(run.result.outOfMemory) << f << " dip " << dip;
      }
    }
    EXPECT_GT(whole, total / 2) << f;
  }
}
