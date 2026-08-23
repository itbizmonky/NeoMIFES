#pragma once

// buildGitDiffMarkers - converts git::LineDiffRegion (WI-17a/b) into
// render::GitDiffMarker (WI-17c). Header-only, pure, and free of Windows-SDK
// includes so it stays unit-testable without a live HWND, mirroring
// json_tree_bridge.h's buildJsonTreeItems() rationale - this lives under
// src/app/ rather than src/render/ because RenderPipeline deliberately does
// not depend on neomifes::git (see render_pipeline.h's GitDiffMarker comment
// for the "independent, concurrently runnable engines" reasoning this bridge
// exists to preserve). A flat 1:1 element-wise conversion (no tree/stack
// needed, unlike buildJsonTreeItems()) since both types are already the same
// flat vector-of-hunks shape.

#include <vector>

#include "neomifes/git/git_repository.h"
#include "neomifes/render/render_pipeline.h"

namespace neomifes::app {

namespace detail_git_diff_bridge {

[[nodiscard]] inline render::GitDiffKind toRenderGitDiffKind(git::LineDiffKind kind) {
    switch (kind) {
        case git::LineDiffKind::Added:
            return render::GitDiffKind::Added;
        case git::LineDiffKind::Modified:
            return render::GitDiffKind::Modified;
        case git::LineDiffKind::Deleted:
            return render::GitDiffKind::Deleted;
    }
    return render::GitDiffKind::Added;  // unreachable - every LineDiffKind enumerator is handled above
}

}  // namespace detail_git_diff_bridge

[[nodiscard]] inline std::vector<render::GitDiffMarker> buildGitDiffMarkers(
    const std::vector<git::LineDiffRegion>& regions) {
    using detail_git_diff_bridge::toRenderGitDiffKind;

    std::vector<render::GitDiffMarker> markers;
    markers.reserve(regions.size());
    for (const git::LineDiffRegion& region : regions) {
        markers.push_back(render::GitDiffMarker{
            .startLine = region.startLine, .lineCount = region.lineCount, .kind = toRenderGitDiffKind(region.kind)});
    }
    return markers;
}

}  // namespace neomifes::app
