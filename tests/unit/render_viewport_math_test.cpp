#include <gtest/gtest.h>

#include "neomifes/render/viewport_math.h"

namespace {

using neomifes::render::computeVisibleLineCount;
using neomifes::render::widenLineRangeWithMargin;

TEST(ViewportMathTest, BaselineDpiComputesExpectedLineCount) {
    // 800px / 1.0 scale = 800 DIPs; 800 / 20 DIPs-per-line = 40 lines.
    EXPECT_EQ(computeVisibleLineCount(800, 1.0F, 20.0F), 40U);
}

TEST(ViewportMathTest, HigherDpiFitsFewerLinesForSamePixelHeight) {
    // Same 800px client height, but 150% DPI means fewer DIPs are visible.
    const auto baseline = computeVisibleLineCount(800, 1.0F, 20.0F);
    const auto scaled    = computeVisibleLineCount(800, 1.5F, 20.0F);
    EXPECT_LT(scaled, baseline);
}

TEST(ViewportMathTest, ZeroClientHeightReturnsZero) {
    EXPECT_EQ(computeVisibleLineCount(0, 1.0F, 20.0F), 0U);
}

TEST(ViewportMathTest, ZeroDpiScaleReturnsZero) {
    EXPECT_EQ(computeVisibleLineCount(800, 0.0F, 20.0F), 0U);
}

TEST(ViewportMathTest, NegativeDpiScaleReturnsZero) {
    EXPECT_EQ(computeVisibleLineCount(800, -1.0F, 20.0F), 0U);
}

TEST(ViewportMathTest, ZeroLineHeightReturnsZero) {
    EXPECT_EQ(computeVisibleLineCount(800, 1.0F, 0.0F), 0U);
}

TEST(ViewportMathTest, NegativeLineHeightReturnsZero) {
    EXPECT_EQ(computeVisibleLineCount(800, 1.0F, -20.0F), 0U);
}

TEST(ViewportMathTest, LineHeightTallerThanClientReturnsZero) {
    EXPECT_EQ(computeVisibleLineCount(10, 1.0F, 20.0F), 0U);
}

// Phase 7t: widenLineRangeWithMargin() - the syntax-token prefetch window
// computation.

TEST(WidenLineRangeWithMarginTest, BothMarginsFitWithinDocumentBounds) {
    const auto [start, end] = widenLineRangeWithMargin(100, 150, 20, 1000);
    EXPECT_EQ(start, 80U);
    EXPECT_EQ(end, 170U);
}

TEST(WidenLineRangeWithMarginTest, StartMarginClampsAtDocumentStart) {
    const auto [start, end] = widenLineRangeWithMargin(10, 50, 20, 1000);
    EXPECT_EQ(start, 0U);
    EXPECT_EQ(end, 70U);
}

TEST(WidenLineRangeWithMarginTest, EndMarginClampsAtDocumentEnd) {
    const auto [start, end] = widenLineRangeWithMargin(900, 980, 50, 1000);
    EXPECT_EQ(start, 850U);
    EXPECT_EQ(end, 1000U);
}

TEST(WidenLineRangeWithMarginTest, MarginLargerThanDocumentClampsBothSides) {
    // A tiny document (30 lines) with a much larger margin (100) - both
    // sides clamp to the document's own bounds.
    const auto [start, end] = widenLineRangeWithMargin(5, 25, 100, 30);
    EXPECT_EQ(start, 0U);
    EXPECT_EQ(end, 30U);
}

TEST(WidenLineRangeWithMarginTest, ZeroMarginReturnsTheOriginalRangeUnchanged) {
    const auto [start, end] = widenLineRangeWithMargin(100, 150, 0, 1000);
    EXPECT_EQ(start, 100U);
    EXPECT_EQ(end, 150U);
}

}  // namespace
