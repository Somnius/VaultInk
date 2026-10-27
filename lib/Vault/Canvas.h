#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// JSON Canvas 1.0 (Obsidian .canvas files), read only. The file is streamed once: geometry, ids and
// short labels are kept, a text card's full Markdown stays in the file (offset and length of the
// raw JSON string, read again with readText when the card is opened).
namespace canvas {

enum class NodeType : uint8_t { Text, File, Link, Group, Unknown };
enum class Side : uint8_t { None, Top, Right, Bottom, Left };

struct Node {
  std::string id;
  NodeType type = NodeType::Unknown;
  int32_t x = 0, y = 0, width = 0, height = 0;
  uint8_t color = 0;        // 0 none, 1-6 Obsidian presets, 7 any "#hex"
  std::string label;        // group label, file path, URL (up to MAX_FIELD), or the start of a text card
  std::string subpath;      // file node "#heading" part, without '#'
  uint32_t textOffset = 0;  // text card: the raw JSON string in the file, without the quotes
  uint32_t textLength = 0;
};

struct Edge {
  uint16_t from = 0, to = 0;  // node indexes
  Side fromSide = Side::None, toSide = Side::None;
  bool fromArrow = false, toArrow = true;  // JSON Canvas defaults: fromEnd "none", toEnd "arrow"
  uint8_t color = 0;
  std::string label;
};

struct Canvas {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  bool truncated = false;  // limits reached; the rest of the file was skipped
  // Bounding box of every node, for fit-to-screen.
  int32_t minX = 0, minY = 0, maxX = 0, maxY = 0;
};

constexpr size_t MAX_NODES = 2000;
constexpr size_t MAX_EDGES = 4000;
constexpr size_t LABEL_CHARS = 160;  // bytes kept of a text card; longer text is cut at a UTF-8 boundary
constexpr size_t MAX_FIELD = 1024;   // ids, paths, URLs and labels

// Pulls up to `cap` bytes into `buf`; returns 0 at the end of the file.
using Reader = std::function<size_t(char* buf, size_t cap)>;

// Parses a whole canvas. False when the file is not a JSON object; a damaged tail keeps what was
// read. Edges whose ends name no node are dropped.
bool parse(const Reader& read, Canvas& out);

// Unescapes a raw JSON string body (what textOffset/textLength point at) into UTF-8.
std::string unescape(const std::string& raw);

}  // namespace canvas
