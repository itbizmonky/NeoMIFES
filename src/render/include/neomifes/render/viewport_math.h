#pragma once

// Pure, dependency-free helper for how many text lines fit in the client
// area. Header-only, no Windows-SDK includes, so it stays unit-testable
// without a live COM/device/DirectWrite stack (mirrors resize_math.h).

#include <algorithm>
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

// Phase 7w: minimap "whole document overview" bucketing math. A minimap
// strip physically cannot draw one row per document line once totalLines
// exceeds availableHeightDips/minRowHeightDips (e.g. 1,000,000 lines in a
// 700-DIP-tall strip) - this caps the row count at whatever actually fits,
// falling back to totalLines itself (one bucket per line, matching Phase
// 7v's original 1:1 behavior exactly) whenever the document is small enough
// that every line already gets its own row. minRowHeightDips is the SAME
// derived value RenderPipeline::drawMinimap() already computes
// (m_lineHeightDips / kMinimapScaleDivisor, roadmap sec.7.4's "1/8 scale") -
// not a new guessed constant.
[[nodiscard]] constexpr std::uint64_t computeMinimapBucketCount(
    float availableHeightDips, float minRowHeightDips, std::uint64_t totalLines) noexcept {
    if (availableHeightDips <= 0.0F || minRowHeightDips <= 0.0F || totalLines == 0) {
        return 0;
    }
    const auto          maxByHeight     = static_cast<std::uint64_t>(availableHeightDips / minRowHeightDips);
    const std::uint64_t bucketsByHeight = maxByHeight > 0 ? maxByHeight : 1;
    return std::min(bucketsByHeight, totalLines);
}

// The first document line belonging to `bucket` (0-based) out of
// `bucketCount` buckets spanning `totalLines` lines total. Integer
// (bucket * totalLines) / bucketCount, computed independently per bucket
// rather than via a running "+= totalLines/bucketCount" accumulation loop -
// the latter would drift when totalLines isn't a clean multiple of
// bucketCount, this doesn't (standard "partition N items into K
// nearly-equal groups" idiom). bucket==0 always yields line 0;
// bucket==bucketCount-1 always yields a line strictly less than totalLines
// (given bucketCount>=1) - both properties RenderPipeline::drawMinimapLines()
// relies on without re-checking.
[[nodiscard]] constexpr std::uint64_t minimapBucketStartLine(
    std::uint64_t bucket, std::uint64_t bucketCount, std::uint64_t totalLines) noexcept {
    return bucketCount == 0 ? 0 : (bucket * totalLines) / bucketCount;
}

}  // namespace neomifes::render
