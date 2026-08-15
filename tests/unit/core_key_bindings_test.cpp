#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "neomifes/core/key_bindings.h"

namespace fs = std::filesystem;

namespace {

using neomifes::core::KeyBindings;

fs::path tempJsonPath() {
    return fs::temp_directory_path() / (std::string("nmfs_keybindings_") + std::to_string(std::rand()) + ".json");
}

void writeRaw(const fs::path& path, std::string_view content) {
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

TEST(KeyBindingsTest, LoadFromMissingFileFallsBackToNeomifesPreset) {
    const KeyBindings bindings = KeyBindings::loadFrom(tempJsonPath());
    EXPECT_EQ(bindings, KeyBindings::forPreset(u"neomifes"));
}

TEST(KeyBindingsTest, LoadFromMalformedJsonFallsBackToNeomifesPreset) {
    auto path = tempJsonPath();
    writeRaw(path, "{not valid json");
    const KeyBindings bindings = KeyBindings::loadFrom(path);
    EXPECT_EQ(bindings, KeyBindings::forPreset(u"neomifes"));
    fs::remove(path);
}

TEST(KeyBindingsTest, LoadFromWrongVersionFallsBackToNeomifesPreset) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 2, "preset": "sakura", "bindings": {}})");
    const KeyBindings bindings = KeyBindings::loadFrom(path);
    EXPECT_EQ(bindings, KeyBindings::forPreset(u"neomifes"));
    fs::remove(path);
}

TEST(KeyBindingsTest, LoadFromMissingBindingsObjectFallsBackToNeomifesPreset) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "preset": "sakura"})");
    const KeyBindings bindings = KeyBindings::loadFrom(path);
    EXPECT_EQ(bindings, KeyBindings::forPreset(u"neomifes"));
    fs::remove(path);
}

TEST(KeyBindingsTest, LoadFromTolerateAStrayMalformedBindingEntry) {
    auto path = tempJsonPath();
    writeRaw(path, R"({"version": 1, "preset": "custom",
                        "bindings": {"file.save": ["Ctrl+S"], "find.show": "not-an-array"}})");
    const KeyBindings bindings = KeyBindings::loadFrom(path);
    EXPECT_EQ(bindings.chordsFor(u"file.save"), std::vector<std::u16string>{u"Ctrl+S"});
    EXPECT_TRUE(bindings.chordsFor(u"find.show").empty());
    fs::remove(path);
}

TEST(KeyBindingsTest, ForPresetNeomifesMatchesConfirmedSourceValues) {
    const KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    EXPECT_EQ(bindings.presetName, u"neomifes");
    EXPECT_EQ(bindings.chordsFor(u"file.save"), std::vector<std::u16string>{u"Ctrl+S"});
    EXPECT_EQ(bindings.chordsFor(u"file.new"), std::vector<std::u16string>{u"Ctrl+N"});
    EXPECT_EQ(bindings.chordsFor(u"find.show"), std::vector<std::u16string>{u"Ctrl+F"});
    EXPECT_EQ(bindings.chordsFor(u"find.next"), std::vector<std::u16string>{u"F3"});
    EXPECT_EQ(bindings.chordsFor(u"edit.toggleOverwriteMode"), std::vector<std::u16string>{u"Insert"});
    // tab.next has two chords (primary + alternate), matching
    // kAcceleratorTable's pre-WI-10 two-row-per-command precedent.
    const std::vector<std::u16string> expectedTabNext{u"Ctrl+Tab", u"Ctrl+PageDown"};
    EXPECT_EQ(bindings.chordsFor(u"tab.next"), expectedTabNext);
}

TEST(KeyBindingsTest, ForPresetSakuraCoversMostCommands) {
    const KeyBindings bindings = KeyBindings::forPreset(u"sakura");
    EXPECT_EQ(bindings.presetName, u"sakura");
    EXPECT_EQ(bindings.chordsFor(u"file.save"), std::vector<std::u16string>{u"Ctrl+S"});
    EXPECT_EQ(bindings.chordsFor(u"find.replace"), std::vector<std::u16string>{u"Ctrl+R"});
    EXPECT_EQ(bindings.chordsFor(u"search.grep.show"), std::vector<std::u16string>{u"Ctrl+G"});
    EXPECT_EQ(bindings.chordsFor(u"goto.line.show"), std::vector<std::u16string>{u"Ctrl+J"});
    // Sakura has no command-palette concept - deliberately unbound.
    EXPECT_TRUE(bindings.chordsFor(u"command.paletteShow").empty());
}

TEST(KeyBindingsTest, ForPresetHidemaruLeavesUnconfirmedCommandsUnbound) {
    const KeyBindings bindings = KeyBindings::forPreset(u"hidemaru");
    EXPECT_EQ(bindings.presetName, u"hidemaru");
    EXPECT_EQ(bindings.chordsFor(u"file.save"), std::vector<std::u16string>{u"Ctrl+S"});
    EXPECT_EQ(bindings.chordsFor(u"navigate.tagJump"), std::vector<std::u16string>{u"F10"});
    // Explicitly unconfirmed per build_plan.md's "don't guess" mandate -
    // must stay unbound, never silently backfilled from another preset.
    EXPECT_TRUE(bindings.chordsFor(u"file.saveAs").empty());
    EXPECT_TRUE(bindings.chordsFor(u"search.grep.show").empty());
    EXPECT_TRUE(bindings.chordsFor(u"find.next").empty());
    EXPECT_TRUE(bindings.chordsFor(u"bookmark.toggle").empty());
    EXPECT_TRUE(bindings.chordsFor(u"tab.next").empty());
}

TEST(KeyBindingsTest, ForPresetVscodeCoversMostCommands) {
    const KeyBindings bindings = KeyBindings::forPreset(u"vscode");
    EXPECT_EQ(bindings.presetName, u"vscode");
    EXPECT_EQ(bindings.chordsFor(u"tab.next"), std::vector<std::u16string>{u"Ctrl+PageDown"});
    EXPECT_EQ(bindings.chordsFor(u"navigate.tagJump"), std::vector<std::u16string>{u"F12"});
    const std::vector<std::u16string> expectedPalette{u"Ctrl+Shift+P", u"F1"};
    EXPECT_EQ(bindings.chordsFor(u"command.paletteShow"), expectedPalette);
    // VS Code has no built-in bookmark feature by default - unbound.
    EXPECT_TRUE(bindings.chordsFor(u"bookmark.toggle").empty());
}

TEST(KeyBindingsTest, ForPresetUnknownNameFallsBackToNeomifes) {
    const KeyBindings bindings = KeyBindings::forPreset(u"totally-unknown-preset");
    EXPECT_EQ(bindings, KeyBindings::forPreset(u"neomifes"));
}

TEST(KeyBindingsTest, ChordsForUnboundCommandReturnsEmpty) {
    const KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    EXPECT_TRUE(bindings.chordsFor(u"no.such.command").empty());
}

TEST(KeyBindingsTest, SetChordsOverridesAndCanUnbind) {
    KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    bindings.setChords(u"file.save", {u"Ctrl+Alt+S"});
    EXPECT_EQ(bindings.chordsFor(u"file.save"), std::vector<std::u16string>{u"Ctrl+Alt+S"});
    bindings.setChords(u"file.save", {});
    EXPECT_TRUE(bindings.chordsFor(u"file.save").empty());
}

TEST(KeyBindingsTest, SaveThenLoadRoundTripsAllBindingsAndPresetName) {
    KeyBindings modified = KeyBindings::forPreset(u"sakura");
    modified.setChords(u"file.save", {u"Ctrl+Alt+S"});
    modified.setChords(u"navigate.tagJump", {});  // explicitly unbind

    auto path = tempJsonPath();
    modified.saveTo(path);

    const KeyBindings loaded = KeyBindings::loadFrom(path);
    EXPECT_EQ(loaded, modified);
    EXPECT_EQ(loaded.presetName, u"sakura");

    fs::remove(path);
}

TEST(KeyBindingsTest, SaveToNonExistentDirectoryFailsSilently) {
    const KeyBindings bindings = KeyBindings::forPreset(u"neomifes");
    const fs::path unwritablePath =
        fs::temp_directory_path() / "nmfs_this_directory_does_not_exist" / "keybindings.json";
    EXPECT_NO_THROW(bindings.saveTo(unwritablePath));
    EXPECT_FALSE(fs::exists(unwritablePath));
}

}  // namespace
