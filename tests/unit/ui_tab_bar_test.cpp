#include <gtest/gtest.h>

#include "neomifes/ui/tab_bar.h"

namespace {

using neomifes::ui::formatTabBaseLabel;

TEST(TabBarTest, FormatTabBaseLabelWithFilenameReturnsFilenameVerbatim) {
    EXPECT_EQ(formatTabBaseLabel(std::optional<std::wstring>(L"main.cpp"), 1), L"main.cpp");
}

TEST(TabBarTest, FormatTabBaseLabelWithoutFilenameReturnsUntitledPlusOrdinal) {
    EXPECT_EQ(formatTabBaseLabel(std::nullopt, 1), L"Untitled 1");
    EXPECT_EQ(formatTabBaseLabel(std::nullopt, 2), L"Untitled 2");
}

TEST(TabBarTest, FormatTabBaseLabelIgnoresOrdinalWhenFilenameIsPresent) {
    // untitledOrdinal is only meaningful in the no-filename case - a named
    // tab's label must not vary with it.
    EXPECT_EQ(formatTabBaseLabel(std::optional<std::wstring>(L"a.txt"), 5), L"a.txt");
}

}  // namespace
