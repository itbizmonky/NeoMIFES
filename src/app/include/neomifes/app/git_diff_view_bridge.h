#pragma once

// buildDiffViewDocumentText/buildDiffViewLineMarkers - converts
// git::UnifiedDiffLine (WI-17f) into the two pieces the Diff view needs: the
// synthesized document's own full text, and render::DiffViewLineMarker
// ranges for the Added/Removed background tint. Header-only, pure, and free
// of Windows-SDK includes so both stay unit-testable without a live HWND,
// mirroring git_diff_bridge.h's buildGitDiffMarkers() rationale - lives
// under src/app/ rather than src/render/ for the SAME "RenderPipeline never
// depends on neomifes::git" reasoning that file's own header comment
// documents. A DIFFERENT bridge file (not an addition to git_diff_bridge.h)
// because the input/output shapes are unrelated: that file converts
// git::LineDiffRegion (hunk boundaries only) into render::GitDiffMarker for
// the live document's gutter; this one converts git::UnifiedDiffLine (every
// line's own text) into a synthesized document's full content plus
// render::DiffViewLineMarker ranges for its full-line background tint - see
// render_pipeline.h's own DiffViewLineMarker comment for why that is a
// deliberately separate type from GitDiffMarker, not a reinterpretation.

#include <string>
#include <vector>

#include "neomifes/git/git_repository.h"
#include "neomifes/render/render_pipeline.h"

namespace neomifes::app {

// Joins every line's own text with '\n' - the Diff view's synthesized
// document is built from this via Document::insertText(0, ...), the same
// "plain in-memory text, no file-load path" construction
// launch_setup.cpp's synthesizeMeasurementDocument() already establishes.
[[nodiscard]] inline std::u16string buildDiffViewDocumentText(const std::vector<git::UnifiedDiffLine>& lines) {
    std::u16string text;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        text += lines[i].text;
        if (i + 1 < lines.size()) {
            text += u'\n';
        }
    }
    return text;
}

// Compresses consecutive same-kind Added/Removed lines into one
// render::DiffViewLineMarker range each (Context lines produce no marker at
// all - there is nothing to tint). A flat single pass, not a tree walk
// (unlike buildJsonTreeItems()) - git::UnifiedDiffLine is already the same
// flat, ordered, one-entry-per-line shape the marker ranges are built from.
[[nodiscard]] inline std::vector<render::DiffViewLineMarker> buildDiffViewLineMarkers(
    const std::vector<git::UnifiedDiffLine>& lines) {
    std::vector<render::DiffViewLineMarker> markers;
    for (std::size_t i = 0; i < lines.size();) {
        const git::UnifiedDiffLineKind kind = lines[i].kind;
        if (kind == git::UnifiedDiffLineKind::Context) {
            ++i;
            continue;
        }
        const std::size_t runStart = i;
        while (i < lines.size() && lines[i].kind == kind) {
            ++i;
        }
        markers.push_back(render::DiffViewLineMarker{
            .startLine = static_cast<document::LineNumber>(runStart),
            .lineCount = static_cast<document::LineNumber>(i - runStart),
            .kind = kind == git::UnifiedDiffLineKind::Added ? render::GitDiffKind::Added : render::GitDiffKind::Deleted});
    }
    return markers;
}

}  // namespace neomifes::app
