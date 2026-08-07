#include <gtest/gtest.h>

#include "neomifes/app/tab_index_math.h"

namespace {

using neomifes::app::nextTabIndex;
using neomifes::app::previousTabIndex;
using neomifes::app::tabIndexForDigit;

TEST(TabIndexMathTest, NextTabIndexAdvancesByOne) {
    EXPECT_EQ(nextTabIndex(0, 3), 1U);
    EXPECT_EQ(nextTabIndex(1, 3), 2U);
}

TEST(TabIndexMathTest, NextTabIndexWrapsAroundPastTheLastTab) {
    EXPECT_EQ(nextTabIndex(2, 3), 0U);
}

TEST(TabIndexMathTest, NextTabIndexWithSingleTabStaysAtZero) {
    EXPECT_EQ(nextTabIndex(0, 1), 0U);
}

TEST(TabIndexMathTest, NextTabIndexWithZeroTabsReturnsZero) {
    EXPECT_EQ(nextTabIndex(0, 0), 0U);
}

TEST(TabIndexMathTest, PreviousTabIndexRetreatsByOne) {
    EXPECT_EQ(previousTabIndex(2, 3), 1U);
    EXPECT_EQ(previousTabIndex(1, 3), 0U);
}

TEST(TabIndexMathTest, PreviousTabIndexWrapsAroundPastTheFirstTab) {
    EXPECT_EQ(previousTabIndex(0, 3), 2U);
}

TEST(TabIndexMathTest, PreviousTabIndexWithSingleTabStaysAtZero) {
    EXPECT_EQ(previousTabIndex(0, 1), 0U);
}

TEST(TabIndexMathTest, PreviousTabIndexWithZeroTabsReturnsZero) {
    EXPECT_EQ(previousTabIndex(0, 0), 0U);
}

TEST(TabIndexMathTest, TabIndexForDigitIsFaceValueOneBasedToZeroBasedIndex) {
    EXPECT_EQ(tabIndexForDigit(1, 9), std::optional<std::size_t>(0));
    EXPECT_EQ(tabIndexForDigit(9, 9), std::optional<std::size_t>(8));
}

TEST(TabIndexMathTest, TabIndexForDigitReturnsNulloptWhenNoTabAtThatPosition) {
    // Only 3 tabs open; Ctrl+9 does NOT clamp to the last tab (unlike
    // Chrome/VSCode) - it's simply a no-op.
    EXPECT_EQ(tabIndexForDigit(9, 3), std::nullopt);
    EXPECT_EQ(tabIndexForDigit(4, 3), std::nullopt);
}

TEST(TabIndexMathTest, TabIndexForDigitReturnsNulloptForOutOfRangeDigits) {
    EXPECT_EQ(tabIndexForDigit(0, 9), std::nullopt);
    EXPECT_EQ(tabIndexForDigit(10, 9), std::nullopt);
    EXPECT_EQ(tabIndexForDigit(-1, 9), std::nullopt);
}

TEST(TabIndexMathTest, TabIndexForDigitReturnsNulloptWithZeroTabs) {
    EXPECT_EQ(tabIndexForDigit(1, 0), std::nullopt);
}

}  // namespace
