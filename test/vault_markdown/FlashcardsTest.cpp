#include <gtest/gtest.h>

#include <string>

#include "Flashcards.h"

using flashcards::Card;
using flashcards::parseCards;

TEST(Flashcards, SingleLineCard) {
  const auto cards = parseCards("# Title\n\nCapital of Greece::Athens\nplain text\n");
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards[0].front, "Capital of Greece");
  EXPECT_EQ(cards[0].back, "Athens");
  EXPECT_EQ(cards[0].line, 3u);
}

TEST(Flashcards, ReversibleGivesTwo) {
  const auto cards = parseCards("cat:::γάτα\n");
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0].front, "cat");
  EXPECT_EQ(cards[0].back, "γάτα");
  EXPECT_EQ(cards[1].front, "γάτα");
  EXPECT_EQ(cards[1].back, "cat");
}

TEST(Flashcards, ListMarkersAndScheduleCommentsAreNotText) {
  const auto cards = parseCards("- Q1::A1 <!--SR:!2026-11-01,4,270-->\n1. Q2::A2\n<!--SR:!2026-11-01,4,270-->\n");
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0].front, "Q1");
  EXPECT_EQ(cards[0].back, "A1");
  EXPECT_EQ(cards[1].front, "Q2");
}

TEST(Flashcards, MultiLine) {
  const auto cards = parseCards("What are\nthe primes?\n?\n2, 3, 5\nand so on\n\nnext paragraph\n");
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards[0].front, "What are\nthe primes?");
  EXPECT_EQ(cards[0].back, "2, 3, 5\nand so on");
  EXPECT_EQ(cards[0].line, 1u);
}

TEST(Flashcards, MultiLineReversible) {
  const auto cards = parseCards("front\n??\nback\n");
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[1].front, "back");
}

TEST(Flashcards, QuestionMarkAloneNeedsBothSides) {
  EXPECT_TRUE(parseCards("?\nanswer\n").empty());
  EXPECT_TRUE(parseCards("question\n?\n").empty());
}

TEST(Flashcards, CurlyClozes) {
  const auto cards = parseCards("The capital of {{Greece}} is {{Athens}}.\n");
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0].front, "The capital of [...] is Athens.");
  EXPECT_EQ(cards[0].back, "The capital of Greece is Athens.");
  EXPECT_EQ(cards[1].front, "The capital of Greece is [...].");
}

TEST(Flashcards, BoldIsAClozeOnlyWithFrontmatter) {
  EXPECT_TRUE(parseCards("Some **bold** text\n").empty());
  const auto cards = parseCards("---\nsr-cloze: bold\n---\nSome **bold** text\n");
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards[0].front, "Some [...] text");
  EXPECT_EQ(cards[0].line, 4u);
}

TEST(Flashcards, HighlightIsNeverACloze) { EXPECT_TRUE(parseCards("a ==highlighted== word\n").empty()); }

TEST(Flashcards, FencesFrontmatterAndCodeSpansAreSkipped) {
  const auto cards = parseCards("---\nkey::value\n---\n```\na::b\n```\nuse `std::string` here\nreal::card\n");
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards[0].front, "real");
}

TEST(Flashcards, EmptySidesAndLongFieldsAreDropped) {
  EXPECT_TRUE(parseCards("::answer\nquestion::\n").empty());
  const std::string big(flashcards::MAX_FIELD_BYTES + 1, 'x');
  EXPECT_TRUE(parseCards(big + "::a\n").empty());
}

TEST(Flashcards, CapsCardsPerNote) {
  std::string text;
  for (size_t i = 0; i < flashcards::MAX_CARDS_PER_NOTE + 20; ++i) text += "q" + std::to_string(i) + "::a\n";
  EXPECT_EQ(parseCards(text).size(), flashcards::MAX_CARDS_PER_NOTE);
}

TEST(Flashcards, CrLfAndNoFinalNewline) {
  const auto cards = parseCards("a::b\r\nc::d");
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0].back, "b");
  EXPECT_EQ(cards[1].back, "d");
}

TEST(Flashcards, IdIgnoresAccentsCaseAndSpacingButNotEdits) {
  const Card a{"Καλημέρα  κόσμε", "hello", 1};
  const Card b{"καλημερα κοσμε", "HELLO", 9};
  EXPECT_EQ(flashcards::cardId("Note.md", a), flashcards::cardId("Note.md", b));
  const Card edited{"Καλημέρα κόσμε", "hi", 1};
  EXPECT_NE(flashcards::cardId("Note.md", a), flashcards::cardId("Note.md", edited));
  EXPECT_NE(flashcards::cardId("Note.md", a), flashcards::cardId("Other.md", a));
  EXPECT_EQ(flashcards::idToHex(0xabc).size(), 16u);
  EXPECT_EQ(flashcards::idToHex(0xabc), "0000000000000abc");
}
