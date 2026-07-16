#pragma once

// Pure, dependency-free helper for how many text lines fit in the client
// area. Header-only, no Windows-SDK includes, so it stays unit-testable
// without a live COM/device/DirectWrite stack (mirrors resize_math.h).

#include <cstdint>

namespace neomifes::render {

// clientHeightPx: client-area height in device pixels (as WM_SIZE reports).
// dpiScale: from resize_math.h::dpiToScale(). lineHeightDips: measured from
// the active IDWriteTextFormat's line metrics. Returns 0 if any input is
// non-positive (degenerate window/font state) rather than a garbage count.
[[nodiscard]] constexpr std::uint32_t computeVisibleLineCount(
    std::uint32_t clientHeightPx, float dpiScale, float lineHeightDips) noexcept {
    if (dpiScale <= 0.0F || lineHeightDips <= 0.0F || clientHeightPx == 0) {
        return 0;
    }
    const float clientHeightDips = static_cast<float>(clientHeightPx) / dpiScale;
    const float count            = clientHeightDips / lineHeightDips;
    return count > 0.0F ? static_cast<std::uint32_t>(count) : 0U;
}

}  // namespace neomifes::render
