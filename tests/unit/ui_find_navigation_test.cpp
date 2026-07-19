#include <gtest/gtest.h>

#include "neomifes/ui/find_navigation.h"

namespace {

using neomifes::ui::formatMatchCountLabel;
using neomifes::ui::nextMatchIndex;
using neomifes::ui::previousMatchIndex;

TEST(FindNavigationTest, NextMatchIndexAdvancesByOne) {
    EXPECT_EQ(nextMatchIndex(0, 3), 1U);
    EXPECT_EQ(nextMatchIndex(1, 3), 2U);
}

TEST(FindNavigationTest, NextMatchIndexWrapsAroundPastLast) {
    EXPECT_EQ(nextMatchIndex(2, 3), 0U);
}

TEST(FindNavigationTest, NextMatchIndexWithZeroCountReturnsZero) {
    EXPECT_EQ(nextMatchIndex(0, 0), 0U);
}

TEST(FindNavigationTest, NextMatchIndexWithSingleMatchStaysAtZero) {
    EXPECT_EQ(nextMatchIndex(0, 1), 0U);
}

TEST(FindNavigationTest, PreviousMatchIndexRetreatsByOne) {
    EXPECT_EQ(previousMatchIndex(2, 3), 1U);
    EXPECT_EQ(previousMatchIndex(1, 3), 0U);
}

TEST(FindNavigationTest, PreviousMatchIndexWrapsAroundBeforeFirst) {
    EXPECT_EQ(previousMatchIndex(0, 3), 2U);
}

TEST(FindNavigationTest, PreviousMatchIndexWithZeroCountReturnsZero) {
    EXPECT_EQ(previousMatchIndex(0, 0), 0U);
}

TEST(FindNavigationTest, PreviousMatchIndexWithSingleMatchStaysAtZero) {
    EXPECT_EQ(previousMatchIndex(0, 1), 0U);
}

TEST(FindNavigationTest, FormatMatchCountLabelIsOneBased) {
    EXPECT_EQ(formatMatchCountLabel(0, 12), L"1/12");
    EXPECT_EQ(formatMatchCountLabel(2, 12), L"3/12");
    EXPECT_EQ(formatMatchCountLabel(11, 12), L"12/12");
}

TEST(FindNavigationTest, FormatMatchCountLabelWithZeroCountShowsNoResults) {
    EXPECT_EQ(formatMatchCountLabel(0, 0), L"No results");
}

}  // namespace
