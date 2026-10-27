#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "Canvas.h"

namespace {

// Feeds `text` in small chunks, like an SD read loop.
canvas::Reader readerFor(const std::string& text, size_t chunk = 7) {
  auto pos = std::make_shared<size_t>(0);
  return [text, pos, chunk](char* buf, size_t cap) {
    const size_t n = std::min<size_t>({cap, chunk, text.size() - *pos});
    std::memcpy(buf, text.data() + *pos, n);
    *pos += n;
    return n;
  };
}

const std::string DEMO = R"({
  "nodes": [
    {"id":"g1","type":"group","x":-50,"y":-40,"width":500,"height":300,"label":"Ομάδα"},
    {"id":"t1","type":"text","x":0,"y":0,"width":200,"height":80,"color":"4",
     "text":"# Title\nLine \"two\" é 😀"},
    {"id":"f1","type":"file","x":250,"y":0,"width":150,"height":60,"file":"Notes/Anna.md","subpath":"#Intro"},
    {"id":"l1","type":"link","x":0,"y":120,"width":150,"height":40,"url":"https://example.com","color":"#ff0000"},
    {"id":"x1","type":"weird","x":10,"y":10,"width":1,"height":1,"extra":{"a":[1,2,{"b":"}"}]}}
  ],
  "edges": [
    {"id":"e1","fromNode":"t1","fromSide":"right","toNode":"f1","toSide":"left","label":"see"},
    {"id":"e2","fromNode":"t1","toNode":"l1","fromEnd":"arrow","toEnd":"none"},
    {"id":"e3","fromNode":"t1","toNode":"gone"}
  ],
  "other": true
})";

}  // namespace

TEST(Canvas, ParsesNodesAndEdges) {
  canvas::Canvas c;
  ASSERT_TRUE(canvas::parse(readerFor(DEMO), c));
  ASSERT_EQ(c.nodes.size(), 5u);
  EXPECT_EQ(c.nodes[0].type, canvas::NodeType::Group);
  EXPECT_EQ(c.nodes[0].label, "Ομάδα");
  const auto& t = c.nodes[1];
  EXPECT_EQ(t.type, canvas::NodeType::Text);
  EXPECT_EQ(t.color, 4);
  EXPECT_EQ(t.label, "# Title\nLine \"two\" \xc3\xa9 \xf0\x9f\x98\x80");
  // The raw text in the file round-trips through unescape.
  const std::string raw = DEMO.substr(t.textOffset, t.textLength);
  EXPECT_EQ(canvas::unescape(raw), t.label);
  EXPECT_EQ(c.nodes[2].type, canvas::NodeType::File);
  EXPECT_EQ(c.nodes[2].label, "Notes/Anna.md");
  EXPECT_EQ(c.nodes[2].subpath, "Intro");
  EXPECT_EQ(c.nodes[3].type, canvas::NodeType::Link);
  EXPECT_EQ(c.nodes[3].color, 7);
  EXPECT_EQ(c.nodes[4].type, canvas::NodeType::Unknown);
  ASSERT_EQ(c.edges.size(), 2u);  // e3 points at a missing node
  EXPECT_EQ(c.edges[0].from, 1);
  EXPECT_EQ(c.edges[0].to, 2);
  EXPECT_EQ(c.edges[0].fromSide, canvas::Side::Right);
  EXPECT_EQ(c.edges[0].toSide, canvas::Side::Left);
  EXPECT_TRUE(c.edges[0].toArrow);
  EXPECT_FALSE(c.edges[0].fromArrow);
  EXPECT_EQ(c.edges[0].label, "see");
  EXPECT_TRUE(c.edges[1].fromArrow);
  EXPECT_FALSE(c.edges[1].toArrow);
  EXPECT_EQ(c.minX, -50);
  EXPECT_EQ(c.minY, -40);
  EXPECT_EQ(c.maxX, 450);
  EXPECT_EQ(c.maxY, 260);
}

TEST(Canvas, LongTextStaysInTheFile) {
  std::string body(5000, 'a');
  body += "\\n\\u03b1";  // escaped newline and Greek alpha at the end
  const std::string json =
      R"({"nodes":[{"id":"t","type":"text","x":0,"y":0,"width":9,"height":9,"text":")" + body + R"("}],"edges":[]})";
  canvas::Canvas c;
  ASSERT_TRUE(canvas::parse(readerFor(json, 64), c));
  ASSERT_EQ(c.nodes.size(), 1u);
  EXPECT_LE(c.nodes[0].label.size(), canvas::LABEL_CHARS);
  EXPECT_EQ(c.nodes[0].textLength, body.size());
  const std::string full = canvas::unescape(json.substr(c.nodes[0].textOffset, c.nodes[0].textLength));
  EXPECT_EQ(full.size(), 5000u + 1 + 2);
  EXPECT_EQ(full.substr(5000), "\n\xce\xb1");
}

TEST(Canvas, LabelsCutAtCharacterBoundary) {
  std::string greek;
  for (int i = 0; i < 200; ++i) greek += "\xce\xb1";  // 400 bytes of α
  const std::string json =
      R"({"nodes":[{"id":"t","type":"text","x":0,"y":0,"width":9,"height":9,"text":")" + greek + R"("}]})";
  canvas::Canvas c;
  ASSERT_TRUE(canvas::parse(readerFor(json), c));
  ASSERT_EQ(c.nodes.size(), 1u);
  EXPECT_EQ(c.nodes[0].label.size() % 2, 0u);
  EXPECT_LE(c.nodes[0].label.size(), canvas::LABEL_CHARS);
}

TEST(Canvas, BrokenFilesKeepWhatWasRead) {
  canvas::Canvas c;
  EXPECT_FALSE(canvas::parse(readerFor("[1,2]"), c));
  EXPECT_FALSE(canvas::parse(readerFor(""), c));
  ASSERT_TRUE(canvas::parse(readerFor(DEMO.substr(0, DEMO.find("\"f1\""))), c));
  EXPECT_EQ(c.nodes.size(), 2u);
  EXPECT_TRUE(c.edges.empty());
}

TEST(Canvas, NodeLimit) {
  std::string json = R"({"nodes":[)";
  for (size_t i = 0; i < canvas::MAX_NODES + 5; ++i) {
    if (i) json += ',';
    json += R"({"id":"n)" + std::to_string(i) + R"(","type":"text","x":0,"y":0,"width":1,"height":1,"text":"x"})";
  }
  json += "]}";
  canvas::Canvas c;
  ASSERT_TRUE(canvas::parse(readerFor(json, 512), c));
  EXPECT_EQ(c.nodes.size(), canvas::MAX_NODES);
  EXPECT_TRUE(c.truncated);
}

TEST(Canvas, ReviewEdgeCases) {
  // A lone high surrogate before an escaped quote must not swallow it; long paths stay whole.
  const std::string path = "Folder/" + std::string(300, 'p') + ".md";
  const std::string json =
      R"({"nodes":[}, {"id":"t","type":"text","x":0,"y":0,"width":9,"height":9,"text":"a\ud800\"b"},)"
      R"({"id":"f","type":"file","x":0,"y":0,"width":9,"height":9,"file":")" +
      path + R"("}],"edges":[]})";
  canvas::Canvas c;
  ASSERT_TRUE(canvas::parse(readerFor(json), c));
  ASSERT_EQ(c.nodes.size(), 2u);
  EXPECT_EQ(c.nodes[0].label, "a\xef\xbf\xbd\"b");
  EXPECT_EQ(canvas::unescape(json.substr(c.nodes[0].textOffset, c.nodes[0].textLength)), "a\xef\xbf\xbd\"b");
  EXPECT_EQ(c.nodes[1].label, path);
}
