#include <gtest/gtest.h>

#include <d2derr.h>
#include <dxgi.h>

#include "neomifes/render/render_error.h"

namespace {

using neomifes::render::describe;
using neomifes::render::RenderError;
using neomifes::render::RenderStage;

TEST(RenderErrorTest, IsDeviceLostTrueForDeviceRemoved) {
    const RenderError err{RenderStage::Present, DXGI_ERROR_DEVICE_REMOVED};
    EXPECT_TRUE(err.isDeviceLost());
}

TEST(RenderErrorTest, IsDeviceLostTrueForDeviceReset) {
    const RenderError err{RenderStage::Present, DXGI_ERROR_DEVICE_RESET};
    EXPECT_TRUE(err.isDeviceLost());
}

TEST(RenderErrorTest, IsDeviceLostTrueForDeviceHung) {
    const RenderError err{RenderStage::Present, DXGI_ERROR_DEVICE_HUNG};
    EXPECT_TRUE(err.isDeviceLost());
}

TEST(RenderErrorTest, IsDeviceLostTrueForRecreateTarget) {
    const RenderError err{RenderStage::Present, D2DERR_RECREATE_TARGET};
    EXPECT_TRUE(err.isDeviceLost());
}

TEST(RenderErrorTest, IsDeviceLostFalseForUnrelatedFailure) {
    const RenderError err{RenderStage::D3D11Device, E_FAIL};
    EXPECT_FALSE(err.isDeviceLost());
}

TEST(RenderErrorTest, IsDeviceLostFalseForSuccess) {
    const RenderError err{RenderStage::NotAttached, S_OK};
    EXPECT_FALSE(err.isDeviceLost());
}

TEST(RenderErrorTest, DescribeIncludesStageName) {
    const RenderError err{RenderStage::Present, DXGI_ERROR_DEVICE_REMOVED};
    const std::string text = describe(err);
    EXPECT_NE(text.find("Present"), std::string::npos);
}

TEST(RenderErrorTest, DescribeDoesNotCrashOnUnknownHresult) {
    // An HRESULT with no system message table entry - FormatMessageW should
    // just fail gracefully and describe() should still produce a stage-only
    // string rather than crashing.
    const RenderError err{RenderStage::D2DFactory, static_cast<HRESULT>(0x89990001)};
    const std::string text = describe(err);
    EXPECT_NE(text.find("D2DFactory"), std::string::npos);
}

}  // namespace
