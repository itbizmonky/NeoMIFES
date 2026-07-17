#pragma once

// Pure click-sequence tracking: decides whether a new mouse-down continues
// the previous click's double/triple-click sequence, and computes the
// resulting click count (capped at 3). Header-only and free of Windows-SDK
// includes so it stays unit-testable without a live HWND (mirrors
// src/render/resize_math.h's rationale) - the first part of MainWindow's
// logic to be testable this way (Phase 4b4).
//
// Win32's own WM_LBUTTONDBLCLK (needs CS_DBLCLKS) has no notion of a third
// click, so MainWindow tracks every WM_LBUTTONDOWN through this instead;
// the window class does not opt into CS_DBLCLKS.

#include <cstdint>

namespace neomifes::ui {

struct ClickPoint {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct ClickTrackerState {
    ClickPoint    lastPos{};
    std::uint32_t lastTimeMs = 0;
    int           count      = 0;  // 0 == no prior click

    friend constexpr bool operator==(const ClickTrackerState&, const ClickTrackerState&) = default;
};

// thresholdMs/maxDx/maxDy come from the caller (GetDoubleClickTime()/
// GetSystemMetrics(SM_CXDOUBLECLK, SM_CYDOUBLECLK) at the MainWindow call
// site) rather than being hardcoded here, keeping this header Win32-free.
// Returns the state to persist for the next call; its `count` is 1, 2, or 3
// (a 4th rapid click stays at 3 rather than wrapping back to 1).
[[nodiscard]] constexpr ClickTrackerState nextClickState(const ClickTrackerState& previous,
                                                          ClickPoint pos, std::uint32_t nowMs,
                                                          std::uint32_t thresholdMs,
                                                          std::int32_t maxDx,
                                                          std::int32_t maxDy) noexcept {
    const std::int32_t dx = pos.x - previous.lastPos.x;
    const std::int32_t dy = pos.y - previous.lastPos.y;
    const bool withinTime = (nowMs - previous.lastTimeMs) <= thresholdMs;
    const bool withinDist = dx >= -maxDx && dx <= maxDx && dy >= -maxDy && dy <= maxDy;
    const bool isRepeat   = previous.count > 0 && withinTime && withinDist;
    const int  newCount   = isRepeat ? (previous.count < 3 ? previous.count + 1 : 3) : 1;
    return ClickTrackerState{.lastPos = pos, .lastTimeMs = nowMs, .count = newCount};
}

}  // namespace neomifes::ui
