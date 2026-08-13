#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "neomifes/core/settings.h"

namespace fs = std::filesystem;

namespace {

using neomifes::core::Settings;

fs::path tempJsonPath() {
    return fs::temp_directory_path() / (std::string("nmfs_settings_") + std::to_string(std::rand()) + ".json");
}

void writeRaw(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

TEST(SettingsTest, LoadFromMissingFileUsesDefaults) {
    const Settings settings = Settings::loadFrom(tempJsonPath());
    EXPECT_EQ(settings, Settings{});
}

TEST(SettingsTest, LoadFromMalformedJsonUsesDefaults) {
    auto path = tempJsonPath();
    writeRaw(path, "{not valid json");
    const Settings settings = Settings::loadFrom(path);
    EXPECT_EQ(settings, Settings{});
    fs::remove(path);
}

TEST(SettingsTest, LoadFromWrongVersionUsesDefaults) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 2, "tabWidth": 8})");
    const Settings settings = Settings::loadFrom(path);
    EXPECT_EQ(settings, Settings{});
    fs::remove(path);
}

TEST(SettingsTest, LoadFromZeroTabWidthClampsToDefault) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "tabWidth": 0})");
    const Settings settings = Settings::loadFrom(path);
    EXPECT_EQ(settings.tabWidth, 4u);
    fs::remove(path);
}

TEST(SettingsTest, LoadFromExcessiveTabWidthClampsToDefault) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "tabWidth": 999})");
    const Settings settings = Settings::loadFrom(path);
    EXPECT_EQ(settings.tabWidth, 4u);
    fs::remove(path);
}

TEST(SettingsTest, LoadFromNonPositiveFontSizeClampsToDefault) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "fontSizeDips": 0})");
    const Settings settings = Settings::loadFrom(path);
    EXPECT_FLOAT_EQ(settings.fontSizeDips, 14.0F);
    fs::remove(path);
}

TEST(SettingsTest, LoadFromNegativeFontSizeClampsToDefault) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "fontSizeDips": -3.5})");
    const Settings settings = Settings::loadFrom(path);
    EXPECT_FLOAT_EQ(settings.fontSizeDips, 14.0F);
    fs::remove(path);
}

TEST(SettingsTest, LoadFromPartiallyPopulatedJsonKeepsOtherFieldsAtDefault) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "tabWidth": 2, "showMinimap": false})");
    const Settings settings = Settings::loadFrom(path);
    EXPECT_EQ(settings.tabWidth, 2u);
    EXPECT_FALSE(settings.showMinimap);
    EXPECT_EQ(settings.fontFamily, u"Consolas");     // untouched field stays at default
    EXPECT_TRUE(settings.showLineNumbers);           // untouched field stays at default
    fs::remove(path);
}

TEST(SettingsTest, SaveThenLoadRoundTripsAllFields) {
    Settings modified;
    modified.fontFamily              = u"メイリオ";
    modified.fontSizeDips            = 16.5F;
    modified.tabWidth                = 2;
    modified.insertSpacesForTab      = true;
    modified.showLineNumbers         = false;
    modified.showMinimap             = false;
    modified.autoSaveIntervalSeconds = 30;
    modified.themeName               = u"ライト";

    auto path = tempJsonPath();
    modified.saveTo(path);

    const Settings loaded = Settings::loadFrom(path);
    EXPECT_EQ(loaded, modified);

    fs::remove(path);
}

TEST(SettingsTest, SaveToNonExistentDirectoryFailsSilently) {
    const Settings settings;
    const fs::path unwritablePath =
        fs::temp_directory_path() / "nmfs_this_directory_does_not_exist" / "settings.json";
    // Should not throw, crash, or otherwise propagate the failure - saveTo()
    // is documented best-effort (same contract as SearchHistory::saveTo()).
    EXPECT_NO_THROW(settings.saveTo(unwritablePath));
    EXPECT_FALSE(fs::exists(unwritablePath));
}

}  // namespace
