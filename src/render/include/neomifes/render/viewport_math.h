#pragma once

// Pure, dependency-free helper for how many text lines fit in the client
// area. Header-only, no Windows-SDK includes, so it stays unit-testable
// without a live COM/device/DirectWrite stack (mirrors resize_math.h).

#include <cstdint>
#include <utility>

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

// Phase 7t: widens [visibleStart, visibleEnd) by up to `marginLines` lines
// on each side, clamped to [0, totalLines) - the syntax-token "prefetch
// window" RenderPipeline::computeDesiredTokenRange() requests, so small
// scrolls within the margin don't each trigger a fresh async re-tokenize
// request. Pure/header-only (no Document/TextPos conversion here - that
// needs a live Document::lineToOffset(), done by the caller) so the margin
// arithmetic itself stays unit-testable without a device/window, same
// reasoning as computeVisibleLineCount() above.
[[nodiscard]] constexpr std::pair<std::uint64_t, std::uint64_t> widenLineRangeWithMargin(
    std::uint64_t visibleStart, std::uint64_t visibleEnd, std::uint64_t marginLines,
    std::uint64_t totalLines) noexcept {
    const std::uint64_t marginedStart = visibleStart > marginLines ? visibleStart - marginLines : 0;
    const std::uint64_t remaining     = totalLines > visibleEnd ? totalLines - visibleEnd : 0;
    const std::uint64_t marginedEnd   = remaining > marginLines ? visibleEnd + marginLines : totalLines;
    return {marginedStart, marginedEnd};
}

}  // namespace neomifes::render
