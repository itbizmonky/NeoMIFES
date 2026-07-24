#include <gtest/gtest.h>

#include "neomifes/render/indent_guide_math.h"

namespace {

using neomifes::render::computeIndentColumns;
using neomifes::render::computeIndentGuideCount;

TEST(IndentGuideMathTest, NoLeadingWhitespaceReturnsZeroColumns) {
    EXPECT_EQ(computeIndentColumns(u"int x = 1;", 4), 0U);
}

TEST(IndentGuideMathTest, EmptyStringReturnsZeroColumns) {
    EXPECT_EQ(computeIndentColumns(u"", 4), 0U);
}

TEST(IndentGuideMathTest, SpacesOnlyCountOnePerSpace) {
    EXPECT_EQ(computeIndentColumns(u"        x", 4), 8U);
}

TEST(IndentGuideMathTest, TabsOnlyAdvanceToNextTabStop) {
    EXPECT_EQ(computeIndentColumns(u"\t\tx", 4), 8U);
}

TEST(IndentGuideMathTest, MixedSpacesAndTabsFollowTabStopConvention) {
    // 2 spaces (column 2) then a tab advances to the next multiple of 4 (4),
    // not a flat +1 - matches core::computeIndentationConversionEdits()'s
    // tab-stop semantics.
    EXPECT_EQ(computeIndentColumns(u"  \tx", 4), 4U);
}

TEST(IndentGuideMathTest, TabMidRunAdvancesFromCurrentColumnNotZero) {
    // 5 spaces (column 5) then a tab advances to the next multiple of 4
    // (8), not to 4 (which is already behind column 5).
    EXPECT_EQ(computeIndentColumns(u"     \tx", 4), 8U);
}

TEST(IndentGuideMathTest, StopsAtFirstNonWhitespaceCharacter) {
    EXPECT_EQ(computeIndentColumns(u"  x    y", 4), 2U);
}

TEST(IndentGuideMathTest, WhitespaceOnlyLineCountsWholeString) {
    EXPECT_EQ(computeIndentColumns(u"    ", 4), 4U);
}

TEST(IndentGuideMathTest, ZeroTabWidthReturnsZeroColumns) {
    EXPECT_EQ(computeIndentColumns(u"    x", 0), 0U);
}

TEST(IndentGuideMathTest, GuideCountZeroColumnsIsZeroGuides) {
    EXPECT_EQ(computeIndentGuideCount(0, 4), 0U);
}

TEST(IndentGuideMathTest, GuideCountExactMultipleOfTabWidth) {
    EXPECT_EQ(computeIndentGuideCount(8, 4), 2U);
}

TEST(IndentGuideMathTest, GuideCountFloorsPartialRemainder) {
    // 6 columns with tabWidth 4 is 1 full level plus a 2-column remainder -
    // only the completed level gets a guide.
    EXPECT_EQ(computeIndentGuideCount(6, 4), 1U);
}

TEST(IndentGuideMathTest, GuideCountBelowOneLevelIsZero) {
    EXPECT_EQ(computeIndentGuideCount(3, 4), 0U);
}

TEST(IndentGuideMathTest, GuideCountZeroTabWidthReturnsZero) {
    EXPECT_EQ(computeIndentGuideCount(8, 0), 0U);
}

}  // namespace
