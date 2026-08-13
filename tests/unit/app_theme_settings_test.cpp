#include <gtest/gtest.h>

#include "neomifes/app/theme_settings.h"

namespace {

using neomifes::app::parseThemeKind;
using neomifes::app::themeKindToSettingsString;
using neomifes::render::ThemeKind;

TEST(ThemeSettingsTest, ParseThemeKindRecognizesLight) {
    EXPECT_EQ(parseThemeKind(u"light"), ThemeKind::Light);
}

TEST(ThemeSettingsTest, ParseThemeKindRecognizesHighContrast) {
    EXPECT_EQ(parseThemeKind(u"high-contrast"), ThemeKind::HighContrast);
}

TEST(ThemeSettingsTest, ParseThemeKindRecognizesDark) {
    EXPECT_EQ(parseThemeKind(u"dark"), ThemeKind::Dark);
}

TEST(ThemeSettingsTest, ParseThemeKindFallsBackToDarkForEmptyString) {
    EXPECT_EQ(parseThemeKind(u""), ThemeKind::Dark);
}

TEST(ThemeSettingsTest, ParseThemeKindFallsBackToDarkForGarbageString) {
    // Simulates a hand-edited or stale settings.json - never a fatal error,
    // same "safe default" contract core::Settings::loadFrom() itself
    // follows for every other field.
    EXPECT_EQ(parseThemeKind(u"not-a-real-theme"), ThemeKind::Dark);
}

TEST(ThemeSettingsTest, ThemeKindToSettingsStringRoundTripsForEveryKind) {
    for (const ThemeKind kind : {ThemeKind::Dark, ThemeKind::Light, ThemeKind::HighContrast}) {
        EXPECT_EQ(parseThemeKind(themeKindToSettingsString(kind)), kind);
    }
}

TEST(ThemeSettingsTest, ThemeKindToSettingsStringProducesTheExactPersistedLiterals) {
    EXPECT_EQ(themeKindToSettingsString(ThemeKind::Dark), u"dark");
    EXPECT_EQ(themeKindToSettingsString(ThemeKind::Light), u"light");
    EXPECT_EQ(themeKindToSettingsString(ThemeKind::HighContrast), u"high-contrast");
}

}  // namespace
