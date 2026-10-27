#include <gtest/gtest.h>

#include <string>

#include "LineBuffer.h"

TEST(LineBuffer, RoundTripsExactBytes) {
  for (const std::string text : {"", "a", "a\n", "a\nb", "a\r\nb\r\n", "\xEF\xBB\xBF# T\nx\r\ny", "\n\n", "\tx\n"}) {
    LineBuffer b;
    ASSERT_TRUE(b.load(text));
    EXPECT_EQ(b.serialize(), text);
    EXPECT_FALSE(b.changed());
  }
}

TEST(LineBuffer, EditsKeepEndings) {
  LineBuffer b;
  b.load("one\r\ntwo\r\nthree");
  ASSERT_TRUE(b.replace(1, "TWO"));
  ASSERT_TRUE(b.insert(3, "four"));  // after a last line with no ending
  EXPECT_EQ(b.serialize(), "one\r\nTWO\r\nthree\r\nfour");
  ASSERT_TRUE(b.insert(0, "zero"));
  EXPECT_EQ(b.serialize(), "zero\r\none\r\nTWO\r\nthree\r\nfour");
  ASSERT_TRUE(b.remove(4));  // the last line again: "three" ends the file bare
  EXPECT_EQ(b.serialize(), "zero\r\none\r\nTWO\r\nthree");
}

TEST(LineBuffer, UndoRestoresEverything) {
  const std::string text = "a\nb\r\nc";
  LineBuffer b;
  b.load(text);
  b.replace(0, "A");
  b.insert(3, "d");
  b.moveUp(1);
  b.remove(3);
  b.remove(0);
  b.moveDown(0);
  EXPECT_TRUE(b.changed());
  while (b.undo()) {
  }
  EXPECT_EQ(b.serialize(), text);
  EXPECT_FALSE(b.changed());
}

TEST(LineBuffer, Limits) {
  LineBuffer b;
  EXPECT_FALSE(b.load("x\n" + std::string(LineBuffer::MAX_LINE_BYTES + 1, 'y') + "\n"));
  b.load("a\nb\n");
  EXPECT_FALSE(b.replace(5, "x"));
  EXPECT_FALSE(b.replace(0, "two\nlines"));
  EXPECT_FALSE(b.replace(0, std::string(LineBuffer::MAX_LINE_BYTES + 1, 'z')));
  EXPECT_FALSE(b.moveUp(0));
  EXPECT_FALSE(b.moveDown(1));
  EXPECT_EQ(b.undoDepth(), 0u);
  for (int i = 0; i < 30; ++i) b.replace(0, std::to_string(i));
  EXPECT_EQ(b.undoDepth(), LineBuffer::MAX_UNDO);
  while (b.undo()) {
  }
  EXPECT_TRUE(b.changed());  // the oldest edits fell off the list
}

TEST(LineBuffer, EmptyNote) {
  LineBuffer b;
  b.load("");
  EXPECT_EQ(b.lineCount(), 0u);
  ASSERT_TRUE(b.insert(0, "first"));
  EXPECT_EQ(b.serialize(), "first\n");
  b.undo();
  EXPECT_EQ(b.serialize(), "");
}
