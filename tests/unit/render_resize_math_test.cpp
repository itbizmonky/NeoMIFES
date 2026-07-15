#include <gtest/gtest.h>

#include "neomifes/render/resize_math.h"

namespace {

using neomifes::render::dpiToScale;
using neomifes::render::sanitizeSwapChainSize;

TEST(ResizeMathTest, SanitizeSwapChainSizePassesThroughNonZero) {
    const auto [w, h] = sanitizeSwapChainSize(1920, 1080);
    EXPECT_EQ(w, 1920U);
    EXPECT_EQ(h, 1080U);
}

TEST(ResizeMathTest, SanitizeSwapChainSizeClampsZeroWidth) {
    const auto [w, h] = sanitizeSwapChainSize(0, 600);
    EXPECT_EQ(w, 1U);
    EXPECT_EQ(h, 600U);
}

TEST(ResizeMathTest, SanitizeSwapChainSizeClampsZeroHeight) {
    const auto [w, h] = sanitizeSwapChainSize(800, 0);
    EXPECT_EQ(w, 800U);
    EXPECT_EQ(h, 1U);
}

TEST(ResizeMathTest, SanitizeSwapChainSizeClampsBothZero) {
    const auto [w, h] = sanitizeSwapChainSize(0, 0);
    EXPECT_EQ(w, 1U);
    EXPECT_EQ(h, 1U);
}

TEST(ResizeMathTest, DpiToScaleAtBaseline) {
    EXPECT_FLOAT_EQ(dpiToScale(96), 1.0F);
}

TEST(ResizeMathTest, DpiToScaleAt150Percent) {
    EXPECT_FLOAT_EQ(dpiToScale(144), 1.5F);
}

TEST(ResizeMathTest, DpiToScaleAt200Percent) {
    EXPECT_FLOAT_EQ(dpiToScale(192), 2.0F);
}

}  // namespace
