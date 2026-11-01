#include <gtest/gtest.h>

#include <cstring>

#include "Catalog.h"

using namespace euc;
using namespace euc::catalog;

TEST(Catalog, TableIsWellFormed) {
  size_t n = 0;
  const Model* m = models(n);
  ASSERT_GT(n, 20u);
  for (size_t i = 0; i < n; ++i) {
    EXPECT_NE(m[i].maker, nullptr);
    EXPECT_NE(m[i].name, nullptr);
    EXPECT_LT(static_cast<int>(m[i].brand), static_cast<int>(Brand::Count));
    EXPECT_GE(m[i].packNominalVolts, 60);
    EXPECT_LE(m[i].packNominalVolts, 160);
    // no duplicate maker+name
    for (size_t j = i + 1; j < n; ++j) {
      EXPECT_FALSE(std::strcmp(m[i].maker, m[j].maker) == 0 && std::strcmp(m[i].name, m[j].name) == 0)
          << m[i].maker << " " << m[i].name;
    }
  }
}

TEST(Catalog, MakersAreDistinctAndOrdered) {
  const char* out[32];
  const size_t n = makers(out, 32);
  ASSERT_GE(n, 7u);
  EXPECT_STREQ(out[0], "Begode");
  EXPECT_STREQ(out[1], "ExtremeBull");
  EXPECT_STREQ(out[2], "Veteran");
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      EXPECT_STRNE(out[i], out[j]);
    }
  }
  // capped
  const char* two[2];
  EXPECT_EQ(makers(two, 2), 2u);
}

TEST(Catalog, ModelsOfMaker) {
  const Model* out[32];
  const size_t n = modelsOf("Kingsong", out, 32);
  ASSERT_GE(n, 5u);
  for (size_t i = 0; i < n; ++i) {
    EXPECT_STREQ(out[i]->maker, "Kingsong");
    EXPECT_EQ(out[i]->brand, Brand::Kingsong);
  }
  EXPECT_EQ(modelsOf("Nobody", out, 32), 0u);
  EXPECT_EQ(modelsOf("Begode", out, 3), 3u);  // capped
}

TEST(Catalog, FindAndPackVolts) {
  const Model* s22 = find("Kingsong", "KS-S22");
  ASSERT_NE(s22, nullptr);
  EXPECT_EQ(s22->packNominalVolts, 126);  // WheelDash eucData.mc:143 KSwheels126v
  const Model* sherman = find("Veteran", "Sherman");
  ASSERT_NE(sherman, nullptr);
  EXPECT_EQ(sherman->packNominalVolts, 100);  // eucData.mc:111 "voltage > 100.2 -> 100 %"
  EXPECT_EQ(find("Veteran", "Sherman S")->brand, Brand::Veteran);
  EXPECT_EQ(find("InMotion", "V11")->brand, Brand::InmotionV2);
  EXPECT_EQ(find("InMotion", "V8")->brand, Brand::InmotionV1);
  EXPECT_EQ(find("Begode", "Nope"), nullptr);
}

TEST(Catalog, DetectByNamePrefix) {
  // SITE:737-740 prefixes
  EXPECT_EQ(detectBrand("GotWay_12345", nullptr).brand(), Brand::Begode);
  EXPECT_EQ(detectBrand("GW-EX20S", nullptr).brand(), Brand::Begode);
  EXPECT_EQ(detectBrand("KS-S22-0231", nullptr).brand(), Brand::Kingsong);
  EXPECT_EQ(detectBrand("KSN-S18", uuid::kFfe0).brand(), Brand::Kingsong);  // GarminEUCBleDelegate.mc:290
  EXPECT_EQ(detectBrand("LK1234", nullptr).brand(), Brand::Veteran);
  EXPECT_EQ(detectBrand("NF-Nosfet", nullptr).brand(), Brand::Veteran);
  EXPECT_EQ(detectBrand("V11-ABCD", nullptr).brand(), Brand::InmotionV2);
  EXPECT_EQ(detectBrand("V12 PRO", nullptr).brand(), Brand::InmotionV2);
  EXPECT_EQ(detectBrand("V14", nullptr).brand(), Brand::InmotionV2);
  EXPECT_EQ(detectBrand("Adventure", nullptr).brand(), Brand::InmotionV2);
  EXPECT_EQ(detectBrand("E20", nullptr).brand(), Brand::InmotionV2);
  EXPECT_EQ(detectBrand("VESC BLE", nullptr).brand(), Brand::Vesc);  // GarminEUCBleDelegate.mc:310
  const Detection d = detectBrand("ks-16x", uuid::kFfe0);            // case-insensitive
  EXPECT_EQ(d.confidence, Confidence::Name);
  EXPECT_EQ(d.count, 1);
  EXPECT_FALSE(d.ambiguous());
}

TEST(Catalog, NameWinsOverService) {
  // a Kingsong on FFE0 is Kingsong, not the 4-way FFE0 list
  const Detection d = detectBrand("KS-18L", uuid::kFfe0);
  EXPECT_EQ(d.confidence, Confidence::Name);
  EXPECT_EQ(d.brand(), Brand::Kingsong);
  EXPECT_EQ(d.count, 1);
}

TEST(Catalog, Ffe0AloneIsAmbiguous) {
  const Detection d = detectBrand("", uuid::kFfe0);
  EXPECT_EQ(d.confidence, Confidence::Service);
  EXPECT_TRUE(d.ambiguous());
  EXPECT_EQ(d.count, 4);
  EXPECT_EQ(d.candidates[0], Brand::Begode);
  EXPECT_EQ(d.candidates[1], Brand::Veteran);
  EXPECT_EQ(d.candidates[2], Brand::Kingsong);
  EXPECT_EQ(d.candidates[3], Brand::NinebotE);
  // upper case and 16-bit forms are accepted
  EXPECT_EQ(detectBrand(nullptr, "0000FFE0-0000-1000-8000-00805F9B34FB").count, 4);
  EXPECT_EQ(detectBrand(nullptr, "ffe0").count, 4);
  EXPECT_EQ(detectBrand(nullptr, "FFE0").count, 4);
}

TEST(Catalog, OtherServices) {
  const Detection fff0 = detectBrand("Unknown", uuid::kFff0);
  EXPECT_EQ(fff0.brand(), Brand::Kingsong);  // old Kingsong, eucPM.mc:82-90
  EXPECT_EQ(fff0.confidence, Confidence::Service);
  EXPECT_EQ(fff0.count, 1);
  EXPECT_EQ(detectBrand(nullptr, uuid::kFfe5).brand(), Brand::InmotionV1);
  const Detection nus = detectBrand("", uuid::kNus);
  EXPECT_TRUE(nus.ambiguous());
  EXPECT_EQ(nus.candidates[0], Brand::InmotionV2);
  EXPECT_EQ(nus.candidates[1], Brand::Vesc);
}

TEST(Catalog, NinebotNames) {
  const Detection s = detectBrand("Ninebot-ABC", uuid::kNus);
  EXPECT_EQ(s.candidates[0], Brand::NinebotS);
  EXPECT_EQ(s.candidates[1], Brand::NinebotZ);
  EXPECT_EQ(s.count, 2);
  const Detection e = detectBrand("Ninebot-ABC", uuid::kFfe0);
  EXPECT_EQ(e.brand(), Brand::NinebotE);
  EXPECT_EQ(e.count, 1);
}

TEST(Catalog, NothingMatched) {
  const Detection d = detectBrand("JBL Flip", nullptr);
  EXPECT_EQ(d.confidence, Confidence::None);
  EXPECT_EQ(d.count, 0);
  EXPECT_EQ(d.brand(), Brand::Count);
  EXPECT_EQ(detectBrand(nullptr, nullptr).count, 0);
  EXPECT_EQ(detectBrand("", "").count, 0);
  EXPECT_EQ(detectBrand("", "0000180f-0000-1000-8000-00805f9b34fb").count, 0);  // battery service
  EXPECT_EQ(detectBrand("", "ffe").count, 0);
  EXPECT_EQ(detectBrand("", "0000ffe0").count, 0);  // neither a short nor a full form
}
