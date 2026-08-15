#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>

#include <windows.h>

#include "neomifes/app/key_chord.h"

namespace {

using neomifes::app::KeyChord;
using neomifes::app::keyChordToString;
using neomifes::app::parseKeyChord;

TEST(KeyChordTest, ParsesSingleCharacterKeyWithNoModifiers) {
    const auto chord = parseKeyChord(u"F");
    ASSERT_TRUE(chord.has_value());
    EXPECT_FALSE(chord->ctrl);
    EXPECT_FALSE(chord->shift);
    EXPECT_FALSE(chord->alt);
    EXPECT_EQ(chord->virtualKey, static_cast<UINT>(u'F'));
}

TEST(KeyChordTest, ParsesCtrlModifier) {
    const auto chord = parseKeyChord(u"Ctrl+S");
    ASSERT_TRUE(chord.has_value());
    EXPECT_TRUE(chord->ctrl);
    EXPECT_FALSE(chord->shift);
    EXPECT_FALSE(chord->alt);
    EXPECT_EQ(chord->virtualKey, static_cast<UINT>(u'S'));
}

TEST(KeyChordTest, ParsesAllThreeModifiersInOrder) {
    const auto chord = parseKeyChord(u"Ctrl+Shift+Alt+P");
    ASSERT_TRUE(chord.has_value());
    EXPECT_TRUE(chord->ctrl);
    EXPECT_TRUE(chord->shift);
    EXPECT_TRUE(chord->alt);
    EXPECT_EQ(chord->virtualKey, static_cast<UINT>(u'P'));
}

TEST(KeyChordTest, ModifierNamesAreCaseInsensitive) {
    const auto chord = parseKeyChord(u"ctrl+SHIFT+p");
    ASSERT_TRUE(chord.has_value());
    EXPECT_TRUE(chord->ctrl);
    EXPECT_TRUE(chord->shift);
    EXPECT_EQ(chord->virtualKey, static_cast<UINT>(u'P'));
}

TEST(KeyChordTest, ParsesDigitKey) {
    const auto chord = parseKeyChord(u"Ctrl+1");
    ASSERT_TRUE(chord.has_value());
    EXPECT_EQ(chord->virtualKey, static_cast<UINT>(u'1'));
}

TEST(KeyChordTest, ParsesFunctionKey) {
    const auto chord = parseKeyChord(u"F3");
    ASSERT_TRUE(chord.has_value());
    EXPECT_EQ(chord->virtualKey, static_cast<UINT>(VK_F3));
}

TEST(KeyChordTest, ParsesShiftPlusFunctionKey) {
    const auto chord = parseKeyChord(u"Shift+F3");
    ASSERT_TRUE(chord.has_value());
    EXPECT_TRUE(chord->shift);
    EXPECT_EQ(chord->virtualKey, static_cast<UINT>(VK_F3));
}

TEST(KeyChordTest, ParsesNamedNonAlphanumericKeys) {
    EXPECT_EQ(parseKeyChord(u"Tab")->virtualKey, static_cast<UINT>(VK_TAB));
    EXPECT_EQ(parseKeyChord(u"Insert")->virtualKey, static_cast<UINT>(VK_INSERT));
    EXPECT_EQ(parseKeyChord(u"Ctrl+PageDown")->virtualKey, static_cast<UINT>(VK_NEXT));
    EXPECT_EQ(parseKeyChord(u"Ctrl+PageUp")->virtualKey, static_cast<UINT>(VK_PRIOR));
}

TEST(KeyChordTest, RejectsEmptyString) {
    EXPECT_FALSE(parseKeyChord(u"").has_value());
}

TEST(KeyChordTest, RejectsModifierOnlyString) {
    EXPECT_FALSE(parseKeyChord(u"Ctrl+Shift").has_value());
}

TEST(KeyChordTest, RejectsUnknownModifierToken) {
    EXPECT_FALSE(parseKeyChord(u"Super+S").has_value());
}

TEST(KeyChordTest, RejectsUnknownKeyName) {
    EXPECT_FALSE(parseKeyChord(u"Ctrl+NotAKey").has_value());
}

TEST(KeyChordTest, FormatUsesFixedCtrlShiftAltOrder) {
    const KeyChord chord{.ctrl = true, .shift = true, .alt = true, .virtualKey = static_cast<UINT>(u'P')};
    EXPECT_EQ(keyChordToString(chord), u"Ctrl+Shift+Alt+P");
}

TEST(KeyChordTest, FormatWithNoModifiers) {
    const KeyChord chord{.virtualKey = static_cast<UINT>(VK_F3)};
    EXPECT_EQ(keyChordToString(chord), u"F3");
}

TEST(KeyChordTest, FormatOfZeroVirtualKeyIsEmpty) {
    EXPECT_EQ(keyChordToString(KeyChord{}), u"");
}

TEST(KeyChordTest, RoundTripsExistingKeybindingLabelLiterals) {
    // Every one of these strings is an actual keybindingLabel literal
    // already hardcoded somewhere in normal_mode_wiring.cpp today - the
    // format this module reads/writes must match them exactly, not just be
    // internally consistent.
    const std::array<std::u16string_view, 23> literals{
        u"Ctrl+S",     u"Ctrl+Shift+S", u"Ctrl+F",       u"Ctrl+H",   u"F3",
        u"Shift+F3",   u"Ctrl+Shift+P", u"Ctrl+Shift+F", u"Ctrl+Shift+O", u"Ctrl+G",
        u"Ctrl+F2",    u"F2",           u"Shift+F2",     u"F12",      u"Ctrl+C",
        u"Ctrl+X",     u"Ctrl+V",       u"Ctrl+Z",       u"Ctrl+Y",   u"Insert",
        u"Ctrl+Tab",   u"Ctrl+Shift+Tab", u"Ctrl+W",
    };
    for (std::size_t i = 0; i < literals.size(); ++i) {
        const auto chord = parseKeyChord(literals.at(i));
        // MSVC's <ostream> deletes operator<<(ostream&, const char16_t*), so
        // a u16string_view can't be streamed directly into gtest's failure
        // message - report the index instead.
        ASSERT_TRUE(chord.has_value()) << "Failed to parse literal at index " << i;
        EXPECT_EQ(keyChordToString(*chord), literals.at(i));
    }
}

}  // namespace
