#include <gtest/gtest.h>

#include "neomifes/ui/main_window.h"

namespace {

using neomifes::ui::formatWindowTitle;

TEST(MainWindowTest, FormatWindowTitleWithFilenameNotDirty) {
    EXPECT_EQ(formatWindowTitle(std::optional<std::wstring>(L"main.cpp"), false), L"main.cpp - NeoMIFES");
}

TEST(MainWindowTest, FormatWindowTitleWithFilenameDirtyAppendsAsterisk) {
    EXPECT_EQ(formatWindowTitle(std::optional<std::wstring>(L"main.cpp"), true), L"main.cpp* - NeoMIFES");
}

TEST(MainWindowTest, FormatWindowTitleWithoutFilenameReturnsUntitled) {
    EXPECT_EQ(formatWindowTitle(std::nullopt, false), L"Untitled - NeoMIFES");
}

TEST(MainWindowTest, FormatWindowTitleWithoutFilenameDirtyAppendsAsterisk) {
    EXPECT_EQ(formatWindowTitle(std::nullopt, true), L"Untitled* - NeoMIFES");
}

}  // namespace
