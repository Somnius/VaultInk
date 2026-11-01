#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "Excalidraw.h"
#include "LzStringVectors.inc"

namespace {

using excalidraw::Drawing;
using excalidraw::Error;
using excalidraw::Kind;

// Feeds `text` in small chunks, like an SD read loop.
excalidraw::Reader readerFor(const std::string& text, size_t chunk = 7) {
  auto pos = std::make_shared<size_t>(0);
  return [text, pos, chunk](char* buf, size_t cap) {
    const size_t n = std::min<size_t>({cap, chunk, text.size() - *pos});
    std::memcpy(buf, text.data() + *pos, n);
    *pos += n;
    return n;
  };
}

std::string freedrawPoints(const int n) {
  std::string s = "[";
  for (int i = 0; i < n; ++i) {
    if (i) s += ",";
    s += "[" + std::to_string(i) + "," + std::to_string(i * 2) + "]";
  }
  return s + "]";
}

const std::string DEMO = R"({
  "type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
  "elements": [
    {"id":"r1","type":"rectangle","x":10,"y":20,"width":100,"height":50,"angle":0,
     "strokeColor":"#1e1e1e","backgroundColor":"#a5d8ff","fillStyle":"solid","strokeWidth":2,
     "strokeStyle":"dashed","roughness":1,"opacity":100,"groupIds":["g1"],"frameId":null,
     "roundness":{"type":3},"seed":1,"version":3,"versionNonce":4,"isDeleted":false,
     "boundElements":[{"id":"a1","type":"arrow"}],"updated":1,"link":null,"locked":false},
    {"id":"e1","type":"ellipse","x":200,"y":20,"width":80,"height":80,"angle":0.5,
     "backgroundColor":"transparent","strokeWidth":1,"strokeStyle":"dotted","groupIds":["g1"]},
    {"id":"d1","type":"diamond","x":300,"y":-10,"width":60,"height":60,"angle":0,
     "backgroundColor":"#ffc9c9","strokeStyle":"solid","groupIds":[]},
    {"id":"t1","type":"text","x":15,"y":30,"width":90,"height":40,"angle":0,
     "text":"Hello\nΓειά σου κόσμε","fontSize":20,"fontFamily":1,"textAlign":"left",
     "verticalAlign":"top","containerId":"r1","originalText":"Hello\nΓειά σου κόσμε","lineHeight":1.25},
    {"id":"a1","type":"arrow","x":110,"y":45,"width":90,"height":15,"angle":0,
     "points":[[0,0],[90,15]],"lastCommittedPoint":null,
     "startBinding":{"elementId":"r1","focus":0,"gap":1},"endBinding":null,
     "startArrowhead":"bar","endArrowhead":"arrow","strokeWidth":4},
    {"id":"l1","type":"line","x":0,"y":200,"width":50,"height":0,"angle":0,
     "points":[[0,0],[25,10],[50,0]],"startArrowhead":null,"endArrowhead":null},
    {"id":"f1","type":"freedraw","x":400,"y":400,"width":499,"height":998,"angle":0,
     "points":)" + freedrawPoints(500) +
                         R"(,"pressures":[],"simulatePressure":true,"lastCommittedPoint":[499,998]},
    {"id":"gone","type":"rectangle","x":-9999,"y":-9999,"width":10,"height":10,"isDeleted":true},
    {"id":"i1","type":"image","x":500,"y":500,"width":200,"height":200,"fileId":"abc","status":"saved","scale":[1,1]},
    {"id":"fr1","type":"frame","x":0,"y":0,"width":900,"height":900,"name":"Frame"}
  ],
  "appState": {"gridSize": null, "viewBackgroundColor": "#ffffff"},
  "files": {}
})";

}  // namespace

TEST(Excalidraw, ParsesShapesInOrder) {
  Drawing d;
  ASSERT_EQ(excalidraw::parse(readerFor(DEMO), d), Error::Ok);
  ASSERT_EQ(d.shapes.size(), 7u);
  EXPECT_EQ(d.skipped, 3u);  // deleted, image, frame
  EXPECT_FALSE(d.truncated);

  const auto& r = d.shapes[0];
  EXPECT_EQ(r.kind, Kind::Rect);
  EXPECT_FLOAT_EQ(r.x, 10);
  EXPECT_FLOAT_EQ(r.y, 20);
  EXPECT_FLOAT_EQ(r.w, 100);
  EXPECT_FLOAT_EQ(r.h, 50);
  EXPECT_EQ(r.strokeStyle, 1);
  EXPECT_EQ(r.fill, 1);
  EXPECT_EQ(r.strokeWidth, 2);
  EXPECT_EQ(r.groupId, 0);
  EXPECT_EQ(r.pointCount, 0);

  const auto& e = d.shapes[1];
  EXPECT_EQ(e.kind, Kind::Ellipse);
  EXPECT_FLOAT_EQ(e.angle, 0.5f);
  EXPECT_EQ(e.strokeStyle, 2);
  EXPECT_EQ(e.fill, 0);
  EXPECT_EQ(e.groupId, 0);  // same group as the rectangle

  const auto& dm = d.shapes[2];
  EXPECT_EQ(dm.kind, Kind::Diamond);
  EXPECT_EQ(dm.fill, 1);
  EXPECT_EQ(dm.groupId, -1);

  const auto& t = d.shapes[3];
  EXPECT_EQ(t.kind, Kind::Text);
  EXPECT_EQ(d.textOf(t), "Hello\nΓειά σου κόσμε");

  const auto& a = d.shapes[4];
  EXPECT_EQ(a.kind, Kind::Arrow);
  EXPECT_TRUE(a.startArrow);
  EXPECT_TRUE(a.endArrow);
  EXPECT_EQ(a.strokeWidth, 4);
  ASSERT_EQ(a.pointCount, 2);
  EXPECT_EQ(d.points[a.pointStart + 1].x, 90);
  EXPECT_EQ(d.points[a.pointStart + 1].y, 15);

  const auto& l = d.shapes[5];
  EXPECT_EQ(l.kind, Kind::Line);
  EXPECT_FALSE(l.startArrow);
  EXPECT_FALSE(l.endArrow);
  ASSERT_EQ(l.pointCount, 3);
  EXPECT_EQ(d.points[l.pointStart + 1].y, 10);

  const auto& f = d.shapes[6];
  EXPECT_EQ(f.kind, Kind::FreeDraw);
  EXPECT_LE(f.pointCount, excalidraw::FREEDRAW_POINTS);
  EXPECT_GE(f.pointCount, 32);
  EXPECT_EQ(d.points[f.pointStart].x, 0);
  EXPECT_EQ(d.points[f.pointStart + f.pointCount - 1].x, 499);  // the last point survives
  EXPECT_EQ(d.points[f.pointStart + f.pointCount - 1].y, 998);
  EXPECT_EQ(d.points.size(), 2u + 3u + f.pointCount);

  // Bounds: the deleted element at -9999 is not counted; the freedraw reaches 899,1398.
  EXPECT_FLOAT_EQ(d.minX, 0);
  EXPECT_FLOAT_EQ(d.minY, -10);
  EXPECT_FLOAT_EQ(d.maxX, 899);
  EXPECT_FLOAT_EQ(d.maxY, 1398);
}

TEST(Excalidraw, ArrowDefaultsAndEscapes) {
  const std::string json = R"({"elements":[
    {"id":"a","type":"arrow","x":0,"y":0,"width":1,"height":1,"points":[[0,0],[1,1]]},
    {"id":"t","type":"text","x":0,"y":0,"width":1,"height":1,"text":"café \"q\" 😀"}]})";
  Drawing d;
  ASSERT_EQ(excalidraw::parse(readerFor(json), d), Error::Ok);
  ASSERT_EQ(d.shapes.size(), 2u);
  EXPECT_FALSE(d.shapes[0].startArrow);
  EXPECT_TRUE(d.shapes[0].endArrow);  // an arrow without endArrowhead has one
  EXPECT_EQ(d.textOf(d.shapes[1]), "caf\xc3\xa9 \"q\" \xf0\x9f\x98\x80");
}

TEST(Excalidraw, LongTextCutAtCharacterBoundary) {
  std::string greek;
  for (int i = 0; i < 200; ++i) greek += "\xce\xb1";  // 400 bytes of α
  const std::string json =
      R"({"elements":[{"id":"t","type":"text","x":0,"y":0,"width":1,"height":1,"text":")" + greek + R"("}]})";
  Drawing d;
  ASSERT_EQ(excalidraw::parse(readerFor(json), d), Error::Ok);
  ASSERT_EQ(d.shapes.size(), 1u);
  EXPECT_LE(d.shapes[0].textLength, excalidraw::TEXT_CHARS);
  EXPECT_EQ(d.shapes[0].textLength % 2, 0);
  EXPECT_FALSE(d.truncated);
}

TEST(Excalidraw, BrokenFilesDoNotCrash) {
  Drawing d;
  EXPECT_EQ(excalidraw::parse(readerFor(""), d), Error::NotJson);
  EXPECT_EQ(excalidraw::parse(readerFor("[1,2]"), d), Error::NotJson);
  EXPECT_EQ(excalidraw::parse(readerFor("just some text\nno fence here"), d), Error::NotJson);
  // A cut tail keeps what was read.
  ASSERT_EQ(excalidraw::parse(readerFor(DEMO.substr(0, DEMO.find("\"d1\""))), d), Error::Ok);
  EXPECT_EQ(d.shapes.size(), 2u);
  // Garbage inside an element.
  ASSERT_EQ(excalidraw::parse(readerFor(R"({"elements":[{"id":"r","type":"rectangle","x":[[[,"y":}}}]})"), d),
            Error::Ok);
  EXPECT_TRUE(d.shapes.empty());
  ASSERT_EQ(excalidraw::parse(readerFor(R"({"elements":[{"type":"line","points":[[1],[2,"x"],[3,4,5]]}]})"), d),
            Error::Ok);
  ASSERT_EQ(d.shapes.size(), 1u);
  EXPECT_EQ(d.shapes[0].pointCount, 1);
}

TEST(Excalidraw, TruncatesAtTheShapeCap) {
  std::string json = R"({"elements":[)";
  for (size_t i = 0; i < excalidraw::MAX_SHAPES + 100; ++i) {
    if (i) json += ",";
    json += R"({"id":"r","type":"rectangle","x":)" + std::to_string(i) + R"(,"y":0,"width":1,"height":1})";
  }
  json += "]}";
  Drawing d;
  ASSERT_EQ(excalidraw::parse(readerFor(json, 1024), d), Error::Ok);
  EXPECT_EQ(d.shapes.size(), excalidraw::MAX_SHAPES);
  EXPECT_TRUE(d.truncated);
  EXPECT_FLOAT_EQ(d.maxX, static_cast<float>(excalidraw::MAX_SHAPES));
}

TEST(Excalidraw, TruncatesAtThePointCap) {
  std::string json = R"({"elements":[)";
  const size_t lines = excalidraw::MAX_POINTS / 100 + 5;
  for (size_t i = 0; i < lines; ++i) {
    if (i) json += ",";
    json += R"({"id":"l","type":"line","x":0,"y":0,"width":1,"height":1,"points":)" + freedrawPoints(100) + "}";
  }
  json += "]}";
  Drawing d;
  ASSERT_EQ(excalidraw::parse(readerFor(json, 1024), d), Error::Ok);
  EXPECT_EQ(d.shapes.size(), lines);  // shapes stay, the late ones have no points
  EXPECT_LE(d.points.size(), excalidraw::MAX_POINTS);
  EXPECT_EQ(d.points.size(), excalidraw::MAX_POINTS);
  EXPECT_EQ(d.shapes.back().pointCount, 0);
  EXPECT_TRUE(d.truncated);
}

TEST(Excalidraw, TruncatesAtTheTextCap) {
  std::string json = R"({"elements":[)";
  const std::string body(excalidraw::TEXT_CHARS, 'x');
  const size_t texts = excalidraw::MAX_TEXT / excalidraw::TEXT_CHARS + 5;
  for (size_t i = 0; i < texts; ++i) {
    if (i) json += ",";
    json += R"({"id":"t","type":"text","x":0,"y":0,"width":1,"height":1,"text":")" + body + "\"}";
  }
  json += "]}";
  Drawing d;
  ASSERT_EQ(excalidraw::parse(readerFor(json, 1024), d), Error::Ok);
  EXPECT_EQ(d.shapes.size(), texts);
  EXPECT_LE(d.text.size(), excalidraw::MAX_TEXT);
  EXPECT_EQ(d.shapes.back().textLength, 0);
  EXPECT_TRUE(d.truncated);
}

TEST(Excalidraw, HugeFilesBlobIsSkipped) {
  std::string blob(300 * 1024, 'A');
  const std::string json =
      R"({"type":"excalidraw","files":{"abc":{"mimeType":"image/png","id":"abc","dataURL":"data:image/png;base64,)" +
      blob + R"(","created":1}},"elements":[{"id":"r","type":"rectangle","x":1,"y":2,"width":3,"height":4}]})";
  Drawing d;
  ASSERT_EQ(excalidraw::parse(readerFor(json, 512), d), Error::Ok);
  ASSERT_EQ(d.shapes.size(), 1u);
  EXPECT_FLOAT_EQ(d.shapes[0].w, 3);
  EXPECT_TRUE(d.text.empty());
}

TEST(Excalidraw, ObsidianMarkdownWrapper) {
  const std::string md =
      "---\nexcalidraw-plugin: parsed\ntags: [excalidraw]\n---\n"
      "==⚠  Switch to EXCALIDRAW VIEW in the MORE OPTIONS menu of this document. ⚠==\n\n"
      "# Text Elements\nHello ^t1\n\n%%\n# Drawing\n```json\n"
      R"({"type":"excalidraw","elements":[{"id":"t1","type":"text","x":0,"y":0,"width":1,"height":1,"text":"Hello"}]})"
      "\n```\n%%";
  Drawing d;
  ASSERT_EQ(excalidraw::parse(readerFor(md), d), Error::Ok);
  ASSERT_EQ(d.shapes.size(), 1u);
  EXPECT_EQ(d.textOf(d.shapes[0]), "Hello");
}

TEST(Excalidraw, DamagedCompressedMarkdownIsReported) {
  const std::string md =
      "---\nexcalidraw-plugin: parsed\n---\n# Text Elements\n%%\n# Drawing\n```compressed-json\n"
      "N4KAkARALgngDgUwgLgAQBQjtBECGAzgLYCWANgIYBWAvgHQD2AVgA==\n```\n%%";
  Drawing d;
  // A cut stream decodes to the start of the JSON, which parses to an empty drawing.
  EXPECT_EQ(excalidraw::parse(readerFor(md), d), Error::Ok);
  EXPECT_TRUE(d.shapes.empty());
  // Text that is not lz-string base64 at all.
  const std::string bad =
      "# Drawing\n```compressed-json\nN4KAkARALg!!ngDgUwgLgAQ_-BQjtBECGAzgLYCWANgIYBWAvgHQD2AVgA==\n```\n";
  EXPECT_EQ(excalidraw::parse(readerFor(bad), d), Error::BadCompressed);
  EXPECT_TRUE(d.shapes.empty());
  // An empty block is no drawing at all.
  EXPECT_EQ(excalidraw::parse(readerFor("# Drawing\n```compressed-json\n\n```\n"), d), Error::NotJson);
}

TEST(Excalidraw, CompressedMarkdownParsesLikeItsPlainTwin) {
  const std::string head =
      "---\nexcalidraw-plugin: parsed\ntags: [excalidraw]\n---\n# Text Elements\nHello ^t1\n\n%%\n# Drawing\n";
  const std::string plain = head + "```json\n" + LZ_GREEK_DOC_PLAIN + "\n```\n%%";
  const std::string compressed = head + "```compressed-json\n" + LZ_GREEK_DOC_B64_LINES + "\n```\n%%";
  Drawing a, b;
  ASSERT_EQ(excalidraw::parse(readerFor(plain), a), Error::Ok);
  ASSERT_EQ(excalidraw::parse(readerFor(compressed), b), Error::Ok);
  ASSERT_EQ(a.shapes.size(), 4u);
  ASSERT_EQ(b.shapes.size(), a.shapes.size());
  EXPECT_EQ(b.skipped, a.skipped);
  EXPECT_EQ(b.text, a.text);
  EXPECT_EQ(b.points.size(), a.points.size());
  for (size_t i = 0; i < a.shapes.size(); ++i) {
    EXPECT_EQ(b.shapes[i].kind, a.shapes[i].kind);
    EXPECT_FLOAT_EQ(b.shapes[i].x, a.shapes[i].x);
    EXPECT_FLOAT_EQ(b.shapes[i].y, a.shapes[i].y);
    EXPECT_FLOAT_EQ(b.shapes[i].angle, a.shapes[i].angle);
    EXPECT_EQ(b.shapes[i].strokeStyle, a.shapes[i].strokeStyle);
    EXPECT_EQ(b.shapes[i].pointCount, a.shapes[i].pointCount);
    EXPECT_EQ(b.textOf(b.shapes[i]), a.textOf(a.shapes[i]));
  }
  EXPECT_EQ(b.textOf(b.shapes[2]), "Hello\nΓειά σου κόσμε 😀");
  EXPECT_FLOAT_EQ(b.minX, a.minX);
  EXPECT_FLOAT_EQ(b.maxY, a.maxY);

  // A large compressed drawing, read in SD-sized chunks.
  const std::string big = head + "```compressed-json\n" + LZ_BIG_DOC_B64_LINES + "\n```\n%%";
  Drawing c;
  ASSERT_EQ(excalidraw::parse(readerFor(big, 512), c), Error::Ok);
  EXPECT_EQ(c.shapes.size(), LZ_BIG_DOC_SHAPES);
}
