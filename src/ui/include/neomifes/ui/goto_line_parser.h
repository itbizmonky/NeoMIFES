#pragma once

// parseGotoLineInput - parses GotoLineBar's text input (Phase 4b8b). Header-
// only and free of Windows-SDK includes so it stays unit-testable without a
// live HWND, mirroring find_navigation.h/click_tracking.h's rationale.

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace neomifes::ui {

struct GotoTarget {
    // 1-based, as typed by the user (Ctrl+G's convention, matching every
    // other editor's line/column display). Callers convert to this
    // project's 0-based document::LineNumber/column at the point of use.
    std::uint64_t                line   = 0;
    std::optional<std::uint64_t> column;  // nullopt: jump to line start only

    friend constexpr bool operator==(const GotoTarget&, const GotoTarget&) = default;
};

// Accepts "123" (line only) or "123:45" (line:column), both 1-based. Leading/
// trailing whitespace is not stripped - the caller (GotoLineBar's edit
// control) is expected to pass the raw text as-is; a query with stray
// whitespace simply fails to parse. Returns nullopt for anything else
// (empty input, non-numeric, line/column of 0 - "line 0" has no meaning in
// a 1-based input, unlike this project's internal 0-based LineNumber).
[[nodiscard]] inline std::optional<GotoTarget> parseGotoLineInput(std::u16string_view input) noexcept {
    if (input.empty()) {
        return std::nullopt;
    }
    const auto colonPos = input.find(u':');
    const std::u16string_view lineSpan = (colonPos == std::u16string_view::npos)
                                             ? input
                                             : input.substr(0, colonPos);
    if (lineSpan.empty()) {
        return std::nullopt;
    }

    // std::from_chars operates on char, not char16_t - every digit in a
    // valid input is ASCII, so a narrowing cast per character is safe (this
    // is purely numeric parsing, not general text - no UTF-16 handling
    // needed).
    char lineBuf[20];
    if (lineSpan.size() >= std::size(lineBuf)) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < lineSpan.size(); ++i) {
        if (lineSpan[i] > u'\x7f') {
            return std::nullopt;
        }
        lineBuf[i] = static_cast<char>(lineSpan[i]);
    }
    std::uint64_t line = 0;
    const auto    lineResult =
        std::from_chars(lineBuf, lineBuf + lineSpan.size(), line);
    if (lineResult.ec != std::errc{} || lineResult.ptr != lineBuf + lineSpan.size() || line == 0) {
        return std::nullopt;
    }

    if (colonPos == std::u16string_view::npos) {
        return GotoTarget{.line = line, .column = std::nullopt};
    }

    const std::u16string_view columnSpan = input.substr(colonPos + 1);
    if (columnSpan.empty() || columnSpan.size() >= std::size(lineBuf)) {
        return std::nullopt;
    }
    char columnBuf[20];
    for (std::size_t i = 0; i < columnSpan.size(); ++i) {
        if (columnSpan[i] > u'\x7f') {
            return std::nullopt;
        }
        columnBuf[i] = static_cast<char>(columnSpan[i]);
    }
    std::uint64_t column = 0;
    const auto    columnResult =
        std::from_chars(columnBuf, columnBuf + columnSpan.size(), column);
    if (columnResult.ec != std::errc{} || columnResult.ptr != columnBuf + columnSpan.size() ||
        column == 0) {
        return std::nullopt;
    }
    return GotoTarget{.line = line, .column = column};
}

}  // namespace neomifes::ui
