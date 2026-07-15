#pragma once

// Pure, dependency-free helpers for swap-chain resize and DPI math.
// Header-only and free of Windows-SDK includes so it stays trivially
// unit-testable without a live COM/device stack.

#include <cstdint>
#include <utility>

namespace neomifes::render {

// CreateSwapChainForHwnd/ResizeBuffers reject 0x0 (a minimized window
// delivers WM_SIZE(0,0)); clamp both dimensions to at least 1px.
[[nodiscard]] constexpr std::pair<std::uint32_t, std::uint32_t>
    sanitizeSwapChainSize(std::uint32_t width, std::uint32_t height) noexcept {
    return {width == 0 ? 1U : width, height == 0 ? 1U : height};
}

// 96 DPI is Windows' scale-factor-1.0 baseline (USER_DEFAULT_SCREEN_DPI).
[[nodiscard]] constexpr float dpiToScale(std::uint32_t dpi) noexcept {
    return static_cast<float>(dpi) / 96.0F;
}

}  // namespace neomifes::render
