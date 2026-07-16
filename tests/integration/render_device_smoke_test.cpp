// Integration test (not a unit test): exercises real COM/D3D11/D2D/DXGI
// device creation, which the plain unit-test binary avoids entirely. See
// tests/integration/CMakeLists.txt - a separate executable/ctest entry from
// neomifes_unit_tests, following the startup_measure_test precedent.

#include <gtest/gtest.h>

#include <windows.h>

#include "neomifes/render/render_device.h"
#include "neomifes/render/render_error.h"

namespace {

using neomifes::render::RenderDevice;

TEST(RenderDeviceSmokeTest, CreateHeadlessSucceedsViaHardwareOrWarpFallback) {
    const auto device = RenderDevice::createHeadless();
    // Should succeed even on a GPU-less CI runner - WARP is a core OS
    // component with no driver dependency. A hard failure here means the
    // HARDWARE->WARP fallback itself is broken, not just "no GPU available",
    // so this is a real assertion rather than a soft skip.
    ASSERT_TRUE(device.has_value())
        << "RenderDevice::createHeadless() failed on both HARDWARE and WARP: "
        << neomifes::render::describe(device.error());
}

TEST(RenderDeviceSmokeTest, CreateAndPresentOnHiddenWindow) {
    // A hidden, never-shown popup on the built-in STATIC window class is
    // enough to get a valid HWND for CreateSwapChainForHwnd without any
    // visible UI or a custom WNDCLASS registration.
    HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                  nullptr, nullptr);
    ASSERT_NE(hwnd, nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    auto device = RenderDevice::create(hwnd, 64, 64);
    if (!device.has_value()) {
        ::DestroyWindow(hwnd);
        GTEST_SKIP() << "RenderDevice::create() failed in this environment: "
                     << neomifes::render::describe(device.error());
    }

    auto beginResult = device->beginFrame();
    ASSERT_TRUE(beginResult.has_value())
        << "beginFrame() failed: " << neomifes::render::describe(beginResult.error());
    constexpr D2D1_COLOR_F kBlack = {0.0F, 0.0F, 0.0F, 1.0F};
    (*beginResult)->Clear(kBlack);
    const auto endResult = device->endFrame();
    ::DestroyWindow(hwnd);

    // DXGI_STATUS_OCCLUDED is a legitimate non-error for a never-shown
    // window and is already treated as success inside endFrame().
    EXPECT_TRUE(endResult.has_value())
        << (endResult.has_value() ? "" : neomifes::render::describe(endResult.error()));
}

TEST(RenderDeviceSmokeTest, EndFrameWithoutBeginFrameFails) {
    HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                  nullptr, nullptr);
    ASSERT_NE(hwnd, nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    auto device = RenderDevice::create(hwnd, 64, 64);
    if (!device.has_value()) {
        ::DestroyWindow(hwnd);
        GTEST_SKIP() << "RenderDevice::create() failed in this environment: "
                     << neomifes::render::describe(device.error());
    }

    const auto endResult = device->endFrame();
    ::DestroyWindow(hwnd);
    EXPECT_FALSE(endResult.has_value());
}

TEST(RenderDeviceSmokeTest, BeginFrameTwiceWithoutEndFrameFails) {
    HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                  nullptr, nullptr);
    ASSERT_NE(hwnd, nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    auto device = RenderDevice::create(hwnd, 64, 64);
    if (!device.has_value()) {
        ::DestroyWindow(hwnd);
        GTEST_SKIP() << "RenderDevice::create() failed in this environment: "
                     << neomifes::render::describe(device.error());
    }

    const auto firstBegin = device->beginFrame();
    ASSERT_TRUE(firstBegin.has_value())
        << "first beginFrame() failed: " << neomifes::render::describe(firstBegin.error());
    const auto secondBegin = device->beginFrame();
    EXPECT_FALSE(secondBegin.has_value());

    // Close the still-open frame so the device isn't left mid-draw at teardown.
    (void)device->endFrame();
    ::DestroyWindow(hwnd);
}

}  // namespace
