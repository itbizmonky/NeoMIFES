#include <gtest/gtest.h>

#include "neomifes/render/theme.h"

namespace {

using neomifes::render::Theme;
using neomifes::render::ThemeKind;
using neomifes::render::themeForKind;

// D2D1_COLOR_F has no operator== - compare components directly.
[[nodiscard]] bool colorsEqual(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

TEST(ThemeTest, DarkBackgroundMatchesThePreWi09HardcodedLiteral) {
    // The exact constant this replaces (render_pipeline.cpp's old
    // kBackgroundColor, RGB 30,30,30) - a dark-theme visual regression is
    // unacceptable, so this is pinned byte-for-byte.
    const D2D1_COLOR_F expected = {30.0F / 255.0F, 30.0F / 255.0F, 30.0F / 255.0F, 1.0F};
    EXPECT_TRUE(colorsEqual(themeForKind(ThemeKind::Dark).background, expected));
}

TEST(ThemeTest, DarkTextMatchesThePreWi09HardcodedLiteral) {
    const D2D1_COLOR_F expected = {220.0F / 255.0F, 220.0F / 255.0F, 220.0F / 255.0F, 1.0F};
    EXPECT_TRUE(colorsEqual(themeForKind(ThemeKind::Dark).text, expected));
}

TEST(ThemeTest, LightDiffersFromDarkOnBackgroundAndText) {
    const Theme& dark  = themeForKind(ThemeKind::Dark);
    const Theme& light = themeForKind(ThemeKind::Light);
    EXPECT_FALSE(colorsEqual(dark.background, light.background));
    EXPECT_FALSE(colorsEqual(dark.text, light.text));
}

TEST(ThemeTest, HighContrastDiffersFromDarkOnBackgroundAndText) {
    const Theme& dark = themeForKind(ThemeKind::Dark);
    const Theme& hc   = themeForKind(ThemeKind::HighContrast);
    EXPECT_FALSE(colorsEqual(dark.background, hc.background));
    EXPECT_FALSE(colorsEqual(dark.text, hc.text));
}

TEST(ThemeTest, HighContrastDiffersFromLightOnBackgroundAndText) {
    const Theme& light = themeForKind(ThemeKind::Light);
    const Theme& hc    = themeForKind(ThemeKind::HighContrast);
    EXPECT_FALSE(colorsEqual(light.background, hc.background));
    EXPECT_FALSE(colorsEqual(light.text, hc.text));
}

TEST(ThemeTest, HighContrastBackgroundIsPureBlackAndTextIsPureWhite) {
    const Theme& hc = themeForKind(ThemeKind::HighContrast);
    EXPECT_TRUE(colorsEqual(hc.background, D2D1_COLOR_F{0.0F, 0.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(colorsEqual(hc.text, D2D1_COLOR_F{1.0F, 1.0F, 1.0F, 1.0F}));
}

TEST(ThemeTest, EachThemeKeepsSelectionMatchAndCurrentMatchDistinctFromOneAnother) {
    for (const ThemeKind kind : {ThemeKind::Dark, ThemeKind::Light, ThemeKind::HighContrast}) {
        const Theme& theme = themeForKind(kind);
        EXPECT_FALSE(colorsEqual(theme.selection, theme.match));
        EXPECT_FALSE(colorsEqual(theme.match, theme.currentMatch));
        EXPECT_FALSE(colorsEqual(theme.selection, theme.currentMatch));
    }
}

// WI-14c: logError/logWarning must each read as distinct from plain body
// text and from each other in every theme, or log-mode color-coding would
// be indistinguishable from unhighlighted lines / from one another.
TEST(ThemeTest, EachThemeKeepsLogErrorAndLogWarningDistinctFromTextAndEachOther) {
    for (const ThemeKind kind : {ThemeKind::Dark, ThemeKind::Light, ThemeKind::HighContrast}) {
        const Theme& theme = themeForKind(kind);
        EXPECT_FALSE(colorsEqual(theme.logError, theme.text));
        EXPECT_FALSE(colorsEqual(theme.logWarning, theme.text));
        EXPECT_FALSE(colorsEqual(theme.logError, theme.logWarning));
    }
}

}  // namespace
