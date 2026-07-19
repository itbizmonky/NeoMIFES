#pragma once

// Pure match-navigation helpers for the Find bar (Phase 5b3a): wraparound
// index arithmetic and match-count label formatting. Header-only and free
// of Windows-SDK includes (except <string> for the label, which is a plain
// value type, not a handle) so it stays unit-testable without a live HWND -
// mirrors click_tracking.h's rationale.

#include <cstddef>
#include <string>

namespace neomifes::ui {

// Index of the match after `currentIndex`, wrapping to 0 past the last one.
// count==0 returns 0 - callers must check count==0 separately before using
// the result meaningfully (there is no match to navigate to).
[[nodiscard]] constexpr std::size_t nextMatchIndex(std::size_t currentIndex,
                                                    std::size_t count) noexcept {
    if (count == 0) {
        return 0;
    }
    return (currentIndex + 1) % count;
}

// Index of the match before `currentIndex`, wrapping to the last one before
// index 0. Same count==0 contract as nextMatchIndex().
[[nodiscard]] constexpr std::size_t previousMatchIndex(std::size_t currentIndex,
                                                        std::size_t count) noexcept {
    if (count == 0) {
        return 0;
    }
    return (currentIndex == 0) ? (count - 1) : (currentIndex - 1);
}

// Formats the Find bar's "N/M" match-count label (1-based), or a distinct
// no-matches string when count==0.
[[nodiscard]] inline std::wstring formatMatchCountLabel(std::size_t currentIndex,
                                                         std::size_t count) {
    if (count == 0) {
        return L"No results";
    }
    return std::to_wstring(currentIndex + 1) + L"/" + std::to_wstring(count);
}

}  // namespace neomifes::ui
