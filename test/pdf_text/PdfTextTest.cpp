#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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
  ASSERT_EQ(run.pages, 2u);
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
