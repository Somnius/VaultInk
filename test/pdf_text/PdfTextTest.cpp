#include <gtest/gtest.h>

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

TEST(PdfText, AFileThatNeedsAPasswordIsNotRead) {
  const Extracted run = extract(slurp("needs-password.pdf"));
  EXPECT_FALSE(run.ok);
  EXPECT_TRUE(run.result.encrypted);
}
