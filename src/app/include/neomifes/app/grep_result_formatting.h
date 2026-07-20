#pragma once

// formatGrepResultRow - formats a single search::GrepMatch as the plain-text
// row GrepBar's WC_LISTBOX displays (Phase 5c3). Header-only and free of
// Windows-SDK includes so it stays unit-testable without a live HWND,
// mirroring ui::goto_line_parser.h/ui::find_navigation.h's rationale - this
// lives under src/app/ rather than src/ui/ because it depends on
// search::GrepMatch, and ui:: is deliberately kept free of neomifes::search
// (see grep_bar.h's class comment).

#include <string>

#include "neomifes/search/grep_service.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::app {

// "{path}({line+1}): {lineText}" - 1-based line number display (matches
// ui::parseGotoLineInput()'s Ctrl+G convention and every compiler/editor's
// display convention; GrepMatch::line itself is 0-based per grep_service.h).
// No truncation of long lineText - the listbox simply clips it, acceptable
// for a first implementation (no speculative wrapping/ellipsis logic).
[[nodiscard]] inline std::u16string formatGrepResultRow(const search::GrepMatch& match) {
    const std::wstring   pathWide = match.path.wstring();
    const std::u16string pathText(neomifes::util::fromWstringView(pathWide));
    const std::wstring   lineWide = std::to_wstring(match.line + 1);
    const std::u16string lineText(neomifes::util::fromWstringView(lineWide));

    std::u16string row;
    row.reserve(pathText.size() + lineText.size() + match.lineText.size() + 4);
    row += pathText;
    row += u'(';
    row += lineText;
    row += u"): ";
    row += match.lineText;
    return row;
}

}  // namespace neomifes::app
