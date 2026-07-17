// Integration test (not a unit test): exercises the real OS clipboard
// (Phase 4b6c), which the plain unit-test binary avoids entirely - same
// rationale as render_device_smoke_test.cpp for real D3D11/D2D device
// creation. A CI runner without an interactive desktop session may not have
// a usable clipboard at all, so failures are a soft skip rather than a hard
// assertion (see GTEST_SKIP() below).

#include <gtest/gtest.h>

#include <windows.h>

#include "neomifes/platform/clipboard.h"

namespace {

using neomifes::platform::getClipboardText;
using neomifes::platform::setClipboardText;

TEST(ClipboardTest, RoundTripsUnicodeTextThroughTheRealClipboard) {
    HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                  nullptr, nullptr);
    ASSERT_NE(hwnd, nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    const std::u16string original = u"NeoMIFES clipboard テスト";  // includes CJK
    if (!setClipboardText(hwnd, original)) {
        ::DestroyWindow(hwnd);
        GTEST_SKIP() << "setClipboardText() failed in this environment (no clipboard access?)";
    }

    const auto readBack = getClipboardText(hwnd);
    ::DestroyWindow(hwnd);
    ASSERT_TRUE(readBack.has_value());
    EXPECT_EQ(*readBack, original);
}

TEST(ClipboardTest, SetClipboardTextWithEmptyStringSucceeds) {
    HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                  nullptr, nullptr);
    ASSERT_NE(hwnd, nullptr) << "CreateWindowExW failed: " << ::GetLastError();

    if (!setClipboardText(hwnd, u"")) {
        ::DestroyWindow(hwnd);
        GTEST_SKIP() << "setClipboardText() failed in this environment (no clipboard access?)";
    }

    const auto readBack = getClipboardText(hwnd);
    ::DestroyWindow(hwnd);
    ASSERT_TRUE(readBack.has_value());
    EXPECT_EQ(*readBack, u"");
}

}  // namespace
