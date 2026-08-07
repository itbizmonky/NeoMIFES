#pragma once

// Pure, Win32/Workspace-independent tab-index arithmetic (WI-05). Header-only
// so it stays unit-testable without a live Workspace/RenderPipeline, mirroring
// viewport_math.h/resize_math.h's "small shared helper, dependency-free"
// pattern.

#include <cstddef>
#include <optional>

namespace neomifes::app {

// Ctrl+Tab: the tab after `activeIndex`, wrapping around past the last tab
// back to 0. tabCount==0 returns 0 (degenerate/unreachable in practice -
// Workspace always holds >=1 session - but avoids a division by zero).
[[nodiscard]] constexpr std::size_t nextTabIndex(std::size_t activeIndex, std::size_t tabCount) noexcept {
    if (tabCount == 0) {
        return 0;
    }
    return (activeIndex + 1) % tabCount;
}

// Ctrl+Shift+Tab: the tab before `activeIndex`, wrapping around past the
// first tab back to the last one.
[[nodiscard]] constexpr std::size_t previousTabIndex(std::size_t activeIndex, std::size_t tabCount) noexcept {
    if (tabCount == 0) {
        return 0;
    }
    return activeIndex == 0 ? tabCount - 1 : activeIndex - 1;
}

// Ctrl+1..Ctrl+9: `digit` is taken at face value (Ctrl+1 == tab index 0, ...,
// Ctrl+9 == tab index 8) - not Chrome/VSCode's "9 always means the last tab"
// convention. Returns nullopt (no-op, deliberately NOT clamped - clamping
// would silently jump to an unintended tab) if `digit` is outside 1..9 or
// there is no tab at that position.
[[nodiscard]] constexpr std::optional<std::size_t> tabIndexForDigit(int digit, std::size_t tabCount) noexcept {
    if (digit < 1 || digit > 9) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(digit - 1);
    if (index >= tabCount) {
        return std::nullopt;
    }
    return index;
}

}  // namespace neomifes::app
