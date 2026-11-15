// The Settings root list on the X4 Pro (480x800, touch): its last row (System) must take a tap
// without scrolling. Lays the screen out as SettingsListActivity::buildScreen does, with the
// firmware's UI font line heights and the Lyra metrics the VaultInk theme uses, through the real
// FreeInkApp, and asks the published touch zones.
#include <builtinFonts/ubuntu_10_regular.h>
#include <builtinFonts/ubuntu_12_regular.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

#include "activities/settings/SettingsListFit.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;  // UiListActivity::ACTION_ROW
constexpr int ROWS = 8;                  // Search + 7 sections
constexpr int SYSTEM_ROW = 7;

// Font slot 0 is the small UI font (Ubuntu 10), every other slot the body font (Ubuntu 12), as
// uiScaleSpec() binds them. Widths do not matter here: every label and subtitle is one line.
class FontTarget final : public fui::DrawTarget {
 public:
  fui::Size measureText(fui::FontId font, const char* text, fui::TextStyle) const override {
    return {static_cast<int16_t>(text ? 8 * static_cast<int>(strlen(text)) : 0), lineHeight(font)};
  }
  int16_t lineHeight(fui::FontId font) const override {
    return font == 0 ? ubuntu_10_regular.advanceY : ubuntu_12_regular.advanceY;
  }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};

using App = fui::FreeInkApp<32, 10>;  // UiAppHost::UiApp

struct Harness {
  bool fit = true;
  int16_t height = 800;
  fui::ListItem items[ROWS]{};
  fui::ListNav nav;
  int16_t sectionGapUsed = 0;

  Harness() {
    for (int i = 0; i < ROWS; ++i) {
      items[i].label = "Section";
      items[i].subtitle = "What is in it";
      items[i].actionValue = static_cast<int16_t>(i);
    }
    items[1].sectionHeading = "Reading";  // SettingsListActivity::rebuildRows
    items[4].sectionHeading = "Device";
  }

  static void build(App::ScreenType& screen, void* user) {
    auto& self = *static_cast<Harness*>(user);
    // LyraMetrics: topPadding 10 + headerHeight 84; buttonHintsHeight is 0 with touch
    // (UITheme::getMetrics); verticalSpacing 16.
    screen.setContentMarginFromScreen(fui::Insets{94, 0, 0, 0});
    screen.spacer(16);
    fui::ListProps props;
    props.items = self.items;
    props.count = ROWS;
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    props.subtitleText = screen.theme().smallText;
    const auto& theme = screen.theme();
    const int16_t textHeight = static_cast<int16_t>(screen.target().lineHeight(theme.bodyText.font) +
                                                    screen.target().lineHeight(theme.smallText.font));
    props.rowHeight = static_cast<int16_t>(std::max<int16_t>(textHeight, 32) + 2 * theme.listTouchRowPaddingY);
    // UiListActivity::syncListViewport
    props.toggleCheckbox = true;
    props.toggleWidth = 28;
    props.toggleHeight = 28;
    props.partialTrailingRow = true;
    screen.syncListViewport(self.nav, props, ROWS);
    if (self.fit) settingslist::fitSectionGap(screen, props, 2);
    self.sectionGapUsed = props.sectionGap;
    screen.list(props);
  }

  // The row a tap at (240, y) lands on, -1 for none.
  int rowAt(const int16_t y) {
    FontTarget target;
    fui::DeviceContext device;
    device.width = 480;
    device.height = height;
    device.hasTouch = true;
    App app(target, device);
    // uiThemeTokens() with LyraMetrics.
    fui::ThemeTokens tokens = fui::themeTokensForLineHeight(target.lineHeight(1), 0, 1, 1);
    tokens.listRowGap = 0;
    tokens.listRowRadius = 6;
    tokens.listInset = 20;
    tokens.listSidePadding = 8;
    tokens.listSelectionStyle = fui::SelectionStyle::LightPill;
    tokens.listScrollWidth = 4;
    tokens.listScrollSide = 0;
    tokens.listScrollInset = 0;
    tokens.headerHeight = 84;
    app.setTheme(tokens);
    app.setScreen(&Harness::build, this);
    nav.reset();
    app.render();
    fui::Interaction hit;
    return app.hitPublished(240, y, ACTION_ROW, hit) ? hit.value : -1;
  }
};
}  // namespace

TEST(SettingsListFit, FontsAreTheOnesTheNumbersWereWorkedOutWith) {
  EXPECT_EQ(ubuntu_10_regular.advanceY, 24);
  EXPECT_EQ(ubuntu_12_regular.advanceY, 29);
}

// The bug as shipped up to 0.75.1: System is drawn from y=735 to 804 on an 800 px screen, as a
// preview without a touch zone.
TEST(SettingsListFit, WithoutTheFitTheSystemRowTakesNoTap) {
  Harness h;
  h.fit = false;
  EXPECT_EQ(h.rowAt(770), -1);
  EXPECT_EQ(h.sectionGapUsed, 16);
  EXPECT_EQ(h.nav.drawnRows, SYSTEM_ROW);   // 7 of 8
  EXPECT_EQ(h.rowAt(700), SYSTEM_ROW - 1);  // Connectivity, the row above, does
}

TEST(SettingsListFit, SystemRowTakesATapWithoutScrolling) {
  Harness h;
  EXPECT_EQ(h.rowAt(770), SYSTEM_ROW);
  EXPECT_EQ(h.nav.drawnRows, ROWS);
  EXPECT_EQ(h.sectionGapUsed, 14);  // 4 px over, two headings
  EXPECT_EQ(h.rowAt(799), SYSTEM_ROW);
  EXPECT_EQ(h.rowAt(120), 0);  // Search, the first row
}

TEST(SettingsListFit, ARoomyScreenKeepsTheFullPadding) {
  Harness h;
  h.height = 900;
  EXPECT_EQ(h.rowAt(770), SYSTEM_ROW);
  EXPECT_EQ(h.sectionGapUsed, 16);
}

// 480 px high (a landscape panel): the rows cannot fit, so the padding stays and the list scrolls.
TEST(SettingsListFit, AShortScreenStillScrolls) {
  Harness h;
  h.height = 480;
  h.rowAt(200);
  EXPECT_EQ(h.sectionGapUsed, 16);
  EXPECT_LT(h.nav.drawnRows, ROWS);
}

TEST(SettingsListFit, FittedSectionGap) {
  using settingslist::fittedSectionGap;
  EXPECT_EQ(fittedSectionGap(694, 690, 2, 16), 14);
  EXPECT_EQ(fittedSectionGap(690, 690, 2, 16), 16);  // fits already
  EXPECT_EQ(fittedSectionGap(691, 690, 2, 16), 15);  // rounds the cut up
  EXPECT_EQ(fittedSectionGap(706, 690, 2, 16), 8);   // half the padding at most
  EXPECT_EQ(fittedSectionGap(707, 690, 2, 16), 16);  // more than that: leave it, the list scrolls
  EXPECT_EQ(fittedSectionGap(700, 690, 0, 16), 16);
  EXPECT_EQ(fittedSectionGap(700, 690, 2, 0), 0);
}
