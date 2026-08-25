#pragma once

// buildGitPaneItems - converts Workspace::gitStatus()'s own
// std::optional<std::vector<git::GitStatusEntry>> (WI-17e) into
// ui::GitPaneItem rows. Header-only, pure, and free of Windows-SDK includes
// so it stays unit-testable without a live HWND, mirroring
// json_tree_bridge.h's buildJsonTreeItems() rationale - this lives under
// src/app/ rather than src/ui/ because it depends on neomifes::git, and
// ui:: is deliberately kept free of that dependency (see git_pane.h's own
// class comment).
//
// Flat 1:1 element-wise conversion (no tree/stack needed, unlike
// buildJsonTreeItems()) since GitStatusEntry is already a flat
// vector-of-files shape - closer in shape to git_diff_bridge.h's
// buildGitDiffMarkers() than to buildJsonTreeItems().
//
// The nullopt-vs-empty-vector distinction Workspace::gitStatus() itself
// cannot make (see workspace.h's own comment: both collapse to
// std::nullopt from GitStatusWorker's perspective for "never requested" -
// but gitStatus() ALSO uses std::nullopt for "requested, not a Git
// repository", while an empty (non-null) vector unambiguously means "a
// clean working tree") is resolved HERE, not in ui::GitPane itself -
// exactly the kind of "only the app layer needs to know the difference"
// split json_fold_bridge.h's own comment establishes for its own
// nullopt-collapsing case.

#include <optional>
#include <string>
#include <vector>

#include "neomifes/git/git_repository.h"
#include "neomifes/ui/git_pane.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::app {

namespace detail_git_pane_bridge {

[[nodiscard]] inline std::u16string glyphFor(git::GitFileStatus status) {
    switch (status) {
        case git::GitFileStatus::Modified:
            return u"M";
        case git::GitFileStatus::Added:
            return u"A";
        case git::GitFileStatus::Deleted:
            return u"D";
        case git::GitFileStatus::Renamed:
            return u"R";
        case git::GitFileStatus::Untracked:
            return u"U";
    }
    return u"M";  // unreachable - every GitFileStatus enumerator is handled above
}

}  // namespace detail_git_pane_bridge

[[nodiscard]] inline std::vector<ui::GitPaneItem> buildGitPaneItems(
    const std::optional<std::vector<git::GitStatusEntry>>& status) {
    using detail_git_pane_bridge::glyphFor;

    if (!status.has_value()) {
        return {ui::GitPaneItem{.statusGlyph = u"", .displayPath = u"Not a Git repository", .absolutePath = {}}};
    }
    if (status->empty()) {
        return {ui::GitPaneItem{.statusGlyph = u"", .displayPath = u"No changes", .absolutePath = {}}};
    }

    std::vector<ui::GitPaneItem> items;
    items.reserve(status->size());
    for (const git::GitStatusEntry& entry : *status) {
        items.push_back(ui::GitPaneItem{
            .statusGlyph  = glyphFor(entry.status),
            .displayPath  = std::u16string(util::fromWstringView(entry.relativePath.wstring())),
            .absolutePath = entry.absolutePath});
    }
    return items;
}

}  // namespace neomifes::app
