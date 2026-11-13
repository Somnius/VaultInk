#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "DevLink.h"

namespace {

std::vector<std::string> feedAll(devlink::LineAssembler& assembler, const std::string& bytes) {
  std::vector<std::string> lines;
  for (const char c : bytes) {
    if (assembler.feed(c)) lines.emplace_back(assembler.line(), assembler.length());
  }
  return lines;
}

devlink::Parsed parse(const std::string& line) { return devlink::parse(line.data(), line.size()); }

std::string args(const devlink::Parsed& parsed) { return std::string(parsed.args, parsed.argsLength); }

}  // namespace

TEST(LineAssembler, ReturnsALineOnlyAtTheNewline) {
  devlink::LineAssembler assembler;
  EXPECT_TRUE(feedAll(assembler, "CMD:STAT").empty());
  EXPECT_EQ(assembler.length(), 0u);
  const auto lines = feedAll(assembler, "US\n");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "CMD:STATUS");
}

TEST(LineAssembler, SplitsSeveralLinesFedAtOnce) {
  devlink::LineAssembler assembler;
  const auto lines = feedAll(assembler, "one\ntwo\n\nthree\nrest");
  ASSERT_EQ(lines.size(), 4u);
  EXPECT_EQ(lines[0], "one");
  EXPECT_EQ(lines[1], "two");
  EXPECT_EQ(lines[2], "");
  EXPECT_EQ(lines[3], "three");
}

TEST(LineAssembler, DropsCarriageReturns) {
  devlink::LineAssembler assembler;
  const auto lines = feedAll(assembler, "CMD:PING\r\nCMD:HELP\r\n");
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], "CMD:PING");
  EXPECT_EQ(lines[1], "CMD:HELP");
}

TEST(LineAssembler, KeepsALineOfExactlyTheMaximum) {
  devlink::LineAssembler assembler;
  const std::string longest(devlink::LineAssembler::MAX_LINE, 'a');
  const auto lines = feedAll(assembler, longest + "\n");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], longest);
  EXPECT_EQ(assembler.dropped(), 0u);
}

TEST(LineAssembler, DropsAnOverlongLineAndReadsTheNextOne) {
  devlink::LineAssembler assembler;
  const std::string tooLong(devlink::LineAssembler::MAX_LINE + 1, 'a');
  const auto lines = feedAll(assembler, tooLong + "CMD:REBOOT\nCMD:PING\n");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "CMD:PING");
  EXPECT_EQ(assembler.dropped(), 1u);
}

TEST(LineAssembler, SurvivesALongRunOfBytesWithNoNewline) {
  devlink::LineAssembler assembler;
  EXPECT_TRUE(feedAll(assembler, std::string(100000, 'x')).empty());
  EXPECT_EQ(assembler.dropped(), 1u);
  const auto lines = feedAll(assembler, "\nok\n");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "ok");
}

TEST(LineAssembler, KeepsBinaryBytes) {
  devlink::LineAssembler assembler;
  const std::string bytes = std::string("a\0b\xff", 4) + "\n";
  const auto lines = feedAll(assembler, bytes);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], std::string("a\0b\xff", 4));
}

TEST(Parse, KnowsEveryCommand) {
  EXPECT_EQ(parse("CMD:SCREENSHOT").command, devlink::Command::Screenshot);
  EXPECT_EQ(parse("CMD:STATUS").command, devlink::Command::Status);
  EXPECT_EQ(parse("CMD:LOG 2").command, devlink::Command::Log);
  EXPECT_EQ(parse("CMD:REBOOT").command, devlink::Command::Reboot);
  EXPECT_EQ(parse("CMD:BOOTLOADER").command, devlink::Command::Bootloader);
  EXPECT_EQ(parse("CMD:PING").command, devlink::Command::Ping);
  EXPECT_EQ(parse("CMD:HELP").command, devlink::Command::Help);
}

TEST(Parse, EveryListedCommandParses) {
  const std::string list = devlink::commandList();
  size_t start = 0;
  int count = 0;
  while (start < list.size()) {
    size_t end = list.find(' ', start);
    if (end == std::string::npos) end = list.size();
    const auto parsed = parse("CMD:" + list.substr(start, end - start));
    EXPECT_NE(parsed.command, devlink::Command::Unknown) << list.substr(start, end - start);
    EXPECT_NE(parsed.command, devlink::Command::None);
    start = end + 1;
    ++count;
  }
  EXPECT_EQ(count, 7);
}

TEST(Parse, LinesThatAreNotCommands) {
  EXPECT_EQ(parse("").command, devlink::Command::None);
  EXPECT_EQ(parse("CMD").command, devlink::Command::None);
  EXPECT_EQ(parse("hello").command, devlink::Command::None);
  EXPECT_EQ(parse("cmd:STATUS").command, devlink::Command::None);
  EXPECT_EQ(parse(" CMD:STATUS").command, devlink::Command::None);
  EXPECT_EQ(devlink::parse(nullptr, 0).command, devlink::Command::None);
}

TEST(Parse, UnknownNames) {
  EXPECT_EQ(parse("CMD:").command, devlink::Command::Unknown);
  EXPECT_EQ(parse("CMD:   ").command, devlink::Command::Unknown);
  EXPECT_EQ(parse("CMD:FLASH /x.bin").command, devlink::Command::Unknown);
  EXPECT_EQ(parse("CMD:status").command, devlink::Command::Unknown);
  EXPECT_EQ(parse("CMD:STATUSES").command, devlink::Command::Unknown);
  EXPECT_EQ(parse("CMD:STAT").command, devlink::Command::Unknown);
}

TEST(Parse, TrimsBlanksAroundTheNameAsBefore) {
  const auto parsed = parse("CMD: SCREENSHOT \t\r");
  EXPECT_EQ(parsed.command, devlink::Command::Screenshot);
  EXPECT_EQ(parsed.argsLength, 0u);
}

TEST(Parse, ReturnsTheArguments) {
  // The arguments point into the line, so the lines outlive the results.
  const std::string log = "CMD:LOG   2  ";
  auto parsed = parse(log);
  EXPECT_EQ(parsed.command, devlink::Command::Log);
  EXPECT_EQ(args(parsed), "2");
  const std::string shot = "CMD:SCREENSHOT now please";
  parsed = parse(shot);
  EXPECT_EQ(parsed.command, devlink::Command::Screenshot);
  EXPECT_EQ(args(parsed), "now please");
  const std::string ping = "CMD:PING";
  parsed = parse(ping);
  EXPECT_EQ(args(parsed), "");
}

TEST(ParseUnsigned, AcceptsOnlyPlainNumbers) {
  uint32_t value = 99;
  EXPECT_TRUE(devlink::parseUnsigned("0", 1, value));
  EXPECT_EQ(value, 0u);
  EXPECT_TRUE(devlink::parseUnsigned("4294967295", 10, value));
  EXPECT_EQ(value, 4294967295u);
  EXPECT_TRUE(devlink::parseUnsigned("31", 1, value));
  EXPECT_EQ(value, 3u);
  value = 7;
  EXPECT_FALSE(devlink::parseUnsigned("4294967296", 10, value));
  EXPECT_FALSE(devlink::parseUnsigned("12345678901", 11, value));
  EXPECT_FALSE(devlink::parseUnsigned("", 0, value));
  EXPECT_FALSE(devlink::parseUnsigned("-1", 2, value));
  EXPECT_FALSE(devlink::parseUnsigned("2x", 2, value));
  EXPECT_FALSE(devlink::parseUnsigned(" 2", 2, value));
  EXPECT_FALSE(devlink::parseUnsigned(nullptr, 1, value));
  EXPECT_EQ(value, 7u);
}

TEST(JsonLine, EmptyObject) {
  devlink::JsonLine json;
  EXPECT_STREQ(json.finish(), "{}");
}

TEST(JsonLine, BuildsEveryType) {
  devlink::JsonLine json;
  json.addBool("ok", true).addString("version", "0.74.0").addInt("wake", -1).addUnsigned("heap", 4294967296ULL);
  json.addBool("light", false);
  const std::string text = json.finish();
  EXPECT_EQ(text, "{\"ok\":true,\"version\":\"0.74.0\",\"wake\":-1,\"heap\":4294967296,\"light\":false}");
  EXPECT_EQ(json.length(), text.size());
  EXPECT_FALSE(json.tooLong());
}

TEST(JsonLine, EscapesQuotesBackslashesAndControlCharacters) {
  devlink::JsonLine json;
  json.addString("a\"b", "q\" b\\ n\n r\r t\t bell\x07 esc\x1b del\x7f");
  EXPECT_STREQ(json.finish(), "{\"a\\\"b\":\"q\\\" b\\\\ n\\n r\\r t\\t bell\\u0007 esc\\u001b del\x7f\"}");
}

TEST(JsonLine, KeepsUtf8AndTreatsNullAsEmpty) {
  devlink::JsonLine json;
  json.addString("name", "Σημειώσεις").addString("none", nullptr);
  EXPECT_STREQ(json.finish(), "{\"name\":\"Σημειώσεις\",\"none\":\"\"}");
}

TEST(JsonLine, NeverHasARawLineBreak) {
  devlink::JsonLine json;
  json.addString("text", "one\ntwo\r\nthree");
  const std::string text = json.finish();
  EXPECT_EQ(text.find('\n'), std::string::npos);
  EXPECT_EQ(text.find('\r'), std::string::npos);
}

TEST(JsonLine, AReplyThatDoesNotFitBecomesAnError) {
  devlink::JsonLine json;
  json.addBool("ok", true);
  const std::string big(devlink::JsonLine::CAPACITY, 'x');
  json.addString("big", big.c_str());
  EXPECT_TRUE(json.tooLong());
  json.addInt("later", 1);
  EXPECT_STREQ(json.finish(), "{\"ok\":false,\"error\":\"reply too long\"}");
}

TEST(JsonLine, FillsToTheCapWithoutOverrunning) {
  // {"k":"<n x>"} is n + 8 bytes; the buffer holds CAPACITY - 1 of them plus the terminator.
  const size_t fits = devlink::JsonLine::CAPACITY - 1 - 8;
  {
    devlink::JsonLine json;
    const std::string value(fits, 'x');
    json.addString("k", value.c_str());
    EXPECT_FALSE(json.tooLong());
    const std::string text = json.finish();
    EXPECT_EQ(text.size(), devlink::JsonLine::CAPACITY - 1);
    EXPECT_EQ(text.back(), '}');
  }
  {
    devlink::JsonLine json;
    const std::string value(fits + 1, 'x');
    json.addString("k", value.c_str());
    EXPECT_TRUE(json.tooLong());
  }
}

TEST(JsonLine, AnEscapeCutAtTheCapIsLeftOutWhole) {
  devlink::JsonLine json;
  const std::string value(devlink::JsonLine::CAPACITY - 12, 'x');
  json.addString("k", (value + "\x01\x01").c_str());
  EXPECT_TRUE(json.tooLong());
  EXPECT_STREQ(json.finish(), "{\"ok\":false,\"error\":\"reply too long\"}");
}

TEST(DevLinkLines, AStaleFragmentIsForgottenAfterAPause) {
  devlink::LineAssembler lines;
  for (const char c : std::string("xx")) EXPECT_FALSE(lines.feed(c, 1000));
  bool done = false;
  for (const char c : std::string("CMD:PING\n")) done = lines.feed(c, 5000);  // 4 s later
  ASSERT_TRUE(done);
  EXPECT_EQ(std::string(lines.line(), lines.length()), "CMD:PING");
}

TEST(DevLinkLines, ABurstIsNotCutByThePause) {
  devlink::LineAssembler lines;
  bool done = false;
  uint32_t now = 100;
  for (const char c : std::string("CMD:PING\n")) done = lines.feed(c, now += 5);
  ASSERT_TRUE(done);
  EXPECT_EQ(std::string(lines.line(), lines.length()), "CMD:PING");
}

TEST(DevLinkLines, AWrappedClockStillGivesTheGap) {
  devlink::LineAssembler lines;
  EXPECT_FALSE(lines.feed('x', 0xFFFFFF00u));
  bool done = false;
  for (const char c : std::string("CMD:PING\n")) done = lines.feed(c, 0x00001000u);  // 4,352 ms later, wrapped
  ASSERT_TRUE(done);
  EXPECT_EQ(std::string(lines.line(), lines.length()), "CMD:PING");
}
