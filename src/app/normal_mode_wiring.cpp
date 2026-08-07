#include "neomifes/app/normal_mode_wiring.h"

#include <commctrl.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/app/document_open.h"
#include "neomifes/app/editor_input.h"
#include "neomifes/app/file_dialogs.h"
#include "neomifes/app/fold_bridge.h"
#include "neomifes/app/grep_query_builder.h"
#include "neomifes/app/grep_result_formatting.h"
#include "neomifes/app/message_dialogs.h"
#include "neomifes/app/outline_bridge.h"
#include "neomifes/app/syntax_language.h"
#include "neomifes/app/tag_jump.h"
#include "neomifes/core/bookmark_manager.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/folding_model.h"
#include "neomifes/core/indentation_conversion.h"
#include "neomifes/core/replace_all_command.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/document/file_saver.h"
#include "neomifes/platform/clipboard.h"
#include "neomifes/search/grep_service.h"
#include "neomifes/search/replacement.h"
#include "neomifes/search/search_service.h"
#include "neomifes/ui/command_descriptor.h"
#include "neomifes/ui/find_navigation.h"
#include "neomifes/ui/goto_line_parser.h"
#include "neomifes/util/tag_jump_parser.h"

namespace neomifes::app {

namespace {

using neomifes::core::Cursor;
using neomifes::core::FoldingModel;
using neomifes::core::FoldRegion;
using neomifes::core::IndentationConversionTarget;
using neomifes::core::MovementKind;
using neomifes::core::moveTextPos;
using neomifes::core::PerCursorEdit;
using neomifes::core::ReplaceAllCommand;
using neomifes::core::ReplaceRangeCommand;
using neomifes::core::SearchHistory;
using neomifes::core::SelectionModel;
using neomifes::core::Viewport;
using neomifes::document::Document;
using neomifes::document::LoadError;
using neomifes::document::TextRange;
using neomifes::render::FoldVisual;
using neomifes::render::MatchVisual;
using neomifes::render::RenderPipeline;
using neomifes::search::expandReplacementTemplate;
using neomifes::search::GrepMatch;
using neomifes::search::GrepService;
using neomifes::search::Match;
using neomifes::search::Query;
using neomifes::search::SearchService;
using neomifes::ui::CommandDescriptor;
using neomifes::ui::CommandPalette;
using neomifes::ui::CommandPaletteConfig;
using neomifes::ui::FindBar;
using neomifes::ui::FindBarConfig;
using neomifes::ui::GotoLineBar;
using neomifes::ui::GotoLineBarConfig;
using neomifes::ui::GrepBar;
using neomifes::ui::GrepBarConfig;
using neomifes::ui::MainWindow;
using neomifes::ui::MainWindowConfig;
using neomifes::ui::OutlinePane;
using neomifes::ui::OutlinePaneConfig;

// Bridges core::Viewport/SelectionModel state into RenderPipeline and
// requests a repaint - the shared tail of onKeyDown/onChar/onMouseWheel/
// onMouseDown below (Phase 4b1/4b2). RenderPipeline stays core-agnostic
// (setTopLine/setCursorVisuals take plain document types), so this glue
// lives here in the app layer rather than in either core or render.
// Phase 4b7a: builds one CursorVisual per SelectionModel cursor (not just
// the primary) so every cursor's caret/selection actually gets drawn.
// `primaryVirtualColumnOffset` (Phase 4b8e, default 0 - every existing call
// site is unaffected) carries the primary cursor's free-cursor virtual
// column count, if any, onto its CursorVisual; every other cursor always
// gets 0 (free cursor mode is single-primary-cursor only, per the approved
// plan). WI-04: takes EditorSession& instead of separate SelectionModel&/
// Viewport& refs - this is the shared tail of nearly every handler in this
// file, so every one of those call sites already has `session` in scope.
void syncRenderStateAndInvalidate(HWND hwnd, RenderPipeline& renderPipeline,
                                  const EditorSession& session,
                                  std::uint32_t primaryVirtualColumnOffset = 0) {
    const SelectionModel& selection = session.selection();
    const Viewport&        viewport  = session.viewport();
    renderPipeline.setTopLine(viewport.topLine());
    // WI-03: horizontal counterpart to setTopLine() above, same "driven
    // every frame from Viewport" wiring.
    renderPipeline.setLeftColumn(viewport.leftColumn());
    std::vector<neomifes::render::CursorVisual> visuals;
    visuals.reserve(selection.cursors().size());
    for (const auto& cursor : selection.cursors()) {
        visuals.push_back(neomifes::render::CursorVisual{
            .position       = cursor.position,
            .selectionRange = TextRange{.start = std::min(cursor.position, cursor.anchor),
                                        .end     = std::max(cursor.position, cursor.anchor)},
            .virtualColumnOffset = cursor.isPrimary ? primaryVirtualColumnOffset : 0,
            .isPrimary           = cursor.isPrimary,
        });
    }
    renderPipeline.setCursorVisuals(std::move(visuals));
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// WI-03: keeps the window's standard horizontal scrollbar (WS_HSCROLL) in
// sync with RenderPipeline/Viewport's own idea of the scroll range - range
// (nMax) from maxVisibleLineLength() (the visible-window approximation, see
// that method's comment), page size (nPage) from visibleColumnCount(), thumb
// position (nPos) from leftColumn(). Called once per successful render()
// (the paint handler below) rather than from every scroll-causing event
// individually - maxVisibleLineLength()/visibleColumnCount() are only
// current AFTER a render pass has walked the (possibly newly scrolled-to)
// visible lines, so re-deriving them any earlier would use stale values.
// WI-04: only ever needs Viewport (1 EditorSession member) - left as an
// individual parameter rather than EditorSession&, unlike
// syncRenderStateAndInvalidate() above.
void syncHorizontalScrollBar(HWND hwnd, const RenderPipeline& renderPipeline,
                             const Viewport& viewport) noexcept {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = static_cast<int>(renderPipeline.maxVisibleLineLength());
    si.nPage  = static_cast<UINT>(renderPipeline.visibleColumnCount());
    si.nPos   = static_cast<int>(viewport.leftColumn());
    ::SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
}

// Pushes FoldingModel's current region list into RenderPipeline as
// render::FoldVisual (Phase 7i) - the render:: mirror-type conversion every
// core:: session-state setter (setBookmarkedLines(), setCursorVisuals())
// already follows. Called after every FoldingModel mutation (toggle, or a
// fresh region list from refreshOutlinePane()/refreshFoldingRegions()).
// WI-04: only ever needs FoldingModel (1 EditorSession member) - left as an
// individual parameter, same rationale as syncHorizontalScrollBar() above.
void syncFoldingState(HWND hwnd, RenderPipeline& renderPipeline, const FoldingModel& foldingModel) {
    std::vector<FoldVisual> visuals;
    visuals.reserve(foldingModel.regions().size());
    for (const FoldRegion& region : foldingModel.regions()) {
        visuals.push_back(FoldVisual{
            .headerLine       = region.headerLine,
            .endLineInclusive = region.endLineInclusive,
            .folded           = region.folded,
        });
    }
    renderPipeline.setFoldRegions(std::move(visuals));
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Rebuilds RenderPipeline's match highlight set from `state.currentMatches`,
// marking `state.currentMatchIndex` as the "active" one (Phase 5b3a).
// Pulled out of runFindQuery()/navigateToMatch() since both need to do this
// identically. WI-04: only ever needs FindReplaceState (1 EditorSession
// member) - left as an individual parameter, same rationale as
// syncFoldingState() above.
void syncMatchVisuals(const FindReplaceState& state, RenderPipeline& renderPipeline) {
    std::vector<MatchVisual> visuals;
    visuals.reserve(state.currentMatches.size());
    for (std::size_t i = 0; i < state.currentMatches.size(); ++i) {
        visuals.push_back(MatchVisual{.range     = state.currentMatches[i].range,
                                      .isCurrent = (i == state.currentMatchIndex)});
    }
    renderPipeline.setMatchVisuals(std::move(visuals));
}

// Moves the selection/viewport to `state.currentMatches[state.currentMatchIndex]`
// and pushes the resulting state to FindBar/RenderPipeline (Phase 5b3a).
// Shared by runFindQuery() (jump to the first match after a new search) and
// navigateToMatch() (F3/Shift+F3) - both end up wanting exactly this. WI-04:
// takes EditorSession& (touches findReplaceState/selection/viewport/
// document - 4 members) instead of 4 separate refs.
void jumpToMatch(HWND hwnd, EditorSession& session, RenderPipeline& renderPipeline, FindBar& findBar) {
    const auto& state = session.findReplaceState();
    const Match& match = state.currentMatches[state.currentMatchIndex];
    session.selection().setCursors(
        {Cursor{.position = match.range.end, .anchor = match.range.start, .isPrimary = true}});
    session.viewport().ensureVisible(match.range.start, session.document());
    findBar.setMatchCount(state.currentMatchIndex, state.currentMatches.size());
    syncMatchVisuals(state, renderPipeline);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

// Runs SearchService::findAll() and updates `state.currentQuery`/
// `state.currentMatches`/`state.currentMatchIndex` (reset to 0) plus
// RenderPipeline's highlight set - but does NOT move the
// selection/viewport (Phase 5b3b: extracted from runFindQuery()'s previous
// body, which always jumped to match #0. replaceCurrentMatch() needs the
// search-and-update-state half without the jump, since it wants to land on
// "the match nearest the one just replaced", not unconditionally #0).
// WI-04: takes EditorSession& (document/findReplaceState).
void refreshMatches(const Query& query, EditorSession& session, RenderPipeline& renderPipeline,
                    FindBar& findBar) {
    auto& state              = session.findReplaceState();
    state.currentQuery      = query;
    state.currentMatches    = SearchService::findAll(session.document(), query);
    state.currentMatchIndex = 0;
    findBar.setMatchCount(state.currentMatchIndex, state.currentMatches.size());
    syncMatchVisuals(state, renderPipeline);
}

// Runs SearchService::findAll() for FindBar's onQueryChanged callback and
// jumps to the first match, if any (Phase 5b3a). An empty/no-match result
// clears all highlighting and shows FindBar's "no results" state. WI-04:
// takes EditorSession& (document/findReplaceState/selection/viewport).
void runFindQuery(std::u16string_view query, bool caseSensitive, bool wholeWord, bool regex, HWND hwnd,
                  EditorSession& session, RenderPipeline& renderPipeline, FindBar& findBar) {
    refreshMatches(Query{.pattern       = std::u16string(query),
                        .caseSensitive = caseSensitive,
                        .wholeWord     = wholeWord,
                        .regex         = regex},
                  session, renderPipeline, findBar);
    if (session.findReplaceState().currentMatches.empty()) {
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    jumpToMatch(hwnd, session, renderPipeline, findBar);
}

// F3 (forward=true) / Shift+F3 (forward=false), wrapping around - shared by
// FindBarConfig::onFindNext/onFindPrevious (fired while the find edit has
// focus) and the F3/Shift+F3 branch of handleFindBarKey() below (fired
// while the document editing area has focus instead) - same "one shared
// helper, two call sites" pattern as neomifes::app::dispatchMouseDown()/
// handleClipboardKey(). WI-04: takes EditorSession& (findReplaceState/
// selection/viewport/document).
void navigateToMatch(bool forward, HWND hwnd, EditorSession& session, RenderPipeline& renderPipeline,
                     FindBar& findBar) {
    auto& state = session.findReplaceState();
    if (state.currentMatches.empty()) {
        return;
    }
    state.currentMatchIndex = forward
        ? neomifes::ui::nextMatchIndex(state.currentMatchIndex, state.currentMatches.size())
        : neomifes::ui::previousMatchIndex(state.currentMatchIndex, state.currentMatches.size());
    jumpToMatch(hwnd, session, renderPipeline, findBar);
}

// Escape while the find edit has focus (FindBarConfig::onClosed) - hides
// the bar, clears match highlighting, and restores focus to the document
// editing area (FindBar itself does not know where that is). WI-04: takes
// EditorSession& (findReplaceState).
void closeFindBar(HWND hwnd, FindBar& findBar, EditorSession& session, RenderPipeline& renderPipeline) {
    findBar.hide();
    session.findReplaceState().currentMatches.clear();
    renderPipeline.setMatchVisuals({});
    ::SetFocus(hwnd);
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Ctrl+F (show) / F3 / Shift+F3 (navigate) while the document editing area
// has focus (not the find edit - see find_bar.h's class comment for why
// these same keys are ALSO handled inside FindBar's own subclass proc when
// the find edit itself has focus). Returns true if the key was one this
// handles, mirroring handleClipboardKey()'s ClipboardKeyResult.handled shape.
// WI-04: takes EditorSession& (findReplaceState/selection/viewport/document).
bool handleFindBarKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, FindBar& findBar,
                      EditorSession& session, RenderPipeline& renderPipeline) {
    // !shiftDown is redundant today (handleGrepKey() is checked earlier in
    // handleKeyDownEvent()'s dispatch chain and already claims Ctrl+Shift+F
    // via an early return), but makes this condition self-documenting and
    // safe against a future reordering of that chain (Phase 5c3).
    if (ctrlDown && !shiftDown && vkCode == 'F') {
        findBar.show();
        return true;
    }
    if (vkCode == VK_F3) {
        navigateToMatch(!shiftDown, hwnd, session, renderPipeline, findBar);
        return true;
    }
    return false;
}

// Ctrl+Shift+P while the document editing area has focus (Phase 5b3c) -
// mirrors handleFindBarKey()'s single-purpose shape. Not fired while the
// palette's own query edit has focus (same reasoning as handleFindBarKey's
// comment: Win32 routes keyboard input straight to the focused child HWND).
bool handleCommandPaletteKey(UINT vkCode, bool shiftDown, bool ctrlDown, CommandPalette& commandPalette) {
    if (ctrlDown && shiftDown && vkCode == 'P') {
        commandPalette.show();
        return true;
    }
    return false;
}

// Ctrl+Shift+F while the document editing area has focus (Phase 5c3) -
// mirrors handleCommandPaletteKey()'s single-purpose shape. Must be checked
// in handleKeyDownEvent()'s dispatch chain BEFORE handleFindBarKey():
// handleFindBarKey()'s own `ctrlDown && vkCode == 'F'` check does not look
// at shiftDown, so without this ordering Ctrl+Shift+F would already be
// swallowed by the plain Find bar before ever reaching this function.
bool handleGrepKey(UINT vkCode, bool shiftDown, bool ctrlDown, GrepBar& grepBar) {
    if (ctrlDown && shiftDown && vkCode == 'F') {
        grepBar.show();
        return true;
    }
    return false;
}

// Recomputes and displays the outline for the currently open document
// (Phase 7g) - called from handleOutlineKey() below whenever the panel is
// (re-)shown. Same "harmless empty result, no special-casing" convention as
// buildGrepQueryFromInput() on an empty query. Phase 7i: also refreshes
// FoldingModel's foldable-region list from the same parse (see
// extractCurrentOutline()'s comment) - existing folded state is preserved
// by FoldingModel::setFoldableRegions() matching on headerLine, same
// "stale after edit until next refresh" limitation as BookmarkManager.
// WI-04: takes EditorSession& (document/path/folding - 3 members).
void refreshOutlinePane(EditorSession& session, neomifes::ui::OutlinePane& outlinePane) {
    const auto nodes = neomifes::app::extractCurrentOutline(session.document(), session.pathIfNamed());
    outlinePane.showWith(neomifes::app::buildOutlineItems(nodes));
    session.folding().setFoldableRegions(neomifes::app::buildFoldRegions(nodes, session.document()));
}

// Ctrl+Shift+O while the document editing area has focus (Phase 7g) -
// unlike handleCommandPaletteKey()/handleGrepKey(), this TOGGLES (a second
// press while visible hides it) rather than only ever showing. An outline
// view is a persistent navigation aid the user dismisses with the same key
// they opened it with, not a one-shot search/command tool - see
// outline_pane.h's class comment. WI-04: takes EditorSession& (document/
// path/folding - 3 members).
bool handleOutlineKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, EditorSession& session,
                      neomifes::ui::OutlinePane& outlinePane, RenderPipeline& renderPipeline) {
    if (!ctrlDown || !shiftDown || vkCode != 'O') {
        return false;
    }
    if (outlinePane.isVisible()) {
        outlinePane.hide();
    } else {
        refreshOutlinePane(session, outlinePane);
        syncFoldingState(hwnd, renderPipeline, session.folding());
    }
    return true;
}

// OutlinePaneConfig::onItemSelected (Phase 7g) - unlike jumpToGotoTarget()/
// openDocumentAt(), no line/column conversion is needed: the targetPos
// OutlinePane echoes back is already a 0-based document::TextPos into the
// SAME open document, not a cross-file jump. The panel is deliberately left
// open afterward (see outline_pane.h's class comment) - this function never
// touches it. WI-04: takes EditorSession& (document/selection/viewport - 3
// members).
void jumpToOutlinePosition(std::uint64_t targetPos, HWND hwnd, EditorSession& session,
                           RenderPipeline& renderPipeline) {
    const auto pos =
        std::min(static_cast<neomifes::document::TextPos>(targetPos), session.document().length());
    session.selection().moveAllTo(pos);
    session.viewport().ensureVisible(pos, session.document());
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

// Ctrl+G while the document editing area has focus (Phase 4b8b) - same
// single-purpose shape as handleFindBarKey()/handleCommandPaletteKey().
bool handleGotoLineKey(UINT vkCode, bool ctrlDown, GotoLineBar& gotoLineBar) {
    if (ctrlDown && vkCode == 'G') {
        gotoLineBar.show();
        return true;
    }
    return false;
}

// Ctrl+F2 (toggle a bookmark on the current line) / F2 (jump to next
// bookmark) / Shift+F2 (jump to previous), Phase 4b8c. Mirrors
// handleGotoLineKey()'s single-purpose shape. `bookmarks.next()`/
// `previous()` already wrap around and return the nearest bookmark
// *strictly* after/before the current line, so re-pressing F2 while
// sitting on a bookmarked line correctly cycles to the next one rather than
// staying put. WI-04: takes EditorSession& (bookmarks/selection/viewport/
// document/folding - 5 members).
bool handleBookmarkKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, EditorSession& session,
                       RenderPipeline& renderPipeline) {
    if (vkCode != VK_F2) {
        return false;
    }
    const Document& document    = session.document();
    const auto      currentLine = document.offsetToLine(session.selection().primaryCursor().position);
    if (ctrlDown) {
        session.bookmarks().toggle(currentLine);
        renderPipeline.setBookmarkedLines(std::vector<neomifes::document::LineNumber>(
            session.bookmarks().lines().begin(), session.bookmarks().lines().end()));
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    const auto target =
        shiftDown ? session.bookmarks().previous(currentLine) : session.bookmarks().next(currentLine);
    if (target) {
        const auto pos = document.lineToOffset(*target);
        session.selection().moveAllTo(pos);
        session.viewport().ensureVisible(pos, document);
        // Phase 7i: a bookmark can land inside content that's since been
        // folded - reveal it rather than leaving the cursor on a hidden line.
        if (session.folding().revealLine(*target)) {
            syncFoldingState(hwnd, renderPipeline, session.folding());
        }
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    }
    return true;
}

// WI-02: the "the document just changed to a different file" reset,
// shared by every caller that swaps the session's document content out from
// under the view (F12 tag-jump, Grep-result-click, and Ctrl+O/drag-drop-
// open). Originally duplicated verbatim at the F12/Grep call sites (Phase
// 5c2-7i); a 3rd/4th caller needing the identical sequence is exactly this
// codebase's own "extract once 3+ callers exist" trigger (see
// visibleLineAtRow()/reservedTopHeightDips()'s own history).
//
// Deliberately does NOT reset dispatcher/bookmarks/anchors/
// freeCursorVirtualColumns - those are EditorSession::openFile()'s own
// internal responsibility when a file was actually loaded (WI-04: was
// document_open.cpp's openDocumentAt()'s responsibility pre-WI-04, now
// wrapped by EditorSession::openFile()). A "reset to blank" caller (Ctrl+N)
// never calls openFile() at all (there is no file to load) and MUST perform
// that half itself via EditorSession::resetToBlank() - see
// handleNewDocumentKey()'s own comment for the concrete data-corruption
// path (a stale Undo splicing the previous file's deleted text into the new
// blank document) that skipping it caused during WI-02's design review.
// WI-04: takes EditorSession& (folding/findReplaceState/language - 3
// members, was FoldingModel&/FindReplaceState&/optional<path> separately).
void resetViewAfterDocumentSwap(HWND hwnd, RenderPipeline& renderPipeline, EditorSession& session,
                                FindBar& findBar) {
    auto& findReplaceState = session.findReplaceState();
    findReplaceState.currentMatches.clear();
    findReplaceState.currentMatchIndex = 0;
    findBar.setMatchCount(0, 0);
    renderPipeline.setMatchVisuals({});
    renderPipeline.setBookmarkedLines({});
    session.folding().setFoldableRegions({});
    syncFoldingState(hwnd, renderPipeline, session.folding());
    renderPipeline.setLanguage(session.language());
    ::SetFocus(hwnd);
}

// WI-02: opens `path` into the session's Document via
// EditorSession::openFile() (optionally jumping to targetLine/targetColumn -
// both already 0-based, same convention that method documents), and on
// success runs resetViewAfterDocumentSwap() + a final repaint. Returns the
// LoadError on failure, leaving the session completely untouched (matches
// EditorSession::openFile()'s own no-partial-mutation-on-failure contract) -
// callers decide whether to surface it (F12/Grep-click keep their
// pre-existing silent no-op below; Ctrl+O/drag-drop-open show
// message_dialogs.h's showOpenErrorDialog()). WI-04: renamed from
// openAndResetTo() and collapsed from 17 parameters to 7 - most of its old
// body now lives inside EditorSession::openFile()/resetViewAfterDocumentSwap().
std::optional<LoadError> openFileAndSyncView(const std::filesystem::path& path,
                                             std::optional<neomifes::document::LineNumber> targetLine,
                                             std::optional<std::uint64_t> targetColumn, HWND hwnd,
                                             EditorSession& session, RenderPipeline& renderPipeline,
                                             FindBar& findBar) {
    auto error = session.openFile(path, targetLine, targetColumn);
    if (error) {
        return error;
    }
    resetViewAfterDocumentSwap(hwnd, renderPipeline, session, findBar);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    return std::nullopt;
}

// WI-02: shared save routine for Ctrl+S/Ctrl+Shift+S (and the "Save" choice
// of confirmDiscardIfDirty()'s unsaved-changes prompt below). `forceSaveAs`
// (or a document that has never been saved anywhere, i.e. `session.isUntitled()`)
// prompts via file_dialogs.h's showSaveFileDialog() for a destination first;
// otherwise reuses the existing path silently. Returns false on a cancelled
// dialog or a failed document::saveFile() call (in which case
// message_dialogs.h's showSaveErrorDialog() has already reported it to the
// user) - never silently swallows a failure (CLAUDE.md rule 3). WI-04:
// takes EditorSession& (document/fileState/path - 3 members) and calls
// EditorSession::setSavedPath() instead of assigning a local optional<path>
// directly.
bool performSave(HWND hwnd, EditorSession& session, bool forceSaveAs) {
    std::filesystem::path targetPath;
    if (forceSaveAs || session.isUntitled()) {
        const auto chosen = neomifes::app::showSaveFileDialog(hwnd, session.pathIfNamed());
        if (!chosen) {
            return false;  // dialog cancelled - not an error, just no-op
        }
        targetPath = *chosen;
    } else {
        targetPath = session.path();
    }
    const auto& fileState = session.fileState();
    const auto  error     = neomifes::document::saveFile(session.document(), targetPath, fileState.encoding,
                                                       fileState.lineEnding, fileState.writeBom);
    if (error) {
        neomifes::app::showSaveErrorDialog(hwnd, *error);
        return false;
    }
    session.setSavedPath(targetPath);
    return true;
}

// WI-02: the "may this destructive operation proceed" gate shared by
// Ctrl+O/Ctrl+N/drag-drop-open/WM_CLOSE. An already-clean document is
// always an immediate yes. Otherwise prompts via
// message_dialogs.h's showUnsavedChangesDialog(); the Save choice routes
// through performSave() above so a cancelled Save-As dialog or a failed
// save also blocks the destructive operation - unsaved work is never
// discarded behind a save that didn't actually happen. WI-04: takes
// EditorSession& (document/fileState/path - 3 members).
bool confirmDiscardIfDirty(HWND hwnd, EditorSession& session) {
    if (!session.isDirty()) {
        return true;
    }
    const std::wstring documentName =
        session.isUntitled() ? L"Untitled" : session.path().filename().wstring();
    switch (neomifes::app::showUnsavedChangesDialog(hwnd, documentName)) {
        case neomifes::app::UnsavedChangesChoice::Save:
            return performSave(hwnd, session, /*forceSaveAs=*/session.isUntitled());
        case neomifes::app::UnsavedChangesChoice::DontSave:
            return true;
        case neomifes::app::UnsavedChangesChoice::Cancel:
        default:
            return false;
    }
}

// F12 (Phase 5c4) - same "act on the cursor's current line" shape as
// handleBookmarkKey()'s F2 above, but here the line's TEXT (not just its
// number) is inspected: if it contains an MSVC-diagnostic-style location
// reference ("path(line)"/"path(line,column)", util::parseTagJumpReference()),
// opens that file via openFileAndSyncView() (WI-02/WI-04, wrapping
// EditorSession::openFile()) and jumps to the referenced position. Always
// returns true once vkCode==VK_F12 is confirmed - F12 is unclaimed
// everywhere else in this dispatch chain, so there is nothing to fall
// through to whether or not a reference was found/opened (same
// silent-no-op contract EditorSession::openFile() itself guarantees on a
// stale/missing path). WI-04: takes EditorSession& (essentially every
// member).
bool handleTagJumpKey(HWND hwnd, UINT vkCode, EditorSession& session, RenderPipeline& renderPipeline,
                      FindBar& findBar) {
    if (vkCode != VK_F12) {
        return false;
    }
    const Document& document  = session.document();
    const auto      cursorPos = session.selection().primaryCursor().position;
    const auto      line      = document.offsetToLine(cursorPos);
    const auto      lineStart = document.lineToOffset(line);
    const auto      lineEnd   = (line + 1 < document.lineCount()) ? document.lineToOffset(line + 1) - 1
                                                                  : document.length();
    const std::u16string lineText = document.snapshot()->extract(
        neomifes::document::TextRange{.start = lineStart, .end = lineEnd});

    const auto reference = neomifes::util::parseTagJumpReference(lineText);
    if (!reference) {
        return true;
    }
    const auto resolvedPath =
        neomifes::app::resolveTagJumpPath(reference->path, std::filesystem::current_path());
    const std::optional<std::uint64_t> targetColumn =
        reference->column ? std::optional<std::uint64_t>(*reference->column - 1) : std::nullopt;
    // stale/missing path - openFileAndSyncView() leaves everything untouched
    // on failure, same silent no-op as before this WI.
    (void)openFileAndSyncView(resolvedPath, reference->line - 1, targetColumn, hwnd, session,
                              renderPipeline, findBar);
    return true;
}

// Enter while the replace edit has focus (FindBarConfig::onReplaceCurrent,
// Phase 5b3b) - replaces state.currentMatches[state.currentMatchIndex] with
// `replacementTemplate` expanded against the match's capture groups, then
// re-runs state.currentQuery and jumps to whichever match now occupies the
// same index (clamped, since a replace can only ever remove exactly one
// match, so the count shrinks by at most 1 - see the plan's Context section
// for the out-of-bounds trace). WI-04: takes EditorSession& (document/
// dispatcher/findReplaceState/selection/viewport - 5 members).
void replaceCurrentMatch(std::u16string_view replacementTemplate, HWND hwnd, EditorSession& session,
                         RenderPipeline& renderPipeline, FindBar& findBar) {
    auto& state = session.findReplaceState();
    if (state.currentMatches.empty()) {
        return;
    }
    const std::size_t    replacedIndex = state.currentMatchIndex;
    const Match&           match         = state.currentMatches[replacedIndex];
    const std::u16string expanded = expandReplacementTemplate(replacementTemplate, session.document(), match);
    session.dispatcher().dispatch(std::make_unique<ReplaceRangeCommand>(match.range, expanded));

    refreshMatches(state.currentQuery, session, renderPipeline, findBar);
    if (state.currentMatches.empty()) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
        return;
    }
    state.currentMatchIndex = std::min(replacedIndex, state.currentMatches.size() - 1);
    jumpToMatch(hwnd, session, renderPipeline, findBar);
}

// Ctrl+Enter while the replace edit has focus (FindBarConfig::onReplaceAll,
// Phase 5b3b) - replaces every current match atomically as one undo step.
// state.currentMatches is already in ascending document order
// (SearchService::findAll()'s guarantee - search_service.h), matching
// applyEditsWithCumulativeShift()'s ordering requirement
// (cumulative_shift_edit.h) directly, so no re-sort is needed before
// building the PerCursorEdit vector. Each replacement's capture-group
// expansion is resolved against the pre-edit document (expandReplacementTemplate()'s
// contract, replacement.h) before any edit is applied.
//
// Does not re-search afterward: match highlighting is simply cleared, same
// as closeFindBar() - re-matching the just-replaced text against the same
// query would be confusing (looks like the replace silently didn't work)
// rather than informative. WI-04: takes EditorSession& (document/dispatcher/
// selection/findReplaceState - 4 members).
void replaceAllMatches(std::u16string_view replacementTemplate, HWND hwnd, EditorSession& session,
                       RenderPipeline& renderPipeline, FindBar& findBar) {
    auto& state = session.findReplaceState();
    if (state.currentMatches.empty()) {
        return;
    }
    const Document& document = session.document();
    std::vector<PerCursorEdit> edits;
    edits.reserve(state.currentMatches.size());
    for (const Match& match : state.currentMatches) {
        edits.push_back(PerCursorEdit{.range        = match.range,
                                      .insertedText = expandReplacementTemplate(replacementTemplate,
                                                                                document, match)});
    }
    const std::vector<Cursor> cursorsBefore(session.selection().cursors().begin(),
                                            session.selection().cursors().end());
    session.dispatcher().dispatch(std::make_unique<ReplaceAllCommand>(std::move(edits), cursorsBefore));

    state.currentMatches.clear();
    state.currentMatchIndex = 0;
    findBar.setMatchCount(0, 0);
    renderPipeline.setMatchVisuals({});
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Checks whether a WM_LBUTTONDOWN landed on a foldable gutter row and, if
// so, toggles that region and repaints, returning true so the caller skips
// its ordinary hitTest()/neomifes::app::dispatchMouseDown() cursor-placement
// path entirely (Phase 7j). Pulled out of wireNormalMode's onMouseDown
// lambda to keep that function's cognitive complexity down, same rationale
// as neomifes::app::dispatchMouseDown(). WI-04: only ever needs FoldingModel
// (1 EditorSession member) - left as an individual parameter.
bool tryToggleFoldMarker(HWND hwnd, std::int32_t x, std::int32_t y, RenderPipeline& renderPipeline,
                         FoldingModel& foldingModel) {
    const auto foldHeaderLine = renderPipeline.hitTestFoldMarker(x, y);
    if (!foldHeaderLine) {
        return false;
    }
    foldingModel.toggleFold(*foldHeaderLine);
    syncFoldingState(hwnd, renderPipeline, foldingModel);
    return true;
}

// Checks whether a WM_LBUTTONDOWN landed on the minimap strip and, if so,
// jumps the viewport there and starts drag tracking, returning true so the
// caller skips its ordinary hitTest()/dispatchMouseDown() cursor-placement
// path entirely (Phase 7v) - same "priority-check, consume, and return"
// shape as tryToggleFoldMarker() above. Does not touch SelectionModel: a
// minimap click is a scroll-position operation, not a cursor-placement one
// (matches the common convention other minimap-bearing editors use). WI-04:
// takes EditorSession& (viewport - 1 member) so it can reuse
// syncRenderStateAndInvalidate() rather than duplicating its body.
bool tryHandleMinimapClick(HWND hwnd, std::int32_t x, std::int32_t y, RenderPipeline& renderPipeline,
                           EditorSession& session, bool& isDraggingMinimap) {
    const auto targetLine = renderPipeline.hitTestMinimap(x, y);
    if (!targetLine) {
        return false;
    }
    isDraggingMinimap = true;
    session.viewport().scrollTo(*targetLine);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    return true;
}

// Handles WM_LBUTTONDOWN. Pulled out of wireNormalMode's onMouseDown lambda
// for the same cognitive-complexity reason as handleKeyDownEvent() above -
// Phase 7j's tryToggleFoldMarker() check pushed the inline version over
// clang-tidy's threshold. WI-04: takes EditorSession& (selection/viewport/
// document/altCursorAnchor/rectangularAnchor/freeCursorVirtualColumns/
// folding - 7 members).
void handleMouseDownEvent(HWND hwnd, std::int32_t x, std::int32_t y, bool shiftDown, bool altDown,
                          int clickCount, EditorSession& session, RenderPipeline& renderPipeline,
                          bool& isDraggingMinimap) {
    // Every new mouse-down is the start of a fresh gesture - this is the one
    // reliable reset point for this flag (MainWindow exposes no onMouseUp
    // hook; see isDraggingMinimap's declaration comment in wWinMain). Only
    // tryHandleMinimapClick() below sets it back to true.
    isDraggingMinimap = false;
    if (tryToggleFoldMarker(hwnd, x, y, renderPipeline, session.folding())) {
        return;
    }
    if (tryHandleMinimapClick(hwnd, x, y, renderPipeline, session, isDraggingMinimap)) {
        return;
    }
    const auto hit = renderPipeline.hitTest(x, y);
    if (!hit) {
        return;
    }
    // A click is always an "unrelated operation" for free-cursor virtual
    // columns (Phase 4b8e is keyboard-only) - discard silently; the click
    // itself always changes the selection, so the repaint below already
    // clears any stale virtual-offset caret.
    session.freeCursorVirtualColumns().reset();
    const bool changed = neomifes::app::dispatchMouseDown(
        *hit, shiftDown, altDown, clickCount, session.selection(), session.viewport(), session.document(),
        session.altCursorAnchor(), session.rectangularAnchor());
    if (changed) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    }
}

// Phase 4b8e (フリーカーソル簡略版): Right-arrow past the real end of the
// current line, while Free Cursor Mode is on and there is exactly one
// cursor with no active selection (deliberately narrow scope per the
// approved plan - no mouse support, no multi-cursor, no Shift-extend or
// Ctrl+Right word-jump into virtual space), increments a virtual column
// count instead of the usual "do nothing at end of line/document" behavior.
// No document mutation happens here - the virtual columns are session state
// until onChar materializes them (see applyFreeCursorChar() below) - so
// this only ever needs a repaint, never dispatcher.dispatch(). WI-04: takes
// EditorSession& (freeCursorVirtualColumns/selection/document/viewport - 4
// members); freeCursorModeEnabled stays a separate bool - it is a
// wWinMain-scope editing-mode toggle, not part of any one EditorSession
// (see this file's EditorSession member-placement notes).
bool handleFreeCursorRightArrow(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown,
                                bool freeCursorModeEnabled, EditorSession& session,
                                RenderPipeline& renderPipeline) {
    if (!freeCursorModeEnabled || vkCode != VK_RIGHT || shiftDown || ctrlDown ||
        session.selection().cursors().size() != 1) {
        return false;
    }
    const Cursor& cursor = session.selection().primaryCursor();
    if (cursor.hasSelection()) {
        return false;
    }
    const Document& document          = session.document();
    const auto      line              = document.offsetToLine(cursor.position);
    const auto      lineEndExclusive = (line + 1 < document.lineCount())
                                           ? document.lineToOffset(line + 1) - 1
                                           : document.length();
    if (cursor.position != lineEndExclusive) {
        return false;  // not at the real end of the line yet - normal movement applies
    }
    auto& virtualColumns = session.freeCursorVirtualColumns();
    virtualColumns        = virtualColumns.value_or(0) + 1;
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session, *virtualColumns);
    return true;
}

// Materializes free-cursor virtual columns (Phase 4b8e) once the user types
// a character while the caret sits past the real end of its line: inserts
// `virtualColumns` real spaces followed by `ch` in one edit, via the same
// single-range core::ReplaceRangeCommand dispatch replaceCurrentMatch()
// uses (Phase 5b3b) rather than neomifes::app::handleChar()'s
// insertTextAtEveryCursor() path - this is always exactly one pre-validated,
// selection-less cursor (handleFreeCursorRightArrow()'s guard), not the
// general multi-cursor case. Mirrors handleChar()'s own \r->\n translation
// and C0-control filter (Enter/Tab accepted, everything else below 0x20
// ignored - Backspace/Escape/etc. arrive via WM_KEYDOWN instead) since this
// bypasses handleChar() entirely rather than wrapping it. WI-04: takes
// EditorSession& (dispatcher/selection/viewport/document - 4 members).
void applyFreeCursorChar(wchar_t ch, std::uint32_t virtualColumns, HWND hwnd, EditorSession& session,
                         RenderPipeline& renderPipeline) {
    if (ch < 0x20 && ch != u'\r' && ch != u'\t') {
        return;
    }
    auto inserted = static_cast<char16_t>(ch);
    if (ch == u'\r') {
        inserted = u'\n';
    }
    std::u16string text(virtualColumns, u' ');
    text.push_back(inserted);
    const auto pos = session.selection().primaryCursor().position;
    session.dispatcher().dispatch(
        std::make_unique<ReplaceRangeCommand>(TextRange{.start = pos, .end = pos}, text));
    session.viewport().ensureVisible(session.selection().primaryCursor().position, session.document());
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

// Whether a Ctrl+C/X/V keystroke was recognized at all, and (only when it
// was) whether it changed the document/selection.
struct ClipboardKeyResult {
    bool handled = false;
    bool changed = false;
};

// Handles Ctrl+C/X/V (Phase 4b6c). Pulled out of wireNormalMode's onKeyDown
// lambda for the same cognitive-complexity reason as
// neomifes::app::dispatchMouseDown() above. Clipboard I/O is a Win32 API
// concern (src/platform/clipboard.h), so this lives here rather than inside
// neomifes::app::handleKeyDown() - editor_input.cpp is deliberately kept
// free of Win32 calls so it stays headlessly testable (see editor_input.h's
// file header). Applies to every cursor (Phase 4b7c) via
// textToCopy()/handlePaste()/deleteAllSelections(). WI-04: takes
// EditorSession& (dispatcher/selection/viewport/document - 4 members).
ClipboardKeyResult handleClipboardKey(HWND hwnd, UINT vkCode, bool ctrlDown, EditorSession& session) {
    if (!ctrlDown || (vkCode != 'C' && vkCode != 'X' && vkCode != 'V')) {
        return {};
    }
    const Document& document = session.document();
    if (vkCode == 'V') {
        const auto text = neomifes::platform::getClipboardText(hwnd);
        if (!text) {
            return {.handled = true, .changed = false};
        }
        neomifes::app::handlePaste(*text, session.dispatcher(), session.selection(), session.viewport(),
                                   document);
        return {.handled = true, .changed = true};
    }
    // Copy or Cut. If the clipboard write fails, don't delete any selection
    // for Cut either - that would destroy text the user never actually got
    // a copy of.
    const auto text = neomifes::app::textToCopy(session.selection(), document);
    if (!text || !neomifes::platform::setClipboardText(hwnd, *text)) {
        return {.handled = true, .changed = false};
    }
    if (vkCode == 'X') {
        const bool changed = neomifes::app::deleteAllSelections(session.dispatcher(), session.selection(),
                                                                 session.viewport(), document);
        return {.handled = true, .changed = changed};
    }
    return {.handled = true, .changed = false};
}

// Ctrl+S (save) / Ctrl+Shift+S (save as) (WI-02, build_plan.md §5, 🎉 M1).
// Single function for both - mirrors handleFindBarKey()'s Ctrl+F/F3
// combination - since performSave()'s forceSaveAs parameter is exactly
// `shiftDown`. Always returns true once Ctrl+S is confirmed (return value
// of performSave() itself is intentionally ignored here - no window-title
// "saved" indicator exists yet to update either way, out of scope for
// this WI). WI-04: takes EditorSession& (document/fileState/path - 3
// members).
bool handleSaveKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, EditorSession& session) {
    if (!ctrlDown || vkCode != 'S') {
        return false;
    }
    (void)performSave(hwnd, session, /*forceSaveAs=*/shiftDown);
    return true;
}

// Ctrl+O (WI-02) - confirmDiscardIfDirty() first (so an unsaved edit is
// never silently discarded by opening a different file), then
// file_dialogs.h's showOpenFileDialog() for the destination, then
// openFileAndSyncView() (WI-04, wrapping EditorSession::openFile()) same as
// F12/Grep-result-click. Unlike those two silent-no-op callers, a failed
// open here is user-INITIATED (not a stale background reference), so it is
// surfaced via message_dialogs.h's showOpenErrorDialog(). WI-04: takes
// EditorSession& (essentially every member).
bool handleOpenKey(HWND hwnd, UINT vkCode, bool ctrlDown, EditorSession& session,
                   RenderPipeline& renderPipeline, FindBar& findBar) {
    if (!ctrlDown || vkCode != 'O') {
        return false;
    }
    if (!confirmDiscardIfDirty(hwnd, session)) {
        return true;  // user cancelled the unsaved-changes prompt
    }
    const auto chosen = neomifes::app::showOpenFileDialog(hwnd);
    if (!chosen) {
        return true;  // Open dialog cancelled - nothing to do
    }
    const auto error =
        openFileAndSyncView(*chosen, std::nullopt, std::nullopt, hwnd, session, renderPipeline, findBar);
    if (error) {
        neomifes::app::showOpenErrorDialog(hwnd, *error);
    }
    return true;
}

// Ctrl+N (WI-02) - confirmDiscardIfDirty() first, then resets the session's
// Document to a fresh blank Document IN PLACE via EditorSession::resetToBlank()
// (move-assignment onto the existing object, not a locally-scoped
// replacement - CommandDispatcher and other collaborators were bound to
// this specific Document instance at construction and must keep pointing at
// it - see editor_session.h's own header comment). Ctrl+N never calls
// EditorSession::openFile() (there is no file to load), so it does not get
// that method's internal reset for free - resetToBlank() mirrors it
// explicitly (dispatcher.resetUndoHistory()/bookmarks.clear()/both
// selection anchors/freeCursorVirtualColumns, exactly as
// EditorSession::openFile() does after its own move-assignment). Skipping
// this was a real data-corruption path found during WI-02's design review:
// a stale Undo entry from the PREVIOUS document splices its deleted text
// into the new blank document's start on Ctrl+Z (PieceTable::insert()
// silently clamps an out-of-range offset to 0 rather than rejecting it) -
// see resetViewAfterDocumentSwap()'s own comment. WI-04: takes
// EditorSession& (essentially every member).
bool handleNewDocumentKey(HWND hwnd, UINT vkCode, bool ctrlDown, EditorSession& session,
                          RenderPipeline& renderPipeline, FindBar& findBar) {
    if (!ctrlDown || vkCode != 'N') {
        return false;
    }
    if (!confirmDiscardIfDirty(hwnd, session)) {
        return true;  // user cancelled the unsaved-changes prompt
    }
    session.resetToBlank();
    resetViewAfterDocumentSwap(hwnd, renderPipeline, session, findBar);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    return true;
}

// Handles WM_KEYDOWN end-to-end: Ctrl+C/X/V first (Phase 4b6c), falling
// through to the regular movement/edit/undo path otherwise. Pulled all the
// way out of wireNormalMode's onKeyDown lambda body (not just the branching
// logic) - a lambda defined inline inside wireNormalMode has its body
// counted toward wireNormalMode's own cognitive complexity even when the
// branching it does is itself delegated to helper functions, so leaving any
// nontrivial control flow in the lambda itself re-creates the problem
// neomifes::app::dispatchMouseDown()/handleClipboardKey() were extracted to
// avoid. WI-04: takes EditorSession& (all session-scoped state) plus the
// widget/mode references that are NOT part of any one EditorSession (see
// this file's EditorSession member-placement notes for why FindBar/
// CommandPalette/GotoLineBar/GrepBar/OutlinePane/freeCursorModeEnabled stay
// separate).
void handleKeyDownEvent(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, EditorSession& session,
                        RenderPipeline& renderPipeline, FindBar& findBar, CommandPalette& commandPalette,
                        GotoLineBar& gotoLineBar, GrepBar& grepBar, OutlinePane& outlinePane,
                        bool freeCursorModeEnabled) {
    if (handleFreeCursorRightArrow(hwnd, vkCode, shiftDown, ctrlDown, freeCursorModeEnabled, session,
                                   renderPipeline)) {
        return;
    }
    // Any other key discards a pending virtual-column count (Phase 4b8e) -
    // "無関係な操作で破棄" rule, same convention as altCursorAnchor/
    // rectangularAnchor above. Forces one repaint here (rather than relying
    // on whichever branch below happens to run) so the caret doesn't stay
    // visually stranded past the real end of the line when the branch that
    // does run turns out to be a no-op (e.g. Ctrl+Z with nothing to undo).
    if (session.freeCursorVirtualColumns().has_value()) {
        session.freeCursorVirtualColumns().reset();
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    }
    if (handleCommandPaletteKey(vkCode, shiftDown, ctrlDown, commandPalette)) {
        return;
    }
    if (handleGrepKey(vkCode, shiftDown, ctrlDown, grepBar)) {
        return;
    }
    if (handleOutlineKey(hwnd, vkCode, shiftDown, ctrlDown, session, outlinePane, renderPipeline)) {
        return;
    }
    if (handleGotoLineKey(vkCode, ctrlDown, gotoLineBar)) {
        return;
    }
    if (handleBookmarkKey(hwnd, vkCode, shiftDown, ctrlDown, session, renderPipeline)) {
        return;
    }
    if (handleTagJumpKey(hwnd, vkCode, session, renderPipeline, findBar)) {
        return;
    }
    if (handleSaveKey(hwnd, vkCode, shiftDown, ctrlDown, session)) {
        return;
    }
    if (handleOpenKey(hwnd, vkCode, ctrlDown, session, renderPipeline, findBar)) {
        return;
    }
    if (handleNewDocumentKey(hwnd, vkCode, ctrlDown, session, renderPipeline, findBar)) {
        return;
    }
    if (handleFindBarKey(hwnd, vkCode, shiftDown, ctrlDown, findBar, session, renderPipeline)) {
        return;
    }
    const auto clipboardResult = handleClipboardKey(hwnd, vkCode, ctrlDown, session);
    if (clipboardResult.handled) {
        if (clipboardResult.changed) {
            syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
        }
        return;
    }
    const bool changed =
        neomifes::app::handleKeyDown(vkCode, shiftDown, ctrlDown, session.dispatcher(), session.selection(),
                                     session.viewport(), session.document(), &session.folding());
    if (changed) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    }
}

// Handles WM_CHAR: free-cursor materialization (Phase 4b8e, applyFreeCursorChar()
// above) takes priority over the regular insert-at-every-cursor path
// (neomifes::app::handleChar()) whenever a virtual-column count is pending.
// Pulled out of wireNormalMode's onChar lambda for the same cognitive-
// complexity reason as handleKeyDownEvent() above. WI-04: takes
// EditorSession& (dispatcher/selection/viewport/document/
// freeCursorVirtualColumns - 5 members).
void handleCharEvent(HWND hwnd, wchar_t ch, EditorSession& session, RenderPipeline& renderPipeline) {
    auto& virtualColumns = session.freeCursorVirtualColumns();
    if (virtualColumns) {
        applyFreeCursorChar(ch, *virtualColumns, hwnd, session, renderPipeline);
        virtualColumns.reset();
        return;
    }
    const bool changed = neomifes::app::handleChar(ch, session.dispatcher(), session.selection(),
                                                   session.viewport(), session.document());
    if (changed) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    }
}

// WI-03: handles WM_HSCROLL (this window's first-ever scrollbar). Page step
// uses renderPipeline.visibleColumnCount() (falls back to 1 column if the
// text area hasn't been sized/measured yet, so PageLeft/PageRight always
// make forward progress instead of silently doing nothing). Extracted to a
// standalone function (not left inline in wireNormalMode's cfg.onHScroll
// lambda) - the scroll-code switch's nesting pushed wireNormalMode's own
// cognitive complexity over clang-tidy's threshold, same "pull the lambda
// body out" fix this codebase's other onXxx handlers already needed (see
// handleKeyDownEvent()/handleCharEvent() above). WI-04: takes
// EditorSession& (viewport/selection - 2 members), for consistency with
// syncRenderStateAndInvalidate()'s own EditorSession& shape.
void handleHScrollEvent(HWND hwnd, WORD scrollCode, WORD scrollPos, EditorSession& session,
                        RenderPipeline& renderPipeline) {
    const std::uint32_t pageStep = std::max<std::uint32_t>(renderPipeline.visibleColumnCount(), 1);
    const auto newColumn = neomifes::app::computeHScrollTargetColumn(
        scrollCode, scrollPos, session.viewport().leftColumn(), pageStep);
    if (!newColumn) {
        return;  // SB_ENDSCROLL etc - nothing to do
    }
    session.viewport().scrollToColumn(*newColumn);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

// Handles WM_SYSKEYDOWN (Phase 4b8g): Shift+Alt+arrows extends/starts a
// keyboard-driven rectangular selection, reusing `rectangularAnchor` - the
// same session state Shift+Alt+drag already established in Phase 4b8a (see
// neomifes::app::dispatchMouseDown()'s comment) - so a keyboard extension
// started this way can be continued by mouse or vice versa. Shift+Alt+I
// converts the current cursor/selection set into one cursor at the end of
// each spanned line (SelectionModel::convertToLineEndCursors()). Returns
// false for anything else (including plain Alt combos with no Shift) so
// MainWindow falls through to DefWindowProcW - see main_window.h's
// onSysKeyDown comment for why that fallthrough is mandatory (Alt+F4 etc.).
//
// Known limitation: unlike ordinary vertical movement (Viewport-independent
// column memory isn't tracked here), stepping through a shorter line with
// Shift+Alt+Up/Down does not remember the column to return to once a longer
// line follows - each step re-derives purely from the immediately preceding
// step's own (possibly already-clamped) column, same simplification the
// approved plan documents. WI-04: takes EditorSession& (selection/viewport/
// document/rectangularAnchor - 4 members).
bool handleSysKeyDownEvent(HWND hwnd, UINT vkCode, bool shiftDown, EditorSession& session,
                           RenderPipeline& renderPipeline) {
    if (!shiftDown) {
        return false;
    }
    const Document& document = session.document();
    if (vkCode == 'I') {
        session.selection().convertToLineEndCursors(document);
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
        return true;
    }
    MovementKind kind{};
    switch (vkCode) {
        case VK_LEFT:  kind = MovementKind::Left;  break;
        case VK_RIGHT: kind = MovementKind::Right; break;
        case VK_UP:    kind = MovementKind::Up;    break;
        case VK_DOWN:  kind = MovementKind::Down;  break;
        default:       return false;
    }
    auto& rectangularAnchor = session.rectangularAnchor();
    if (!rectangularAnchor) {
        rectangularAnchor = session.selection().primaryCursor().position;
    }
    const auto newActive = moveTextPos(kind, document, session.selection().primaryCursor().position);
    session.selection().setRectangularSelection(*rectangularAnchor, newActive, document);
    session.viewport().ensureVisible(newActive, document);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    return true;
}

// Builds the FindBarConfig callbacks (Phase 5b3a) - pulled out of
// wireNormalMode's onDeferredInit lambda for the same cognitive-complexity
// reason documented above handleKeyDownEvent(). All captured references
// outlive the returned FindBarConfig (they are wWinMain-scope locals; the
// config itself is only used immediately, inside findBar.create()). WI-04:
// takes EditorSession& (document/dispatcher/selection/viewport/
// findReplaceState - 5 members).
FindBarConfig buildFindBarConfig(HWND hwnd, EditorSession& session, RenderPipeline& renderPipeline,
                                 FindBar& findBar, SearchHistory& searchHistory) {
    FindBarConfig config{};
    config.onQueryChanged = [hwnd, &session, &renderPipeline, &findBar](std::u16string_view query,
                                                                        bool caseSensitive, bool wholeWord,
                                                                        bool regex) {
        runFindQuery(query, caseSensitive, wholeWord, regex, hwnd, session, renderPipeline, findBar);
    };
    // Recording happens here (Enter/F3 while the find edit itself has
    // focus), not inside navigateToMatch() - this is the one call site that
    // covers "the user typed a query and asked to act on it" for every
    // realistic flow (Ctrl+F -> type -> Enter). Subsequent F3 presses after
    // focus has moved to the document (handleFindBarKey()) or via the
    // command palette's Find Next/Previous re-record the SAME
    // already-recorded query - record()'s dedupe makes that a harmless
    // no-op move-to-front rather than a second entry, so those other call
    // sites don't also need searchHistory threaded through them (Phase 5c5).
    config.onHistoryOlder = [&findBar, &searchHistory](std::u16string_view currentText) {
        if (const auto older = searchHistory.older(currentText)) {
            findBar.setQueryText(*older);
        }
    };
    config.onHistoryNewer = [&findBar, &searchHistory](std::u16string_view currentText) {
        if (const auto newer = searchHistory.newer(currentText)) {
            findBar.setQueryText(*newer);
        }
    };
    config.onFindNext = [hwnd, &session, &renderPipeline, &findBar, &searchHistory]() {
        searchHistory.record(session.findReplaceState().currentQuery.pattern);
        navigateToMatch(true, hwnd, session, renderPipeline, findBar);
    };
    config.onFindPrevious = [hwnd, &session, &renderPipeline, &findBar, &searchHistory]() {
        searchHistory.record(session.findReplaceState().currentQuery.pattern);
        navigateToMatch(false, hwnd, session, renderPipeline, findBar);
    };
    config.onClosed = [hwnd, &findBar, &session, &renderPipeline]() {
        closeFindBar(hwnd, findBar, session, renderPipeline);
    };
    config.onReplaceCurrent = [hwnd, &session, &renderPipeline,
                               &findBar](std::u16string_view replacementText) {
        replaceCurrentMatch(replacementText, hwnd, session, renderPipeline, findBar);
    };
    config.onReplaceAll = [hwnd, &session, &renderPipeline, &findBar](std::u16string_view replacementText) {
        replaceAllMatches(replacementText, hwnd, session, renderPipeline, findBar);
    };
    return config;
}

// Builds the command palette's static registry (Phase 5b3c, extended in
// Phase 4b8d/4b8e) - 9 entries, each re-exposing an already-implemented
// keybinding or document-wide action through the palette (Find/Find+Replace/
// Find Next/Find Previous/Undo/Redo/Convert Tabs to Spaces/Convert Spaces to
// Tabs/Toggle Free Cursor Mode). None of the last 3 has a dedicated
// keybinding - the palette is their only entry point, same as any editor's
// "no default shortcut, command palette only" commands. Deliberately does
// not invent commands for features this project hasn't built yet (File
// Open/Save has no runtime UI - see this file's header comment), matching
// CLAUDE.md rule 3 (no speculative implementation). Pulled out of
// wireNormalMode's onDeferredInit lambda for the same cognitive-complexity
// reason documented above handleKeyDownEvent(). WI-04: takes EditorSession&
// (dispatcher/findReplaceState/selection/viewport/document/folding/
// freeCursorVirtualColumns - 7 members); freeCursorModeEnabled stays
// separate (see handleFreeCursorRightArrow()'s comment on why).
std::vector<CommandDescriptor> buildCommandRegistry(HWND hwnd, FindBar& findBar, EditorSession& session,
                                                     RenderPipeline& renderPipeline,
                                                     bool& freeCursorModeEnabled) {
    std::vector<CommandDescriptor> commands;
    commands.push_back(CommandDescriptor{.id              = u"find.show",
                                         .title           = u"Find",
                                         .keybindingLabel = u"Ctrl+F",
                                         .action          = [&findBar]() { findBar.show(); }});
    commands.push_back(
        CommandDescriptor{.id              = u"find.replace",
                          .title           = u"Find and Replace",
                          .keybindingLabel = u"Ctrl+H",
                          .action          = [&findBar]() { findBar.showWithReplace(); }});
    commands.push_back(CommandDescriptor{
        .id = u"find.next", .title = u"Find Next", .keybindingLabel = u"F3",
        .action = [hwnd, &session, &renderPipeline, &findBar]() {
            navigateToMatch(true, hwnd, session, renderPipeline, findBar);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"find.previous", .title = u"Find Previous", .keybindingLabel = u"Shift+F3",
        .action = [hwnd, &session, &renderPipeline, &findBar]() {
            navigateToMatch(false, hwnd, session, renderPipeline, findBar);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.undo", .title = u"Undo", .keybindingLabel = u"Ctrl+Z",
        .action = [hwnd, &session, &renderPipeline]() {
            if (session.dispatcher().undo()) {
                syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
            }
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.redo", .title = u"Redo", .keybindingLabel = u"Ctrl+Y",
        .action = [hwnd, &session, &renderPipeline]() {
            if (session.dispatcher().redo()) {
                syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
            }
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.convertTabsToSpaces", .title = u"Convert Tabs to Spaces", .keybindingLabel = u"",
        .action = [hwnd, &session]() {
            if (neomifes::app::applyIndentationConversion(IndentationConversionTarget::TabsToSpaces,
                                                          session.document(), session.dispatcher(),
                                                          session.selection())) {
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.convertSpacesToTabs", .title = u"Convert Spaces to Tabs", .keybindingLabel = u"",
        .action = [hwnd, &session]() {
            if (neomifes::app::applyIndentationConversion(IndentationConversionTarget::SpacesToTabs,
                                                          session.document(), session.dispatcher(),
                                                          session.selection())) {
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
        }});
    commands.push_back(CommandDescriptor{
        .id = u"view.toggleFoldAtCursor", .title = u"Fold/Unfold at Cursor", .keybindingLabel = u"",
        // Phase 7i: v1 requires the primary cursor to sit exactly on a fold
        // header line - no-op otherwise (see the Phase 7i plan's Context
        // point 6 for why gutter-click toggling is deferred to a later
        // sub-phase; this command is the only way to toggle a fold for now).
        .action = [hwnd, &session, &renderPipeline]() {
            const auto line = session.document().offsetToLine(session.selection().primaryCursor().position);
            if (!session.folding().isFoldHeader(line)) {
                return;
            }
            session.folding().toggleFold(line);
            syncFoldingState(hwnd, renderPipeline, session.folding());
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.toggleFreeCursorMode", .title = u"Toggle Free Cursor Mode",
        .keybindingLabel = u"",
        .action = [hwnd, &renderPipeline, &session, &freeCursorModeEnabled]() {
            freeCursorModeEnabled = !freeCursorModeEnabled;
            // Turning the mode off (or back on) mid-way through a pending
            // virtual-column count would otherwise leave the caret rendered
            // past the real end of the line with nothing left able to
            // materialize or reset it - same "discard on unrelated action"
            // rule as handleKeyDownEvent()'s reset above.
            if (session.freeCursorVirtualColumns()) {
                session.freeCursorVirtualColumns().reset();
                syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
            }
        }});
    return commands;
}

// Parses and applies a GotoLineBar submission (Phase 4b8b). Both `target.line`
// and `target.column` are 1-based (Ctrl+G's user-facing convention, per
// goto_line_parser.h); converted to this project's 0-based LineNumber/column
// here at the single point of use. An out-of-range line clamps to the last
// line (same "never throw on a stale/bad user input" convention as
// SelectionModel's own clamping); a column beyond the target line's actual
// length clamps to that line's end. WI-04: takes EditorSession& (document/
// selection/viewport/folding - 4 members).
void jumpToGotoTarget(const neomifes::ui::GotoTarget& target, HWND hwnd, EditorSession& session,
                      RenderPipeline& renderPipeline) {
    const Document& document = session.document();
    const auto lastLine  = document.lineCount() > 0 ? document.lineCount() - 1 : 0;
    const auto line      = std::min(target.line - 1, lastLine);
    const auto lineStart = document.lineToOffset(line);
    const auto lineEnd =
        (line + 1 < document.lineCount()) ? document.lineToOffset(line + 1) - 1 : document.length();
    const auto column = target.column.value_or(1) - 1;
    const auto pos     = std::min(lineStart + column, lineEnd);

    session.selection().moveAllTo(pos);
    session.viewport().ensureVisible(pos, document);
    // Phase 7i: Ctrl+G can target a line that's since been folded - reveal it
    // (same document, so unlike the cross-file jumps this can genuinely
    // still be a real, meaningful line to unfold rather than clear).
    if (session.folding().revealLine(line)) {
        syncFoldingState(hwnd, renderPipeline, session.folding());
    }
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

// Builds the GotoLineBarConfig callbacks (Phase 4b8b) - same extraction
// rationale as buildFindBarConfig()/buildCommandRegistry() above. WI-04:
// takes EditorSession& (document/selection/viewport/folding - 4 members).
GotoLineBarConfig buildGotoLineBarConfig(HWND hwnd, EditorSession& session, RenderPipeline& renderPipeline,
                                         GotoLineBar& gotoLineBar) {
    GotoLineBarConfig config{};
    config.onSubmit = [hwnd, &session, &renderPipeline, &gotoLineBar](std::u16string_view input) {
        const auto target = neomifes::ui::parseGotoLineInput(input);
        if (target) {
            jumpToGotoTarget(*target, hwnd, session, renderPipeline);
        }
        gotoLineBar.hide();
        ::SetFocus(hwnd);
    };
    config.onClosed = [hwnd, &gotoLineBar]() {
        gotoLineBar.hide();
        ::SetFocus(hwnd);
    };
    return config;
}

void runGrepQuery(std::u16string_view queryText, std::u16string_view folderText, GrepState& grepState,
                  GrepBar& grepBar, SearchHistory& searchHistory) {
    // Enter is GrepBar's only trigger (no debounce/live-refresh - see
    // grep_bar.h's class comment), so this single call site covers every
    // way a Grep query actually runs (Phase 5c5).
    searchHistory.record(queryText);
    const auto query = neomifes::app::buildGrepQueryFromInput(queryText, folderText);
    grepState.currentResults = query ? GrepService::findAll(*query) : std::vector<GrepMatch>{};

    std::vector<std::u16string> rows;
    rows.reserve(grepState.currentResults.size());
    for (const auto& match : grepState.currentResults) {
        rows.push_back(neomifes::app::formatGrepResultRow(match));
    }
    grepBar.setResults(rows);
}

// GrepBarConfig::onResultActivated (Phase 5c3) - opens the file a Grep
// result points to via openFileAndSyncView() (WI-04, wrapping
// EditorSession::openFile()), then performs the reset sequence that
// method's header comment explicitly leaves to the caller: RenderPipeline's
// cached match/bookmark visuals and FindBar's match count are separate from
// what EditorSession::openFile() itself already resets internally (undo
// history, BookmarkManager, both selection anchors, free-cursor virtual
// columns). Mirrors replaceAllMatches()'s reset sequence above. WI-04:
// takes EditorSession& (essentially every member); grepState stays separate
// (document-independent, see runGrepQuery()'s comment).
void jumpToGrepResult(std::size_t resultIndex, HWND hwnd, const GrepState& grepState, EditorSession& session,
                      RenderPipeline& renderPipeline, FindBar& findBar) {
    if (resultIndex >= grepState.currentResults.size()) {
        return;
    }
    const GrepMatch& match = grepState.currentResults[resultIndex];
    // Stale result (file moved/deleted since the Grep ran) - openFileAndSyncView()
    // leaves everything untouched on failure, same silent no-op contract as
    // before this WI. No error-toast UI exists yet to surface this.
    (void)openFileAndSyncView(match.path, match.line, match.columnRange.start, hwnd, session,
                              renderPipeline, findBar);
}

// Builds the GrepBarConfig callbacks (Phase 5c3) - same extraction rationale
// as buildFindBarConfig()/buildGotoLineBarConfig() above. WI-04: takes
// EditorSession& for the document-scoped state jumpToGrepResult() needs;
// grepState/searchHistory stay separate (Workspace-wide, not
// document-scoped).
GrepBarConfig buildGrepBarConfig(HWND hwnd, EditorSession& session, RenderPipeline& renderPipeline,
                                 FindBar& findBar, GrepBar& grepBar, GrepState& grepState,
                                 SearchHistory& searchHistory) {
    GrepBarConfig config{};
    config.onRunQuery = [&grepState, &grepBar, &searchHistory](std::u16string_view queryText,
                                                                std::u16string_view folderText) {
        runGrepQuery(queryText, folderText, grepState, grepBar, searchHistory);
    };
    config.onHistoryOlder = [&grepBar, &searchHistory](std::u16string_view currentText) {
        if (const auto older = searchHistory.older(currentText)) {
            grepBar.setQueryText(*older);
        }
    };
    config.onHistoryNewer = [&grepBar, &searchHistory](std::u16string_view currentText) {
        if (const auto newer = searchHistory.newer(currentText)) {
            grepBar.setQueryText(*newer);
        }
    };
    config.onResultActivated = [hwnd, &grepState, &session, &renderPipeline,
                                &findBar](std::size_t resultIndex) {
        jumpToGrepResult(resultIndex, hwnd, grepState, session, renderPipeline, findBar);
    };
    config.onClosed = [hwnd, &grepBar]() {
        grepBar.hide();
        ::SetFocus(hwnd);
    };
    return config;
}

// Builds OutlinePaneConfig, creates the panel, and - if that succeeds -
// primes its initial position/size (Phase 7g bug fix, discovered via
// EnumChildWindows during visual verification): onDeferredInit runs via a
// POSTED message, strictly after the WM_SIZE that fires as part of
// MainWindow::create()'s own ShowWindow() call, so by the time it runs that
// one-off WM_SIZE has already come and gone and cfg.onResize (hence
// outlinePane.onParentResized()) will not fire again until the user manually
// resizes the window. Without the explicit priming call here, the panel
// stays stuck at its CreateWindowExW-time placeholder rect (0,0,10,10) the
// first time it's shown. (The four other overlays created in
// wireNormalMode()'s onDeferredInit below have this same latent gap; fixing
// them is out of scope here - see the Phase 7g completion notes.) Pulled out
// to its own function - same cognitive-complexity reason as
// handleKeyDownEvent()/handleCharEvent() above - rather than left inline in
// onDeferredInit's already-long lambda body. WI-04: takes EditorSession&
// (document/selection/viewport - 3 members).
void createAndPositionOutlinePane(HWND hwnd, HINSTANCE hInstance, EditorSession& session,
                                  RenderPipeline& renderPipeline, OutlinePane& outlinePane) {
    OutlinePaneConfig config{};
    config.onItemSelected = [hwnd, &session, &renderPipeline](std::uint64_t targetPos) {
        jumpToOutlinePosition(targetPos, hwnd, session, renderPipeline);
    };
    config.onClosed = [hwnd]() { ::SetFocus(hwnd); };
    if (!outlinePane.create(hwnd, hInstance, config)) {
        return;
    }
    RECT clientRect{};
    ::GetClientRect(hwnd, &clientRect);
    const auto dpiScale = static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0F;
    outlinePane.onParentResized(static_cast<std::uint32_t>(clientRect.right),
                                static_cast<std::uint32_t>(clientRect.bottom), dpiScale);
}

// WI-02: cfg.onDropFiles body, pulled out of wireNormalMode() for the same
// cognitive-complexity reason as handleMouseDownEvent()/handleKeyDownEvent()
// above. Multi-file drops open only the first path (no multi-tab yet, see
// build_plan.md's explicit WI-02 scope cut - multi-tab is WI-05). WI-04:
// takes EditorSession& (essentially every member).
void handleDropFilesEvent(HWND hwnd, std::vector<std::wstring> paths, EditorSession& session,
                          RenderPipeline& renderPipeline, FindBar& findBar) {
    if (paths.empty()) {
        return;
    }
    if (!confirmDiscardIfDirty(hwnd, session)) {
        return;
    }
    const auto error = openFileAndSyncView(paths.front(), std::nullopt, std::nullopt, hwnd, session,
                                           renderPipeline, findBar);
    if (error) {
        neomifes::app::showOpenErrorDialog(hwnd, *error);
    }
}

}  // namespace

// No logging engine exists yet (basic_design.md sec.6.5 is a later phase);
// this is a deliberate, narrowly-scoped stopgap for render-attach/resize
// failures rather than solving logging prematurely. describe()'s output is
// documented ASCII-only, so OutputDebugStringA (not the W variant) is fine.
// WI-04: moved here from main.cpp - both wireNormalMode() below and
// main.cpp's own wireMeasureFrameMode() call it, so this single shared
// definition avoids duplicating it across both files.
void debugLogRenderError(const char* what, const render::RenderError& err) noexcept {
#ifndef NDEBUG
    const std::string msg = std::string(what) + ": " + neomifes::render::describe(err) + "\n";
    ::OutputDebugStringA(msg.c_str());
#else
    (void)what;
    (void)err;
#endif
}

// Real launches only - deferred so it never affects firstPaintNs timing
// (ADR-009). If attach() fails, the window simply keeps the GDI placeholder
// forever; there is no retry policy. Same non-fatal treatment for
// findBar.create() (Phase 5b3a) - a Find bar that fails to create simply
// isn't available this session, no retry policy either. WI-04: takes
// EditorSession& instead of the ~15 separate refs it used to (document/
// dispatcher/selection/viewport/altCursorAnchor/rectangularAnchor/
// bookmarks/foldingModel/freeCursorVirtualColumns/findReplaceState); the
// remaining individual parameters (findBar/commandPalette/gotoLineBar/
// grepBar/grepState/searchHistory/outlinePane/freeCursorModeEnabled/
// isDraggingMinimap) are Workspace-wide or process-wide state that stays
// outside any one EditorSession - see this file's EditorSession
// member-placement notes.
void wireNormalMode(MainWindowConfig& cfg, MainWindow& window, RenderPipeline& renderPipeline,
                    EditorSession& session, HINSTANCE hInstance, FindBar& findBar,
                    CommandPalette& commandPalette, GotoLineBar& gotoLineBar, GrepBar& grepBar,
                    GrepState& grepState, SearchHistory& searchHistory, OutlinePane& outlinePane,
                    bool& freeCursorModeEnabled, bool& isDraggingMinimap) {
    cfg.onDeferredInit = [&window, &renderPipeline, &session, hInstance, &findBar, &commandPalette,
                          &gotoLineBar, &grepBar, &grepState, &searchHistory, &outlinePane,
                          &freeCursorModeEnabled](HWND hwnd) {
        const auto attached = renderPipeline.attach(hwnd);
        if (!attached) {
            debugLogRenderError("RenderPipeline::attach", attached.error());
            return;
        }
        renderPipeline.setDocument(&session.document());
        window.setPaintHandler([&renderPipeline, &session](HWND paintHwnd) {
            const auto rendered = renderPipeline.render();
            if (!rendered) {
                debugLogRenderError("RenderPipeline::render", rendered.error());
                return;
            }
            // WI-03: kept fresh every successful frame rather than only on
            // WM_SIZE - m_charWidthDips (which visibleColumnCount() depends
            // on) isn't measured until the FIRST render() call completes, so
            // relying on resize() alone would leave this at 0 until the user
            // manually resized the window. See syncHorizontalScrollBar()'s
            // comment for why the scrollbar sync itself belongs here too.
            session.viewport().setVisibleColumnCount(renderPipeline.visibleColumnCount());
            syncHorizontalScrollBar(paintHwnd, renderPipeline, session.viewport());
        });
        const FindBarConfig findBarConfig =
            buildFindBarConfig(hwnd, session, renderPipeline, findBar, searchHistory);
        [[maybe_unused]] const bool findBarCreated = findBar.create(hwnd, hInstance, findBarConfig);

        // Same non-fatal treatment as findBar.create() above - a palette
        // that fails to create simply isn't available this session.
        CommandPaletteConfig commandPaletteConfig{};
        commandPaletteConfig.onClosed = [hwnd]() { ::SetFocus(hwnd); };
        auto commands =
            buildCommandRegistry(hwnd, findBar, session, renderPipeline, freeCursorModeEnabled);
        [[maybe_unused]] const bool commandPaletteCreated =
            commandPalette.create(hwnd, hInstance, commandPaletteConfig, std::move(commands));

        // Same non-fatal treatment as findBar.create() above.
        const GotoLineBarConfig gotoLineBarConfig =
            buildGotoLineBarConfig(hwnd, session, renderPipeline, gotoLineBar);
        [[maybe_unused]] const bool gotoLineBarCreated =
            gotoLineBar.create(hwnd, hInstance, gotoLineBarConfig);

        // Same non-fatal treatment as findBar.create() above.
        const GrepBarConfig grepBarConfig = buildGrepBarConfig(hwnd, session, renderPipeline, findBar,
                                                               grepBar, grepState, searchHistory);
        [[maybe_unused]] const bool grepBarCreated = grepBar.create(hwnd, hInstance, grepBarConfig);

        // Same non-fatal treatment as findBar.create() above.
        createAndPositionOutlinePane(hwnd, hInstance, session, renderPipeline, outlinePane);
        // Phase 7i: seeds FoldingModel's region list once at startup (mirrors
        // renderPipeline.setLanguage()'s own startup timing in wWinMain) so
        // "Fold/Unfold at Cursor" and the gutter markers work immediately,
        // without requiring the user to first open the outline panel
        // (refreshOutlinePane() re-seeds this later from the same parse
        // pattern whenever the panel is opened - see that function's comment).
        session.folding().setFoldableRegions(neomifes::app::buildFoldRegions(
            neomifes::app::extractCurrentOutline(session.document(), session.pathIfNamed()), session.document()));
        syncFoldingState(hwnd, renderPipeline, session.folding());
        // Phase 7h: pushes the startup cursor state (position 0, isPrimary)
        // into RenderPipeline before the first paint - without this,
        // m_cursorVisuals stays empty (its default) until the user's first
        // cursor-moving action, which left both the caret and (once added,
        // Phase 7h) the Breadcrumb strip invisible on a freshly opened file.
        // Supersedes the bare InvalidateRect() this replaced - this already
        // invalidates internally.
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    };
    cfg.onResize = [&renderPipeline, &findBar, &commandPalette, &gotoLineBar, &grepBar, &outlinePane](
                       HWND, std::uint32_t w, std::uint32_t h, float dpiScale) {
        if (renderPipeline.isAttached()) {
            const auto resized = renderPipeline.resize(w, h, dpiScale);
            if (!resized) {
                debugLogRenderError("RenderPipeline::resize", resized.error());
            }
        }
        findBar.onParentResized(w, dpiScale);
        commandPalette.onParentResized(w, dpiScale);
        gotoLineBar.onParentResized(w, dpiScale);
        grepBar.onParentResized(w, dpiScale);
        outlinePane.onParentResized(w, h, dpiScale);
    };
    cfg.onCommand = [&findBar, &commandPalette, &grepBar](HWND, WPARAM wParam, LPARAM lParam) {
        findBar.handleCommand(wParam, lParam);
        commandPalette.handleCommand(wParam, lParam);
        grepBar.handleCommand(wParam, lParam);
    };
    // Phase 7g: OutlinePane's WC_TREEVIEW is this codebase's first control
    // that notifies via WM_NOTIFY rather than WM_COMMAND - see
    // MainWindowConfig::onNotify's doc comment (main_window.h).
    cfg.onNotify = [&outlinePane](HWND, WPARAM wParam, LPARAM lParam) {
        return outlinePane.handleNotify(wParam, lParam);
    };
    // Phase 7c: SyntaxWorker's background-thread parse completion signal.
    // MainWindow forwards every WM_APP+ message it doesn't itself interpret
    // here unexamined (neomifes::ui never learns what kMsgSyntaxTokensReady
    // means - see MainWindowConfig::onAppMessage's doc comment); main.cpp
    // is the layer that already depends on both ui:: and render:: so it's
    // the only place that can safely compare against the constant and
    // reconstruct the payload's real type.
    cfg.onAppMessage = [&renderPipeline](HWND hwnd, UINT msg, WPARAM, LPARAM lParam) {
        if (msg != neomifes::render::kMsgSyntaxTokensReady) {
            return;
        }
        const std::unique_ptr<std::vector<neomifes::syntax::Token>> tokens(
            reinterpret_cast<std::vector<neomifes::syntax::Token>*>(lParam));
        renderPipeline.applyAsyncSyntaxTokens(std::move(*tokens));
        ::InvalidateRect(hwnd, nullptr, FALSE);
    };
    // WI-02: WM_CLOSE veto - both go through confirmDiscardIfDirty() so an
    // unsaved edit is never silently discarded by closing the window or
    // dropping a different file onto it.
    cfg.onClose = [&session](HWND hwnd) { return confirmDiscardIfDirty(hwnd, session); };
    cfg.onDropFiles = [&session, &renderPipeline, &findBar](HWND hwnd, std::vector<std::wstring> paths) {
        handleDropFilesEvent(hwnd, std::move(paths), session, renderPipeline, findBar);
    };
    cfg.onKeyDown = [&session, &renderPipeline, &findBar, &commandPalette, &gotoLineBar, &grepBar,
                     &outlinePane, &freeCursorModeEnabled](HWND hwnd, UINT vkCode, bool shiftDown,
                                                           bool ctrlDown) {
        handleKeyDownEvent(hwnd, vkCode, shiftDown, ctrlDown, session, renderPipeline, findBar,
                          commandPalette, gotoLineBar, grepBar, outlinePane, freeCursorModeEnabled);
    };
    cfg.onSysKeyDown = [&session, &renderPipeline](HWND hwnd, UINT vkCode, bool shiftDown) {
        return handleSysKeyDownEvent(hwnd, vkCode, shiftDown, session, renderPipeline);
    };
    cfg.onChar = [&session, &renderPipeline](HWND hwnd, wchar_t ch) {
        handleCharEvent(hwnd, ch, session, renderPipeline);
    };
    cfg.onMouseWheel = [&session, &renderPipeline](HWND hwnd, short wheelDelta) {
        session.viewport().scrollTo(neomifes::app::applyMouseWheelScroll(
            wheelDelta, session.viewport().topLine(), session.document().lineCount()));
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    };
    // WI-03: this window's first-ever scrollbar (WS_HSCROLL, added by
    // MainWindow::create() only because this handler is set - see
    // MainWindowConfig::onHScroll's comment). Body lives in
    // handleHScrollEvent() (not inline here) - see that function's comment.
    cfg.onHScroll = [&session, &renderPipeline](HWND hwnd, WORD scrollCode, WORD scrollPos) {
        handleHScrollEvent(hwnd, scrollCode, scrollPos, session, renderPipeline);
    };
    cfg.onMouseDown = [&session, &renderPipeline, &isDraggingMinimap](HWND hwnd, std::int32_t x,
                                                                      std::int32_t y, bool shiftDown,
                                                                      bool altDown, int clickCount) {
        handleMouseDownEvent(hwnd, x, y, shiftDown, altDown, clickCount, session, renderPipeline,
                             isDraggingMinimap);
    };
    cfg.onMouseDrag = [&session, &renderPipeline, &isDraggingMinimap](HWND hwnd, std::int32_t x,
                                                                      std::int32_t y) {
        // Highest priority: a minimap drag never falls through to
        // rectangularAnchor/altCursorAnchor/ordinary text-drag handling
        // below - it tracks by Y alone (Phase 7v, see minimapLineAtY()'s
        // comment on why X is ignored once a drag has started).
        if (isDraggingMinimap) {
            if (const auto targetLine = renderPipeline.minimapLineAtY(y)) {
                session.viewport().scrollTo(*targetLine);
                syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
            }
            return;
        }
        const auto hit = renderPipeline.hitTest(x, y);
        if (!hit) {
            return;
        }
        session.freeCursorVirtualColumns().reset();
        // Checked in this priority order: a rectangular-selection drag
        // (Phase 4b8a, Shift+Alt+drag) takes precedence over a plain
        // Alt+drag cursor extension (Phase 4b6d), which takes precedence
        // over the default drag-extends-primary-selection behavior (Phase
        // 4b3). At most one of rectangularAnchor/altCursorAnchor is ever
        // meaningfully set at a time - see neomifes::app::dispatchMouseDown()'s
        // comment for why a Shift+Alt+click that turns into a drag safely
        // supersedes whatever the down-click itself did.
        bool changed = false;
        const Document& document = session.document();
        auto& rectangularAnchor    = session.rectangularAnchor();
        auto& altCursorAnchor      = session.altCursorAnchor();
        if (rectangularAnchor) {
            session.selection().setRectangularSelection(*rectangularAnchor, *hit, document);
            // The rectangle just replaced the entire cursor set, so any
            // altCursorAnchor left over from an earlier plain Alt+click no
            // longer identifies a real cursor - clear it so the next
            // unrelated Shift+Alt+click doesn't silently no-op.
            altCursorAnchor.reset();
            session.viewport().ensureVisible(*hit, document);
            changed = true;
        } else if (altCursorAnchor) {
            session.selection().moveCursorMatching(*altCursorAnchor, *hit);
            session.viewport().ensureVisible(*hit, document);
            changed = true;
        } else {
            changed = neomifes::app::handleMouseDown(*hit, /*shiftDown=*/true, session.selection(),
                                                      session.viewport(), document);
        }
        if (changed) {
            syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
        }
    };
}

}  // namespace neomifes::app
