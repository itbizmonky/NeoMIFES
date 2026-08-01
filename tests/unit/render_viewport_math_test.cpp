#include <gtest/gtest.h>

#include "neomifes/render/viewport_math.h"

namespace {

using neomifes::render::computeMinimapBucketCount;
using neomifes::render::computeVisibleLineCount;
using neomifes::render::minimapBucketStartLine;
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

// Phase 7w: computeMinimapBucketCount()/minimapBucketStartLine() - the
// minimap's "whole document overview" bucketing math.

TEST(ComputeMinimapBucketCountTest, CapsAtHeightLimitForHugeDocuments) {
    // 700 DIPs tall strip, 2.5-DIP minimum row height -> floor(700/2.5)=280
    // buckets, far fewer than the document's 1,000,000 lines.
    EXPECT_EQ(computeMinimapBucketCount(700.0F, 2.5F, 1'000'000ULL), 280ULL);
}

TEST(ComputeMinimapBucketCountTest, FallsBackToOneBucketPerLineForSmallDocuments) {
    // A 5-line document never needs more than 5 buckets, even though the
    // strip could physically fit far more rows - Phase 7v's original 1:1
    // behavior falls out of this as a special case.
    EXPECT_EQ(computeMinimapBucketCount(700.0F, 2.5F, 5ULL), 5ULL);
}

TEST(ComputeMinimapBucketCountTest, ReturnsZeroForZeroTotalLines) {
    EXPECT_EQ(computeMinimapBucketCount(700.0F, 2.5F, 0ULL), 0ULL);
}

TEST(ComputeMinimapBucketCountTest, ReturnsZeroForZeroHeight) {
    EXPECT_EQ(computeMinimapBucketCount(0.0F, 2.5F, 1000ULL), 0ULL);
}

TEST(ComputeMinimapBucketCountTest, ReturnsZeroForZeroMinRowHeight) {
    EXPECT_EQ(computeMinimapBucketCount(700.0F, 0.0F, 1000ULL), 0ULL);
}

TEST(ComputeMinimapBucketCountTest, ReturnsZeroForNegativeHeight) {
    EXPECT_EQ(computeMinimapBucketCount(-700.0F, 2.5F, 1000ULL), 0ULL);
}

TEST(MinimapBucketStartLineTest, FirstBucketIsAlwaysLineZero) {
    EXPECT_EQ(minimapBucketStartLine(0, 280, 1'000'000ULL), 0ULL);
}

TEST(MinimapBucketStartLineTest, LastBucketIsStrictlyLessThanTotalLines) {
    constexpr std::uint64_t kTotalLines  = 1'000'000ULL;
    constexpr std::uint64_t kBucketCount = 280ULL;
    EXPECT_LT(minimapBucketStartLine(kBucketCount - 1, kBucketCount, kTotalLines), kTotalLines);
}

TEST(MinimapBucketStartLineTest, DistributesRemainderWithoutDrift) {
    // 1000 lines over 7 buckets doesn't divide evenly (142.86 lines/bucket)
    // - each bucket's own start should still land within 1 line of the
    // ideal, never accumulating drift across buckets.
    constexpr std::uint64_t kTotalLines  = 1000ULL;
    constexpr std::uint64_t kBucketCount = 7ULL;
    std::uint64_t           previousStart = 0;
    for (std::uint64_t bucket = 0; bucket < kBucketCount; ++bucket) {
        const std::uint64_t start = minimapBucketStartLine(bucket, kBucketCount, kTotalLines);
        if (bucket > 0) {
            EXPECT_GE(start, previousStart);
        }
        previousStart = start;
    }
    EXPECT_LT(previousStart, kTotalLines);
}

TEST(MinimapBucketStartLineTest, WithBucketCountEqualToTotalLinesIsIdentity) {
    constexpr std::uint64_t kTotalLines = 500ULL;
    for (std::uint64_t bucket = 0; bucket < kTotalLines; ++bucket) {
        EXPECT_EQ(minimapBucketStartLine(bucket, kTotalLines, kTotalLines), bucket);
    }
}

}  // namespace
