#pragma once

// document_open - replaces the currently-open Document with a freshly
// loaded file, resetting every piece of session state scoped to "the
// document currently open" (Phase 5c2 - prerequisite for 5c3's Grep
// results pane and 5c4's tag jump, both of which need to open a file other
// than the one currently open and are this module's first real callers).
//
// Split out the same way editor_input.h was (see its file header): no
// HWND, no RenderPipeline, no FindBar - everything here is Win32/render-
// independent, so it stays headlessly unit-testable. The caller still owns
// two things this module cannot reach: RenderPipeline's cached bookmark/
// match-highlight visuals (stale entries would draw over unrelated content
// at the same line/byte positions in the new file) and FindBar's displayed
// match count. Whichever of 5c3/5c4 lands first must reset both immediately
// after calling this, before repainting - see the Phase 5c2 plan (or
// replaceAllMatches() in main.cpp, which already resets the same match-
// related state after a document mutation) for the exact reset sequence
// expected.

#include <cstdint>
#include <filesystem>
#include <optional>

#include "neomifes/document/file_loader.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::document {
class Document;
}  // namespace neomifes::document

namespace neomifes::core {
class BookmarkManager;
class CommandDispatcher;
class SelectionModel;
class Viewport;
}  // namespace neomifes::core

namespace neomifes::app {

// Loads `path` via document::loadUtf8File() and, on success, move-assigns
// the result into `document` (Document::operator=(Document&&) is already
// = default/noexcept, document.h - this is its first real call site, not
// new capability) and resets: undo/redo history
// (CommandDispatcher::resetUndoHistory() - a stale command's byte offsets
// are meaningless against unrelated content), bookmarks
// (BookmarkManager::clear() - a line number bookmarked in the old file has
// no relationship to the same line number in the new one), the Alt-click/
// rectangular-selection anchors, and free-cursor virtual-column state (all
// four are positions in the OLD document).
//
// Then moves the selection/viewport to (targetLine, targetColumn) - both
// already 0-based document coordinates (document::LineNumber's own
// convention, e.g. search::GrepMatch::line/columnRange - NOT
// ui::GotoTarget's 1-based Ctrl+G UI convention, since this module's actual
// callers pass already-0-based match positions) - or document start if
// targetLine is nullopt. Out-of-range values clamp rather than fail, same
// defensive convention as main.cpp's jumpToGotoTarget().
//
// Bundles "open" and "jump" into one call (rather than leaving the caller
// to follow up with a separate SelectionModel::moveAllTo()) so callers get
// one atomic "open this file and land on this line" step with no render
// free to land on a half-updated cursor in between.
//
// Returns the LoadError on failure, leaving `document` and every piece of
// session state above completely untouched (nullopt on success) -
// preserves loadUtf8File()'s existing error taxonomy for a future caller
// that wants to react to it (e.g. an error toast) rather than collapsing
// it to a bare bool now that the information is already there for free.
[[nodiscard]] std::optional<document::LoadError> openDocumentAt(
    const std::filesystem::path& path, std::optional<document::LineNumber> targetLine,
    std::optional<std::uint64_t> targetColumn, document::Document& document,
    core::CommandDispatcher& dispatcher, core::SelectionModel& selectionModel,
    core::Viewport& viewport, core::BookmarkManager& bookmarks,
    std::optional<document::TextPos>& altCursorAnchor,
    std::optional<document::TextPos>& rectangularAnchor,
    std::optional<std::uint32_t>&      freeCursorVirtualColumns);

}  // namespace neomifes::app
