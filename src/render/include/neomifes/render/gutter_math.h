#pragma once

// Pure, dependency-free helpers for the dynamic-width line-number gutter
// (WI-07 step7). Header-only, no Windows-SDK includes, so they stay
// unit-testable without a live COM/device/DirectWrite stack (mirrors
// indent_guide_math.h/resize_math.h/viewport_math.h).

#include <algorithm>
#include <cstdint>

namespace neomifes::render {

// Number of decimal digits in n (n=0 counts as 1 digit, matching how a
// line number is actually displayed - there is no line "0").
[[nodiscard]] constexpr std::uint32_t digitCount(std::uint64_t n) noexcept {
    std::uint32_t count = 1;
    while (n >= 10) {
        n /= 10;
        ++count;
    }
    return count;
}

// Gutter width in DIPs wide enough to fit totalLines' decimal digit count
// at charWidthDips per column, plus a fixed 2-column padding allowance -
// clamped to never go below minWidthDips (the caller's pre-Phase-7
// kGutterWidthDips constant, preserving old layouts/tests when charWidthDips
// hasn't been measured yet or the document is still empty). totalLines==0 or
// charWidthDips<=0 (degenerate/unmeasured input) falls back to minWidthDips
// outright rather than computing a meaningless width.
[[nodiscard]] constexpr float computeGutterWidthDips(std::uint64_t totalLines, float charWidthDips,
                                                       float minWidthDips) noexcept {
    if (totalLines == 0 || charWidthDips <= 0.0F) {
        return minWidthDips;
    }
    constexpr float kPaddingCharWidths = 2.0F;
    const float digitsWidthDips = static_cast<float>(digitCount(totalLines)) * charWidthDips;
    const float computedWidthDips = digitsWidthDips + (kPaddingCharWidths * charWidthDips);
    return std::max(computedWidthDips, minWidthDips);
}

}  // namespace neomifes::render
