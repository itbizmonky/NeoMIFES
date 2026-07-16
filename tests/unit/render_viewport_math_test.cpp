#include <gtest/gtest.h>

#include "neomifes/render/viewport_math.h"

namespace {

using neomifes::render::computeVisibleLineCount;

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

}  // namespace
