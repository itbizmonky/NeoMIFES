#include <gtest/gtest.h>

#include "neomifes/render/gutter_math.h"

namespace {

using neomifes::render::computeGutterWidthDips;
using neomifes::render::digitCount;

TEST(GutterMathTest, DigitCountSingleDigit) {
    EXPECT_EQ(digitCount(0), 1U);
    EXPECT_EQ(digitCount(9), 1U);
}

TEST(GutterMathTest, DigitCountMultipleDigits) {
    EXPECT_EQ(digitCount(10), 2U);
    EXPECT_EQ(digitCount(99), 2U);
    EXPECT_EQ(digitCount(100), 3U);
    EXPECT_EQ(digitCount(999999), 6U);
    EXPECT_EQ(digitCount(1000000), 7U);
}

TEST(GutterMathTest, ComputeGutterWidthDipsZeroTotalLinesFallsBackToMinWidth) {
    EXPECT_FLOAT_EQ(computeGutterWidthDips(0, 8.0F, 24.0F), 24.0F);
}

TEST(GutterMathTest, ComputeGutterWidthDipsNonPositiveCharWidthFallsBackToMinWidth) {
    EXPECT_FLOAT_EQ(computeGutterWidthDips(1000, 0.0F, 24.0F), 24.0F);
    EXPECT_FLOAT_EQ(computeGutterWidthDips(1000, -1.0F, 24.0F), 24.0F);
}

TEST(GutterMathTest, ComputeGutterWidthDipsSmallDocumentClampsToMinWidth) {
    // digitCount(1)=1, so the raw computed width is (1+2)*charWidthDips.
    // With a small char width, that stays below minWidthDips and the floor
    // wins.
    EXPECT_FLOAT_EQ(computeGutterWidthDips(1, 5.0F, 24.0F), 24.0F);
}

TEST(GutterMathTest, ComputeGutterWidthDipsLargeDocumentGrowsPastMinWidth) {
    // digitCount(1000000)=7, charWidthDips=8 -> (7+2)*8 = 72, well past the
    // 24.0F floor - the whole point of a DYNAMIC-width gutter.
    EXPECT_FLOAT_EQ(computeGutterWidthDips(1000000, 8.0F, 24.0F), 72.0F);
}

TEST(GutterMathTest, ComputeGutterWidthDipsWidensAsDigitCountGrows) {
    const float tenLines   = computeGutterWidthDips(10, 8.0F, 24.0F);
    const float millionLines = computeGutterWidthDips(1000000, 8.0F, 24.0F);
    EXPECT_LT(tenLines, millionLines);
}

}  // namespace
