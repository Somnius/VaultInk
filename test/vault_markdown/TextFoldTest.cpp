#include <gtest/gtest.h>

#include "TextFold.h"

TEST(TextFold, LatinAndCase) { EXPECT_EQ(textfold::fold("Café ÉLAN Über"), "cafe elan uber"); }

TEST(TextFold, GreekMonotonic) {
  EXPECT_EQ(textfold::fold("Καλημέρα"), "καλημερα");
  EXPECT_EQ(textfold::fold("ΚΑΛΗΜΕΡΑ"), "καλημερα");
  EXPECT_EQ(textfold::fold("προϋπόθεση"), "προυποθεση");
  EXPECT_EQ(textfold::fold("λόγος"), "λογοσ");  // final sigma folds to sigma
}

TEST(TextFold, GreekPolytonic) {
  EXPECT_EQ(textfold::fold("ἄνθρωπος"), "ανθρωποσ");
  EXPECT_EQ(textfold::fold("ᾠδή Ἡ"), "ωδη η");
}

TEST(TextFold, CombiningAccentsAreDropped) {
  EXPECT_EQ(textfold::fold("e\xCC\x81"
                           "cole"),
            "ecole");  // e + U+0301
}

TEST(TextFold, Cyrillic) { EXPECT_EQ(textfold::fold("Привет"), "привет"); }

TEST(TextFold, Words) {
  const auto w = textfold::words("Καλημέρα, κόσμε! The cat's 2 a; ·τέλος· x-ray");
  const std::vector<std::string> expected{"καλημερα", "κοσμε", "the", "cat", "τελοσ", "ray"};
  EXPECT_EQ(w, expected);
}
