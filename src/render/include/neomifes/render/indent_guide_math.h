#pragma once

// Pure, dependency-free helpers for indent-guide layout (Phase 7e). Header-
// only, no Windows-SDK includes, so they stay unit-testable without a live
// COM/device/DirectWrite stack (mirrors resize_math.h/viewport_math.h).

#include <cstdint>
#include <string_view>

namespace neomifes::render {

// Visual column of lineText's leading whitespace run (spaces/tabs only) -
// spaces advance by 1, tabs advance to the next tabWidth-multiple column.
// Matches core::computeIndentationConversionEdits()'s tab-stop convention
// (indentation_conversion.cpp) so guides line up with the editor's own
// tab<->space semantics, even though the two are independent implementations.
// Stops at the first non-whitespace character, or end of string. Returns 0
// if tabWidth is 0 (degenerate input, avoids a division by zero below).
[[nodiscard]] constexpr std::uint32_t computeIndentColumns(std::u16string_view lineText,
                                                            std::uint32_t       tabWidth) noexcept {
    if (tabWidth == 0) {
        return 0;
    }
    std::uint32_t column = 0;
    for (const char16_t ch : lineText) {
        if (ch == u' ') {
            ++column;
        } else if (ch == u'\t') {
            column = ((column / tabWidth) + 1) * tabWidth;
        } else {
            break;
        }
    }
    return column;
}

// Number of indent-guide lines to draw for a line whose leading whitespace
// spans indentColumns columns - one guide per completed tabWidth multiple
// (VSCode convention: a guide sits at every indent-level boundary, including
// the one the line's own code starts at). floor(indentColumns / tabWidth).
[[nodiscard]] constexpr std::uint32_t computeIndentGuideCount(std::uint32_t indentColumns,
                                                               std::uint32_t tabWidth) noexcept {
    return tabWidth == 0 ? 0 : indentColumns / tabWidth;
}

}  // namespace neomifes::render
