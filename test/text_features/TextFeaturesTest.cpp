#include <TextFeatures.h>
#include <gtest/gtest.h>

#include "ReaderRenderSpec.h"

TEST(TextFeatures, DefaultsAreOnExceptHanging) {
  textfeatures::set(textfeatures::DEFAULTS);
  EXPECT_TRUE(textfeatures::has(textfeatures::LIGATURES));
  EXPECT_TRUE(textfeatures::has(textfeatures::KERNING));
  EXPECT_TRUE(textfeatures::has(textfeatures::WIDOW_ORPHAN));
  EXPECT_TRUE(textfeatures::has(textfeatures::KEEP_HEADINGS));
  EXPECT_FALSE(textfeatures::has(textfeatures::HANGING));
}

TEST(TextFeatures, SetReplacesTheWholeMask) {
  textfeatures::set(textfeatures::HANGING | textfeatures::KERNING);
  EXPECT_FALSE(textfeatures::has(textfeatures::LIGATURES));
  EXPECT_TRUE(textfeatures::has(textfeatures::KERNING));
  EXPECT_TRUE(textfeatures::has(textfeatures::HANGING));
  textfeatures::set(textfeatures::DEFAULTS);
}

TEST(TextFeatures, EachFeatureChangesTheLayoutSignature) {
  ReaderRenderSpec base;
  const uint32_t baseSignature = readerRenderSpecSignature(base);
  for (const uint8_t flag : {textfeatures::LIGATURES, textfeatures::KERNING, textfeatures::WIDOW_ORPHAN,
                             textfeatures::KEEP_HEADINGS, textfeatures::HANGING}) {
    ReaderRenderSpec changed = base;
    changed.textFeatures = static_cast<uint8_t>(base.textFeatures ^ flag);
    EXPECT_NE(readerRenderSpecSignature(changed), baseSignature) << "flag " << static_cast<int>(flag);
  }
}
