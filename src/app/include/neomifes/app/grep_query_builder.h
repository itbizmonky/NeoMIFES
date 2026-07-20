#pragma once

// buildGrepQueryFromInput - builds a search::GrepQuery from GrepBar's raw
// folder-path and query text fields (Phase 5c3). Header-only, no filesystem
// I/O (a nonexistent root is already handled by search::GrepService::findAll()
// itself skipping it, grep_service.h - keeping this function pure means it
// stays unit-testable without touching disk) and free of Windows-SDK
// includes, mirroring ui::goto_line_parser.h/ui::find_navigation.h's
// rationale.

#include <optional>
#include <string_view>

#include "neomifes/search/grep_service.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::app {

// Trims both inputs; either trimming to empty means "not enough to search
// with" -> nullopt (there is no implicit default root/pattern in this
// codebase, and inventing one - e.g. cwd - would risk an unbounded,
// unconfirmed directory scan). On success: a single root built from the
// trimmed folder text (multi-root splitting is out of scope for 5c3),
// includeGlobs/excludeGlobs left empty (no filter-glob UI), and
// caseSensitive/wholeWord/regex left at search::Query's own defaults (no
// toggle UI).
[[nodiscard]] inline std::optional<search::GrepQuery> buildGrepQueryFromInput(
    std::u16string_view queryText, std::u16string_view folderText) {
    auto trim = [](std::u16string_view text) noexcept -> std::u16string_view {
        constexpr std::u16string_view kWhitespace = u" \t\r\n";
        const auto first = text.find_first_not_of(kWhitespace);
        if (first == std::u16string_view::npos) {
            return {};
        }
        const auto last = text.find_last_not_of(kWhitespace);
        return text.substr(first, last - first + 1);
    };

    const std::u16string_view trimmedQuery  = trim(queryText);
    const std::u16string_view trimmedFolder = trim(folderText);
    if (trimmedQuery.empty() || trimmedFolder.empty()) {
        return std::nullopt;
    }

    search::GrepQuery grepQuery;
    grepQuery.roots.emplace_back(neomifes::util::toWstringView(trimmedFolder));
    grepQuery.query.pattern = std::u16string(trimmedQuery);
    return grepQuery;
}

}  // namespace neomifes::app
