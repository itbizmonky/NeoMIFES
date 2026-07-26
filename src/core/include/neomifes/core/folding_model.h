#pragma once

// FoldingModel - collapsible symbol regions (Phase 7i). Headless (no Win32/
// syntax dependency - operates purely on document::LineNumber), mirroring
// BookmarkManager's separation. Regions themselves come from outside (see
// neomifes::app::buildFoldRegions(), src/app/include/neomifes/app/
// fold_bridge.h) - this class only tracks which of the given regions are
// currently folded (collapsed) and answers line-visibility queries.
//
// Deliberately does NOT introduce a separate "display line" coordinate space
// (core::Viewport's header comment already predicted this class's arrival
// and stays folding-unaware; core::SelectionModel stays folding-unaware too
// - see the Phase 7i plan's Context section for why). document::LineNumber
// everywhere here still means "logical line number", exactly as elsewhere in
// this codebase; consumers (RenderPipeline, the movement/jump correction
// helpers in src/app) are responsible for skipping hidden lines using the
// queries below.
//
// Known limitation (documented, not solved here, same as BookmarkManager):
// regions do NOT track document edits between calls to
// setFoldableRegions() - this codebase has no edit-event/observer mechanism
// a FoldingModel could subscribe to. A region's headerLine/endLineInclusive
// can go stale after an edit until the next setFoldableRegions() call
// (Phase 7i: triggered once at file-open and again whenever the outline
// panel is refreshed - see main.cpp's refreshFoldingRegions()).

#include <optional>
#include <span>
#include <vector>

#include "neomifes/document/text_pos.h"

namespace neomifes::core {

struct FoldRegion {
    document::LineNumber headerLine;        // always visible, folded or not
    document::LineNumber endLineInclusive;  // last line hidden when folded
    bool                  folded = false;

    friend constexpr bool operator==(const FoldRegion&, const FoldRegion&) = default;
};

class FoldingModel {
public:
    // Replaces the foldable-region list, matching by headerLine against the
    // previous list to carry over each surviving region's folded state (a
    // region whose headerLine no longer appears in `regions` silently loses
    // its folded state - same "stale after edit, no magic recovery" theme as
    // this header's top comment). Callers are expected to have already
    // excluded single-line regions (endLineInclusive <= headerLine) - see
    // app::buildFoldRegions()'s contract.
    void setFoldableRegions(std::vector<FoldRegion> regions);

    // Flips the folded state of the region whose headerLine is `headerLine`.
    // No-op if no such region exists.
    void toggleFold(document::LineNumber headerLine) noexcept;

    // True if `line` sits strictly inside a currently-folded region (i.e. is
    // hidden from view) - never true for a region's own headerLine, which
    // stays visible whether folded or not.
    [[nodiscard]] bool isLineHidden(document::LineNumber line) const noexcept;

    [[nodiscard]] bool isFoldHeader(document::LineNumber line) const noexcept;

    [[nodiscard]] std::optional<FoldRegion> regionAt(document::LineNumber headerLine) const noexcept;

    // The folded region (if any) whose (headerLine, endLineInclusive] span
    // contains `line`. Used by movement-key correction (Phase 7i's
    // editor_input.cpp) to snap a cursor that landed inside hidden content
    // to the nearest boundary.
    [[nodiscard]] std::optional<FoldRegion> foldedRegionContaining(document::LineNumber line) const noexcept;

    // Unfolds every currently-folded region that hides `line`, so `line`
    // becomes visible. Returns true if anything actually changed (callers
    // use this to decide whether to re-push state to RenderPipeline).
    bool revealLine(document::LineNumber line) noexcept;

    [[nodiscard]] std::span<const FoldRegion> regions() const noexcept { return m_regions; }

private:
    std::vector<FoldRegion> m_regions;
};

}  // namespace neomifes::core
