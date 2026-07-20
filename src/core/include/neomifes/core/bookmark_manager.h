#pragma once

// BookmarkManager - line bookmarks (Phase 4b8c, Ctrl+F2/F2/Shift+F2).
// Headless (no Win32/document dependency - operates purely on
// document::LineNumber) so it stays unit-testable, mirroring
// SelectionModel's separation.
//
// Known limitation (documented, not solved here): bookmarks do NOT track
// document edits. Inserting/deleting lines above a bookmarked line does not
// shift its stored LineNumber - this codebase has no edit-event/observer
// mechanism a BookmarkManager could subscribe to (Document exposes only a
// polled version() counter, see ADR-010's RenderPipeline-side precedent for
// the same "polled, not pushed" pattern). Re-evaluate if this becomes a
// real pain point in practice.

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "neomifes/document/text_pos.h"

namespace neomifes::core {

class BookmarkManager {
public:
    // Adds a bookmark at `line` if none exists there yet, otherwise removes
    // the existing one.
    void toggle(document::LineNumber line);

    // Discards every bookmark. A bookmarked line number is meaningless once
    // the underlying content changes to an unrelated file (Phase 5c2 - "open
    // a different file at runtime") - same "line numbers are only valid
    // against the content they were recorded against" theme as this class's
    // documented lack of edit-tracking above.
    void clear() noexcept;

    [[nodiscard]] bool isBookmarked(document::LineNumber line) const noexcept;

    // The nearest bookmarked line strictly after `from`, wrapping around to
    // the first bookmark if none is found past `from`. nullopt if no
    // bookmarks exist at all.
    [[nodiscard]] std::optional<document::LineNumber> next(document::LineNumber from) const noexcept;

    // The nearest bookmarked line strictly before `from`, wrapping around to
    // the last bookmark if none is found before `from`. nullopt if no
    // bookmarks exist at all.
    [[nodiscard]] std::optional<document::LineNumber> previous(document::LineNumber from) const noexcept;

    // Sorted ascending, no duplicates - the render layer walks this once per
    // visible line per frame (a handful of visible lines against typically
    // tens of bookmarks; same linear-scan shape as RenderPipeline's existing
    // m_cursorVisuals, not the unbounded-growth shape that motivated
    // docs/issues/match_highlight_linear_scan_scaling.md).
    [[nodiscard]] std::span<const document::LineNumber> lines() const noexcept { return m_lines; }

private:
    std::vector<document::LineNumber> m_lines;  // sorted ascending, no duplicates
};

}  // namespace neomifes::core
