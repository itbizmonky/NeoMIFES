#include "neomifes/app/status_bar_format.h"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>

namespace neomifes::app {
namespace {

TEST(StatusBarFormatTest, PositionConvertsZeroBasedToOneBased) {
    EXPECT_EQ(formatStatusBarPosition(0, 0), L"1:1");
    EXPECT_EQ(formatStatusBarPosition(11, 4), L"12:5");
}

TEST(StatusBarFormatTest, SelectionCountIsEmptyWhenZero) {
    EXPECT_EQ(formatStatusBarSelectionCount(0), L"");
}

TEST(StatusBarFormatTest, SelectionCountIsNonEmptyWhenPositive) {
    EXPECT_EQ(formatStatusBarSelectionCount(5), L"5 selected");
}

TEST(StatusBarFormatTest, EncodingNamesAreDistinctForEveryEnumerator) {
    const std::array<encoding::Encoding, 13> all = {
        encoding::Encoding::Utf8,     encoding::Encoding::Utf8Bom,    encoding::Encoding::Utf16Le,
        encoding::Encoding::Utf16LeBom, encoding::Encoding::Utf16Be,  encoding::Encoding::Utf16BeBom,
        encoding::Encoding::Utf32Le,  encoding::Encoding::Utf32LeBom, encoding::Encoding::Utf32Be,
        encoding::Encoding::Utf32BeBom, encoding::Encoding::ShiftJis, encoding::Encoding::EucJp,
        encoding::Encoding::Iso2022Jp,
    };
    std::set<std::wstring> seen;
    for (const auto value : all) {
        const std::wstring name = formatStatusBarEncoding(value);
        EXPECT_FALSE(name.empty());
        EXPECT_TRUE(seen.insert(name).second) << "duplicate encoding name";
    }
}

TEST(StatusBarFormatTest, LineEndingNames) {
    EXPECT_EQ(formatStatusBarLineEnding(encoding::LineEnding::Crlf), L"CRLF");
    EXPECT_EQ(formatStatusBarLineEnding(encoding::LineEnding::Lf), L"LF");
    EXPECT_EQ(formatStatusBarLineEnding(encoding::LineEnding::Cr), L"CR");
    EXPECT_EQ(formatStatusBarLineEnding(encoding::LineEnding::Mixed), L"Mixed");
}

TEST(StatusBarFormatTest, OverwriteModeNames) {
    EXPECT_EQ(formatStatusBarOverwriteMode(true), L"OVR");
    EXPECT_EQ(formatStatusBarOverwriteMode(false), L"INS");
}

TEST(StatusBarFormatTest, LanguageIsEmptyWhenNullopt) {
    EXPECT_EQ(formatStatusBarLanguage(std::nullopt), L"");
}

TEST(StatusBarFormatTest, LanguageNamesAreDistinctForEveryEnumerator) {
    const std::array<syntax::Language, 22> all = {
        syntax::Language::Cpp,   syntax::Language::Python,   syntax::Language::C,
        syntax::Language::JavaScript, syntax::Language::Java, syntax::Language::Go,
        syntax::Language::Rust,  syntax::Language::Json,     syntax::Language::Html,
        syntax::Language::Css,   syntax::Language::Shell,    syntax::Language::Yaml,
        syntax::Language::Toml,  syntax::Language::Xml,      syntax::Language::TypeScript,
        syntax::Language::Tsx,   syntax::Language::Php,      syntax::Language::Markdown,
        syntax::Language::PowerShell, syntax::Language::Ini, syntax::Language::Batch,
        syntax::Language::Sql,
    };
    std::set<std::wstring> seen;
    for (const auto value : all) {
        const std::wstring name = formatStatusBarLanguage(value);
        EXPECT_FALSE(name.empty());
        EXPECT_TRUE(seen.insert(name).second) << "duplicate language name";
    }
}

}  // namespace
}  // namespace neomifes::app
