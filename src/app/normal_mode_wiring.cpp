#include "neomifes/app/normal_mode_wiring.h"

#include <commctrl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/app/command_dispatch.h"
#include "neomifes/app/csv_grid_bridge.h"
#include "neomifes/app/document_open.h"
#include "neomifes/app/editor_input.h"
#include "neomifes/app/file_dialogs.h"
#include "neomifes/app/fold_bridge.h"
#include "neomifes/app/git_diff_bridge.h"
#include "neomifes/app/git_diff_view_bridge.h"
#include "neomifes/app/git_pane_bridge.h"
#include "neomifes/app/grep_query_builder.h"
#include "neomifes/app/grep_result_formatting.h"
#include "neomifes/app/json_fold_bridge.h"
#include "neomifes/app/json_tree_bridge.h"
#include "neomifes/app/key_chord.h"
#include "neomifes/app/keybinding_dispatch.h"
#include "neomifes/app/session_manager.h"
#include "neomifes/app/menu_bar.h"
#include "neomifes/app/message_dialogs.h"
#include "neomifes/app/outline_bridge.h"
#include "neomifes/app/status_bar_format.h"
#include "neomifes/app/syntax_language.h"
#include "neomifes/app/tab_index_math.h"
#include "neomifes/app/tag_jump.h"
#include "neomifes/app/theme_settings.h"
#include "neomifes/app/xml_fold_bridge.h"
#include "neomifes/app/xml_tree_bridge.h"
#include "neomifes/core/bookmark_manager.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/folding_model.h"
#include "neomifes/core/indentation_conversion.h"
#include "neomifes/core/key_bindings.h"
#include "neomifes/core/line_operation_command.h"
#include "neomifes/core/line_operations.h"
#include "neomifes/core/replace_all_command.h"
#include "neomifes/core/selection_metrics.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/settings.h"
#include "neomifes/core/viewport.h"
#include "neomifes/csvmode/csv_delimiter_detection.h"
#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/document/file_saver.h"
#include "neomifes/encoding/encoding.h"
#include "neomifes/git/git_status_worker.h"
#include "neomifes/jsontree/json_format.h"
#include "neomifes/jsontree/json_path.h"
#include "neomifes/jsontree/json_tree.h"
#include "neomifes/logmode/format_detection.h"
#include "neomifes/logmode/log_grouping.h"
#include "neomifes/logmode/log_navigation.h"
#include "neomifes/logmode/log_pattern_file.h"
#include "neomifes/platform/clipboard.h"
#include "neomifes/search/grep_service.h"
#include "neomifes/search/replacement.h"
#include "neomifes/search/search_service.h"
#include "neomifes/ui/command_descriptor.h"
#include "neomifes/ui/find_navigation.h"
#include "neomifes/ui/goto_line_parser.h"
#include "neomifes/util/tag_jump_parser.h"
#include "neomifes/util/version.h"
#include "neomifes/util/wchar_cast.h"
#include "neomifes/xmltree/xpath.h"

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
using neomifes::csvmode::CsvModelWorker;
using neomifes::csvmode::kMsgCsvIndexReady;
using neomifes::document::Document;
using neomifes::document::LoadError;
using neomifes::document::TextRange;
using neomifes::git::GitStatusWorker;
using neomifes::git::kMsgGitStatusReady;
using neomifes::jsontree::kMsgJsonTreeReady;
using neomifes::jsontree::JsonTreeWorker;
using neomifes::logmode::builtInLogPatterns;
using neomifes::logmode::computeGroupedLogLevels;
using neomifes::logmode::detectLogPatternRule;
using neomifes::logmode::kAllLogLevelsVisible;
using neomifes::logmode::LogIndexWorker;
using neomifes::logmode::LogLevel;
using neomifes::logmode::loadUserLogPatternsFromDirectory;
using neomifes::logmode::logLevelFilterBit;
using neomifes::logmode::LogPatternRule;
using neomifes::logmode::nextVisibleLogLine;
using neomifes::logmode::previousVisibleLogLine;
using neomifes::render::FoldVisual;
using neomifes::render::ImeComposition;
using neomifes::render::MatchVisual;
using neomifes::render::RenderPipeline;
using neomifes::render::ThemeKind;
using neomifes::search::expandReplacementTemplate;
using neomifes::search::GrepMatch;
using neomifes::search::GrepService;
using neomifes::search::Match;
using neomifes::search::Query;
using neomifes::search::SearchService;
using neomifes::ui::CommandDescriptor;
using neomifes::ui::CommandId;
using neomifes::ui::CommandPalette;
using neomifes::ui::CommandPaletteConfig;
using neomifes::ui::CsvGridPane;
using neomifes::ui::CsvGridPaneConfig;
using neomifes::ui::FindDialog;
using neomifes::ui::FindReplaceDialog;
using neomifes::ui::FindDialogConfig;
using neomifes::ui::FindReplaceDialogConfig;
using neomifes::ui::GitPane;
using neomifes::ui::GitPaneConfig;
using neomifes::ui::GotoLineBar;
using neomifes::ui::GotoLineBarConfig;
using neomifes::ui::GrepBar;
using neomifes::ui::GrepBarConfig;
using neomifes::ui::JsonPathBar;
using neomifes::ui::JsonPathBarConfig;
using neomifes::ui::JsonTreePane;
using neomifes::ui::JsonTreePaneConfig;
using neomifes::ui::MainWindow;
using neomifes::ui::MainWindowConfig;
using neomifes::ui::OutlinePane;
using neomifes::ui::OutlinePaneConfig;
using neomifes::ui::StatusBar;
using neomifes::ui::StatusBarConfig;
using neomifes::ui::StatusBarParts;
using neomifes::ui::TabBar;
using neomifes::ui::TabBarConfig;
using neomifes::ui::TabBarItem;
using neomifes::xmltree::kMsgXmlTreeReady;
using neomifes::xmltree::XmlTreeWorker;

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
    // WI-21e: a wrapped line never needs horizontal scrolling (every column
    // is already visible on some row) - hiding the bar rather than merely
    // leaving it at a stale range/position also matches Viewport::
    // setWordWrapEnabled()'s own reasoning for why the underlying leftColumn
    // clamp is skipped while wrap is on, not just visually suppressed here.
    if (renderPipeline.wordWrapEnabled()) {
        ::ShowScrollBar(hwnd, SB_HORZ, FALSE);
        return;
    }
    ::ShowScrollBar(hwnd, SB_HORZ, TRUE);
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
// and pushes the resulting state to FindDialog/RenderPipeline (Phase 5b3a).
// Shared by runFindQuery() (jump to the first match after a new search) and
// navigateToMatch() (F3/Shift+F3) - both end up wanting exactly this. WI-04:
// takes EditorSession& (touches findReplaceState/selection/viewport/
// document - 4 members) instead of 4 separate refs.
// WI-18b: templated on `sink` (not a fixed FindDialog&) - the only thing this
// function (and refreshMatches()/runFindQuery()/navigateToMatch()/
// replaceCurrentMatch()/replaceAllMatches() below) ever does with it is
// call setMatchCount(std::size_t, std::size_t), a method both ui::FindDialog
// and ui::FindReplaceDialog implement identically. Templating on that
// shared shape (compile-time duck typing) lets both buildFindDialogConfig()
// and buildFindReplaceDialogConfig() reuse this exact search/replace logic
// verbatim, without a shared base class neither widget otherwise needs.
template <typename MatchCountSink>
void jumpToMatch(HWND hwnd, EditorSession& session, RenderPipeline& renderPipeline, MatchCountSink& sink) {
    const auto& state = session.findReplaceState();
    const Match& match = state.currentMatches[state.currentMatchIndex];
    session.selection().setCursors(
        {Cursor{.position = match.range.end, .anchor = match.range.start, .isPrimary = true}});
    session.viewport().ensureVisible(match.range.start, session.document());
    sink.setMatchCount(state.currentMatchIndex, state.currentMatches.size());
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
template <typename MatchCountSink>
void refreshMatches(const Query& query, EditorSession& session, RenderPipeline& renderPipeline,
                    MatchCountSink& sink) {
    auto& state              = session.findReplaceState();
    state.currentQuery      = query;
    state.currentMatches    = SearchService::findAll(session.document(), query);
    state.currentMatchIndex = 0;
    sink.setMatchCount(state.currentMatchIndex, state.currentMatches.size());
    syncMatchVisuals(state, renderPipeline);
}

// Runs SearchService::findAll() for FindDialog's onQueryChanged callback and
// jumps to the first match, if any (Phase 5b3a). An empty/no-match result
// clears all highlighting and shows FindDialog's "no results" state. WI-04:
// takes EditorSession& (document/findReplaceState/selection/viewport).
template <typename MatchCountSink>
void runFindQuery(std::u16string_view query, bool caseSensitive, bool wholeWord, bool regex, HWND hwnd,
                  EditorSession& session, RenderPipeline& renderPipeline, MatchCountSink& sink) {
    refreshMatches(Query{.pattern       = std::u16string(query),
                        .caseSensitive = caseSensitive,
                        .wholeWord     = wholeWord,
                        .regex         = regex},
                  session, renderPipeline, sink);
    if (session.findReplaceState().currentMatches.empty()) {
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    jumpToMatch(hwnd, session, renderPipeline, sink);
}

// F3 (forward=true) / Shift+F3 (forward=false), wrapping around - shared by
// FindDialogConfig::onFindNext/onFindPrevious (fired while the find edit has
// focus) and the F3/Shift+F3 branch of handleFindDialogKey() below (fired
// while the document editing area has focus instead) - same "one shared
// helper, two call sites" pattern as neomifes::app::dispatchMouseDown().
// WI-04: takes EditorSession& (findReplaceState/selection/viewport/
// document).
template <typename MatchCountSink>
void navigateToMatch(bool forward, HWND hwnd, EditorSession& session, RenderPipeline& renderPipeline,
                     MatchCountSink& sink) {
    auto& state = session.findReplaceState();
    if (state.currentMatches.empty()) {
        return;
    }
    state.currentMatchIndex = forward
        ? neomifes::ui::nextMatchIndex(state.currentMatchIndex, state.currentMatches.size())
        : neomifes::ui::previousMatchIndex(state.currentMatchIndex, state.currentMatches.size());
    jumpToMatch(hwnd, session, renderPipeline, sink);
}

// Escape while the find edit has focus (FindDialogConfig::onClosed) - hides
// the bar, clears match highlighting, and restores focus to the document
// editing area (FindDialog itself does not know where that is). WI-04: takes
// EditorSession& (findReplaceState).
void closeFindDialog(HWND hwnd, FindDialog& findDialog, EditorSession& session, RenderPipeline& renderPipeline) {
    findDialog.hide();
    session.findReplaceState().currentMatches.clear();
    renderPipeline.setMatchVisuals({});
    ::SetFocus(hwnd);
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Ctrl+F (show) / Ctrl+H (show with replace, WI-07 step2 - previously
// listed in the command palette with keybindingLabel "Ctrl+H" but never
// actually wired to any key, see build_plan.md WI-07) / F3 / Shift+F3
// (navigate) while the document editing area has focus (not the find edit -
// see find_dialog.h's class comment for why these same keys are ALSO handled
// inside FindDialog's own subclass proc when the find edit itself has focus).
// Returns true if the key was one this handles. Deliberately NOT promoted
// to the global accelerator table (command_dispatch.h) - see that header's
// top comment: Ctrl+H's control-character WM_CHAR fallback and F3's
// search-history-recording asymmetry with FindDialogConfig::onFindNext() made
// this whole group unsafe to move. WI-04: takes EditorSession&
// (findReplaceState/selection/viewport/document).
// WI-10: takes const core::KeyBindings& - every hardcoded vkCode/modifier
// literal below became a chordMatches() lookup against the live bindings.
// altDown is always passed as false here (and in every other handle*Key()
// below) - the WM_KEYDOWN-driven manual chain has never tracked Alt state
// (see handleKeyDownEvent()'s own parameter list), and none of the 4
// embedded presets (key_bindings_presets.cpp) bind any manual-chain command
// to an Alt-modified chord.
bool handleFindDialogKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, FindDialog& findDialog,
                      FindReplaceDialog& findReplaceDialog, EditorSession& session, RenderPipeline& renderPipeline,
                      const core::KeyBindings& keyBindings) {
    if (chordMatches(keyBindings, CommandId::FindShow, ctrlDown, shiftDown, false, vkCode)) {
        findDialog.show(hwnd);
        return true;
    }
    // WI-18b: previously findBar.showWithReplace() - now opens the
    // standalone dialog, see dispatchWidgetShowCommand()'s identical
    // CommandId::FindReplace case for the full rationale.
    if (chordMatches(keyBindings, CommandId::FindReplace, ctrlDown, shiftDown, false, vkCode)) {
        findReplaceDialog.show(hwnd);
        return true;
    }
    const bool isNext = chordMatches(keyBindings, CommandId::FindNext, ctrlDown, shiftDown, false, vkCode);
    if (isNext || chordMatches(keyBindings, CommandId::FindPrevious, ctrlDown, shiftDown, false, vkCode)) {
        navigateToMatch(isNext, hwnd, session, renderPipeline, findDialog);
        return true;
    }
    return false;
}

// Ctrl+Shift+P while the document editing area has focus (Phase 5b3c) -
// mirrors handleFindDialogKey()'s single-purpose shape. Not fired while the
// palette's own query edit has focus (same reasoning as handleFindDialogKey's
// comment: Win32 routes keyboard input straight to the focused child HWND).
bool handleCommandPaletteKey(UINT vkCode, bool shiftDown, bool ctrlDown, CommandPalette& commandPalette,
                             const core::KeyBindings& keyBindings) {
    if (chordMatches(keyBindings, CommandId::CommandPaletteShow, ctrlDown, shiftDown, false, vkCode)) {
        commandPalette.show();
        return true;
    }
    return false;
}

// Ctrl+Shift+F while the document editing area has focus (Phase 5c3) -
// mirrors handleCommandPaletteKey()'s single-purpose shape. Must be checked
// in handleKeyDownEvent()'s dispatch chain BEFORE handleFindDialogKey():
// handleFindDialogKey()'s own `ctrlDown && vkCode == 'F'` check does not look
// at shiftDown, so without this ordering Ctrl+Shift+F would already be
// swallowed by the plain Find dialog before ever reaching this function.
bool handleGrepKey(UINT vkCode, bool shiftDown, bool ctrlDown, GrepBar& grepBar,
                   const core::KeyBindings& keyBindings) {
    if (chordMatches(keyBindings, CommandId::GrepShow, ctrlDown, shiftDown, false, vkCode)) {
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

// WI-15i: keeps RenderPipeline's reserved right-hand width in sync with
// whichever right-docked pane (OutlinePane/JsonTreePane) is currently
// visible - see RenderPipeline::setRightPaneWidthDips()'s own comment for
// why this must be called at EVERY toggle transition (both the show and the
// hide branch, at every one of their call sites below), not just from
// cfg.onResize's WM_SIZE path: unlike m_tabBarHeightDips/
// m_statusBarHeightDips (set once at startup and never again), this value
// changes whenever a pane opens or closes, and a stale value would leave
// the document view rendering at the wrong width until some unrelated
// resize happened to correct it. Takes the max() of the three panes' own
// widthDips() rather than assuming mutual exclusivity - all three dock to
// the SAME right edge, so this is still correct even if they were ever
// simultaneously visible (today they are not, by construction of every
// toggle path below, but this doesn't rely on that). Always invalidates -
// the document view must reflow immediately, not wait for the next paint.
// WI-17e: gained gitPane - same right-docked-strip shape as outlinePane/
// jsonTreePane above (see git_pane.h's own class comment).
void syncRightPaneWidthDips(HWND hwnd, RenderPipeline& renderPipeline, const neomifes::ui::OutlinePane& outlinePane,
                            const JsonTreePane& jsonTreePane, const GitPane& gitPane) {
    float widthDips = 0.0F;
    if (outlinePane.isVisible()) {
        widthDips = std::max(widthDips, neomifes::ui::OutlinePane::widthDips());
    }
    if (jsonTreePane.isVisible()) {
        widthDips = std::max(widthDips, JsonTreePane::widthDips());
    }
    if (gitPane.isVisible()) {
        widthDips = std::max(widthDips, GitPane::widthDips());
    }
    renderPipeline.setRightPaneWidthDips(widthDips);
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Ctrl+Shift+O while the document editing area has focus (Phase 7g) -
// unlike handleCommandPaletteKey()/handleGrepKey(), this TOGGLES (a second
// press while visible hides it) rather than only ever showing. An outline
// view is a persistent navigation aid the user dismisses with the same key
// they opened it with, not a one-shot search/command tool - see
// outline_pane.h's class comment. WI-04: takes EditorSession& (document/
// path/folding - 3 members). WI-15i: takes JsonTreePane& too, purely to pass
// through to syncRightPaneWidthDips() (see that function's own comment) -
// this function itself never touches jsonTreePane otherwise. WI-17e: same
// reasoning for the added GitPane&.
bool handleOutlineKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, EditorSession& session,
                      neomifes::ui::OutlinePane& outlinePane, JsonTreePane& jsonTreePane, GitPane& gitPane,
                      RenderPipeline& renderPipeline, const core::KeyBindings& keyBindings) {
    if (!chordMatches(keyBindings, CommandId::OutlineToggle, ctrlDown, shiftDown, false, vkCode)) {
        return false;
    }
    if (outlinePane.isVisible()) {
        outlinePane.hide();
        syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
    } else {
        refreshOutlinePane(session, outlinePane);
        syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
        syncFoldingState(hwnd, renderPipeline, session.folding());
    }
    return true;
}

// Shows/refreshes JsonTreePane for session's current document (WI-15c) -
// same role as refreshOutlinePane() above, split across a sync/async branch
// because JsonTreeWorker's parse is backgrounded (WI-15b) while
// extractCurrentOutline() above is not. If session.jsonTree() already holds
// a cached result (a previous toggle-on already indexed this exact document
// state - there is no invalidation-on-edit, same "stale until next refresh"
// limitation refreshOutlinePane()'s own comment documents), it is shown
// immediately and no indexing request is made. Otherwise the pane is shown
// EMPTY right away (never left at its previous tab's stale content) and a
// background request is kicked off if one isn't already in flight;
// `jsonTreePanePendingSessionToken` is set to `&session` whenever indexing
// ends up in flight (whether started just now or already running from an
// earlier toggle) so applyJsonTreeReadyMessage() below knows to auto-
// populate the pane once that specific result lands - see this file's
// normal_mode_wiring.h comment on why that token lives outside EditorSession.
void refreshJsonTreePane(HWND hwnd, EditorSession& session, JsonTreePane& jsonTreePane,
                         std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker, RenderPipeline& renderPipeline,
                         const void*& jsonTreePanePendingSessionToken) {
    if (session.jsonTree().has_value()) {
        jsonTreePanePendingSessionToken = nullptr;
        const auto& tree                = *session.jsonTree();
        jsonTreePane.showWith({neomifes::app::buildJsonTreeItems(tree)});
        session.folding().setFoldableRegions(neomifes::app::buildJsonFoldRegions(tree, session.document()));
        syncFoldingState(hwnd, renderPipeline, session.folding());
        return;
    }

    jsonTreePane.showWith({});
    if (!session.jsonTreeIndexInFlight() && jsonTreeWorker) {
        session.beginJsonTreeIndexing(*jsonTreeWorker);
    }
    if (session.jsonTreeIndexInFlight()) {
        jsonTreePanePendingSessionToken = &session;
    }
}

// XML counterpart of refreshJsonTreePane() above (WI-15h) - full sibling,
// not a templated/shared body, matching this file's own established
// "small duplicated toggle body per pane" convention (see
// appendStructuralViewCommands()'s own comment below on why
// JsonTreeToggle/CsvGridToggle's action bodies duplicate their handleXxxKey()
// counterparts rather than calling them).
//
// Asymmetry worth noting: parseXmlTree() never fails (WI-15f/g) - an
// unparseable document still produces a real XmlTree with
// root.kind == XmlNodeKind::Error, not a dropped/absent result. So
// session.xmlTree().has_value() becomes permanently true after the FIRST
// successful indexing round, even for garbage XML input - unlike JSON
// (where an invalid document leaves jsonTree() perpetually nullopt, so
// every toggle-on re-attempts indexing and shows an empty pane), a
// malformed XML document's pane shows one cached "[parse error] ..." row
// from then on. This is the intended consequence of WI-15f's "always
// return a real XmlTree" design, not a bug.
void refreshXmlTreePane(HWND hwnd, EditorSession& session, JsonTreePane& jsonTreePane,
                        std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker, RenderPipeline& renderPipeline,
                        const void*& jsonTreePanePendingSessionToken) {
    if (session.xmlTree().has_value()) {
        jsonTreePanePendingSessionToken = nullptr;
        const auto& tree                = *session.xmlTree();
        jsonTreePane.showWith({neomifes::app::buildXmlTreeItems(tree.root)});
        session.folding().setFoldableRegions(neomifes::app::buildXmlFoldRegions(tree.root, session.document()));
        syncFoldingState(hwnd, renderPipeline, session.folding());
        return;
    }

    jsonTreePane.showWith({});
    if (!session.xmlTreeIndexInFlight() && xmlTreeWorker) {
        session.beginXmlTreeIndexing(*xmlTreeWorker);
    }
    if (session.xmlTreeIndexInFlight()) {
        jsonTreePanePendingSessionToken = &session;
    }
}

// Single toggle entry point (Ctrl+Shift+J / view menu / command palette)
// for BOTH JSON and XML documents (WI-15h) - ui::JsonTreePane was designed
// from the start (WI-15c) to serve either format (see that class's own
// header comment: "the JSON/XML structure tree panel"), so this dispatch
// is the only new branching logic WI-15h adds; every other language
// (including JSON itself) falls through to the pre-existing,
// UNCHANGED refreshJsonTreePane() path below. jsonTreePanePendingSessionToken
// is safely shared between the two paths - see this file's normal_mode_wiring.h
// comment on why (a session's language() is fixed at toggle-time, so exactly
// one of beginJsonTreeIndexing()/beginXmlTreeIndexing() is ever kicked off
// per toggle-on, and the token can only ever be satisfied by that one
// request's own result message).
void refreshStructureTreePane(HWND hwnd, EditorSession& session, JsonTreePane& jsonTreePane,
                              std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker,
                              std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker, RenderPipeline& renderPipeline,
                              const void*& jsonTreePanePendingSessionToken) {
    if (session.language() == neomifes::syntax::Language::Xml) {
        refreshXmlTreePane(hwnd, session, jsonTreePane, xmlTreeWorker, renderPipeline,
                           jsonTreePanePendingSessionToken);
        return;
    }
    refreshJsonTreePane(hwnd, session, jsonTreePane, jsonTreeWorker, renderPipeline, jsonTreePanePendingSessionToken);
}

// Ctrl+Shift+J while the document editing area has focus (WI-15c) - same
// toggle shape as handleOutlineKey() above (see that function's own comment
// for why this TOGGLES rather than only ever showing). Clears
// jsonTreePanePendingSessionToken on the hide branch - without this, a
// kMsgJsonTreeReady result that arrives AFTER the user has already closed
// the pane would still match the stale token and silently pop the pane back
// open (see normal_mode_wiring.h's own comment on this hazard).
bool handleJsonTreeKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, EditorSession& session,
                       JsonTreePane& jsonTreePane, const neomifes::ui::OutlinePane& outlinePane, GitPane& gitPane,
                       std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker,
                       std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker, RenderPipeline& renderPipeline,
                       const core::KeyBindings& keyBindings, const void*& jsonTreePanePendingSessionToken) {
    if (!chordMatches(keyBindings, CommandId::JsonTreeToggle, ctrlDown, shiftDown, false, vkCode)) {
        return false;
    }
    if (jsonTreePane.isVisible()) {
        jsonTreePane.hide();
        jsonTreePanePendingSessionToken = nullptr;
        syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
    } else {
        refreshStructureTreePane(hwnd, session, jsonTreePane, jsonTreeWorker, xmlTreeWorker, renderPipeline,
                                 jsonTreePanePendingSessionToken);
        syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
    }
    return true;
}

// Shows/refreshes CsvGridPane for session's current document (WI-16c) - same
// async-aware role refreshJsonTreePane() above plays for JsonTreePane. No
// hwnd/RenderPipeline needed here (unlike refreshJsonTreePane()'s folding
// sync) - CsvGridPane has no folding-gutter integration. If session.csvModel()
// already holds a cached result, it is shown immediately and no indexing
// request is made. Otherwise the pane is shown EMPTY right away and a
// background request is kicked off (using detectCsvDelimiter()'s guess,
// falling back to ',' - same "auto-detect once, then treat it as the
// document's delimiter" contract every WI-16 CsvParseOptions call site
// shares) if one isn't already in flight; csvGridPanePendingSessionToken is
// set whenever indexing ends up in flight, same contract as
// jsonTreePanePendingSessionToken.
void refreshCsvGridPane(EditorSession& session, CsvGridPane& csvGridPane,
                        std::optional<csvmode::CsvModelWorker>& csvModelWorker,
                        const void*& csvGridPanePendingSessionToken) {
    // WI-16e: restores THIS session's own filter text before anything else -
    // reopening the grid for a session whose csvFilter() differs from what a
    // PREVIOUSLY active tab left typed in the (single, shared) filter edit
    // must not show that other tab's leftover query. setFilterQueryText()
    // does not fire onFilterQueryChanged (WM_SETTEXT's own contract), so
    // this alone never triggers a spurious recompute.
    csvGridPane.setFilterQueryText(session.csvFilter().query);

    if (session.csvModel().has_value()) {
        csvGridPanePendingSessionToken = nullptr;
        const auto& model               = *session.csvModel();
        csvGridPane.showWith(neomifes::app::buildCsvGridColumnLabels(model, session.document(), session.csvSort()),
                             session.csvRowOrder().size());
        return;
    }

    csvGridPane.showWith({}, 0);
    if (!session.csvIndexInFlight() && csvModelWorker) {
        const char16_t delimiter = csvmode::detectCsvDelimiter(session.document()).value_or(u',');
        session.beginCsvIndexing(*csvModelWorker, csvmode::CsvParseOptions{.delimiter = delimiter, .hasHeader = true});
    }
    if (session.csvIndexInFlight()) {
        csvGridPanePendingSessionToken = &session;
    }
}

// Ctrl+Shift+G while the document editing area has focus (WI-16c) - same
// toggle shape as handleJsonTreeKey() above (see handleOutlineKey()'s own
// comment for why this TOGGLES rather than only ever showing). Clears
// csvGridPanePendingSessionToken on the hide branch - same hazard
// handleJsonTreeKey()'s own comment documents. Deliberately takes NO hwnd -
// unlike refreshJsonTreePane() (folding sync needs it), refreshCsvGridPane()
// has no use for one (CsvGridPane has no folding-gutter integration, see
// its own header comment), so there is nothing here to pass it to.
bool handleCsvGridKey(UINT vkCode, bool shiftDown, bool ctrlDown, EditorSession& session, CsvGridPane& csvGridPane,
                      std::optional<csvmode::CsvModelWorker>& csvModelWorker, const core::KeyBindings& keyBindings,
                      const void*& csvGridPanePendingSessionToken) {
    if (!chordMatches(keyBindings, CommandId::CsvGridToggle, ctrlDown, shiftDown, false, vkCode)) {
        return false;
    }
    if (csvGridPane.isVisible()) {
        csvGridPane.hide();
        csvGridPanePendingSessionToken = nullptr;
    } else {
        refreshCsvGridPane(session, csvGridPane, csvModelWorker, csvGridPanePendingSessionToken);
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
// single-purpose shape as handleFindDialogKey()/handleCommandPaletteKey().
// WI-10: gained a shiftDown parameter (previously absent - Ctrl+G has no
// Shift variant in the pre-WI-10 hardcoded check) since chordMatches()
// needs the full modifier state to compare against an arbitrary configured
// chord.
bool handleGotoLineKey(UINT vkCode, bool shiftDown, bool ctrlDown, GotoLineBar& gotoLineBar,
                       const core::KeyBindings& keyBindings) {
    if (chordMatches(keyBindings, CommandId::GotoLineShow, ctrlDown, shiftDown, false, vkCode)) {
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
// WI-10: the pre-WI-10 version distinguished its 3 sub-commands (toggle/
// next/previous) purely by vkCode==VK_F2 plus the ctrlDown/shiftDown flags -
// all 3 CommandIds (BookmarkToggle/BookmarkNext/BookmarkPrevious) happen to
// share vkCode==VK_F2 in the neomifes preset, differing only by modifier,
// which is exactly what per-CommandId chordMatches() calls express directly
// (and, unlike the old hardcoded check, correctly follow a user's
// keybindings.json even if it moves one of these 3 off VK_F2 entirely).
bool handleBookmarkKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, EditorSession& session,
                       RenderPipeline& renderPipeline, const core::KeyBindings& keyBindings) {
    const Document& document    = session.document();
    const auto      currentLine = document.offsetToLine(session.selection().primaryCursor().position);
    if (chordMatches(keyBindings, CommandId::BookmarkToggle, ctrlDown, shiftDown, false, vkCode)) {
        session.bookmarks().toggle(currentLine);
        renderPipeline.setBookmarkedLines(std::vector<neomifes::document::LineNumber>(
            session.bookmarks().lines().begin(), session.bookmarks().lines().end()));
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    const bool isNext = chordMatches(keyBindings, CommandId::BookmarkNext, ctrlDown, shiftDown, false, vkCode);
    const bool isPrevious =
        chordMatches(keyBindings, CommandId::BookmarkPrevious, ctrlDown, shiftDown, false, vkCode);
    if (!isNext && !isPrevious) {
        return false;
    }
    const auto target = isPrevious ? session.bookmarks().previous(currentLine) : session.bookmarks().next(currentLine);
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
// WI-16c: hides CsvGridPane and clears its pending-session token - a
// document swap (Ctrl+O/Ctrl+N/drag-drop) replaces the SAME EditorSession's
// document with different content, so any grid the OLD content produced is
// meaningless for the new one (and, being a full-client-area overlay, would
// otherwise hide the newly loaded document entirely - unlike OutlinePane/
// JsonTreePane's docked strips, which tolerate staying open across this same
// event). Clearing the token here is the same hazard handleCsvGridKey()'s
// own comment documents for the toggle-off path - without it, a
// kMsgCsvIndexReady result for the OLD document's now-abandoned indexing
// request could still resurrect the pane.
void resetViewAfterDocumentSwap(HWND hwnd, RenderPipeline& renderPipeline, EditorSession& session, FindDialog& findDialog,
                                CsvGridPane& csvGridPane, const void*& csvGridPanePendingSessionToken) {
    // WI-17f: every call site of this function replaces `session`'s own
    // Document content in place (its address is stable across the swap),
    // which every OTHER piece of state below already relied on implicitly
    // by never re-pointing renderPipeline at it. The Diff view is the
    // first feature to break that assumption (it redirects renderPipeline
    // at a wholly SEPARATE synthesized Document while open) - without this
    // explicit re-point, opening the Diff view and then swapping this
    // tab's content (e.g. Ctrl+O into the same tab) would leave the screen
    // rendering the now-abandoned synthesized document, frozen, until some
    // unrelated event forced a real repaint. setDiffViewActive(false) also
    // clears any stale Added/Removed markers for the same reason
    // syncViewForActiveSession() above does.
    renderPipeline.setDocument(&session.document());
    renderPipeline.setDiffViewActive(false);
    auto& findReplaceState = session.findReplaceState();
    findReplaceState.currentMatches.clear();
    findReplaceState.currentMatchIndex = 0;
    findDialog.setMatchCount(0, 0);
    renderPipeline.setMatchVisuals({});
    renderPipeline.setBookmarkedLines({});
    // WI-17c: the OLD content's Git diff markers are meaningless for the
    // newly swapped-in document - same "clear rather than leave stale"
    // treatment as matchVisuals/bookmarkedLines above. The new document
    // hasn't been diffed yet (no automatic trigger in this WI's scope, see
    // build_plan.md's WI-17c section) - a fresh "Git: Refresh Diff Markers"
    // run is what repopulates this.
    renderPipeline.setGitDiffRegions({});
    session.folding().setFoldableRegions({});
    syncFoldingState(hwnd, renderPipeline, session.folding());
    renderPipeline.setLanguage(session.language());
    csvGridPane.hide();
    csvGridPanePendingSessionToken = nullptr;
    ::SetFocus(hwnd);
}

// WI-14c: pushes `session`'s log-mode color-coding/filter into
// `renderPipeline` (or turns them off, if `session` never enabled log
// mode) - shared by syncViewForActiveSession() below (tab switch) and
// applyLogIndexReadyMessage() (async LogIndexWorker result arriving for
// the currently active tab) so both keep RenderPipeline's log-mode state
// in sync with whichever EditorSession is actually on screen, the same
// "one shared push function, multiple call sites" shape
// syncFoldingState()/syncMatchVisuals() already established for their own
// per-tab visuals.
//
// WI-14d: routes through computeGroupedLogLevels() rather than pushing each
// LogLine's own level directly - a continuation line (e.g. a Java stack
// trace frame) now inherits its group leader's level, so isLineHidden()'s
// filter check and drawLogLevelOnLine()'s color lookup both treat a
// multi-line entry as one unit instead of the continuation lines defaulting
// to LogLevel::Unknown and disagreeing with their own ERROR/WARNING header.
void pushLogVisualsForSession(RenderPipeline& renderPipeline, const EditorSession& session) {
    if (!session.logModel()) {
        renderPipeline.setLogLineLevels({});
        return;
    }
    renderPipeline.setLogLineLevels(computeGroupedLogLevels(session.logModel()->lines()));
    renderPipeline.setLogLevelFilter(session.logLevelFilterMask());
}

// WI-17c: pushes `session`'s Git diff gutter markers into `renderPipeline`
// (or clears them, if `session` has never been diffed / isn't inside a Git
// repository / is Untitled - EditorSession::gitDiff() itself doesn't
// distinguish those cases, see its own comment). Same "one shared push
// function, multiple call sites" shape as pushLogVisualsForSession() above -
// shared by syncViewForActiveSession() below (tab switch) and
// applyGitDiffReadyMessage() (async GitDiffWorker result arriving for the
// currently active tab).
void pushGitDiffVisualsForSession(RenderPipeline& renderPipeline, const EditorSession& session) {
    if (!session.gitDiff()) {
        renderPipeline.setGitDiffRegions({});
        return;
    }
    renderPipeline.setGitDiffRegions(buildGitDiffMarkers(*session.gitDiff()));
}

// WI-05: pushes `session`'s OWN already-existing view state into
// RenderPipeline/FindDialog - the tab-switch counterpart to
// resetViewAfterDocumentSwap() above. Deliberately NOT reused for a tab
// switch: that function CLEARS matches/folds/bookmarks/language, which is
// correct when the SAME EditorSession's Document is being replaced with a
// different file's content, but wrong here - a tab switch moves the
// "active" role to an EditorSession that already has its own matches/
// folds/bookmarks/language, which must be RESTORED as-is, not wiped. Also
// used for newly-created/opened sessions (Ctrl+O/Ctrl+N/drag-drop below) -
// a brand new EditorSession's state (empty matches/folds/bookmarks) is
// indistinguishable from "restore" in that case, so the same function
// covers both without a separate code path.
// WI-16c: hides CsvGridPane and clears its pending-session token on every
// tab switch - see resetViewAfterDocumentSwap()'s own comment for why this
// is required (not merely tolerated the way OutlinePane/JsonTreePane's own
// stay-open-across-a-tab-switch gap already is) for a full-client-area
// overlay.
void syncViewForActiveSession(HWND hwnd, RenderPipeline& renderPipeline, EditorSession& session, FindDialog& findDialog,
                              CsvGridPane& csvGridPane, const void*& csvGridPanePendingSessionToken) {
    renderPipeline.setDocument(&session.document());
    // WI-17f: unconditionally closes the Diff view if it happened to be
    // open - same "force-close a full-view overlay on any tab switch/file
    // open" reasoning csvGridPane.hide() below already established for
    // CsvGridPane. This function IS the Diff view's own "close" action
    // (see the new git.toggleDiffView command's action lambda, which just
    // calls this function again rather than duplicating restore logic).
    renderPipeline.setDiffViewActive(false);
    renderPipeline.setLanguage(session.language());
    renderPipeline.setBookmarkedLines(std::vector<neomifes::document::LineNumber>(
        session.bookmarks().lines().begin(), session.bookmarks().lines().end()));
    syncFoldingState(hwnd, renderPipeline, session.folding());
    // WI-14c: pushes the NEWLY active session's own log-mode color-coding/
    // filter (or turns them off, if this session never enabled log mode) -
    // pushLogVisualsForSession()'s own comment explains why this is shared
    // with the async kMsgLogIndexReady path instead of only living there.
    pushLogVisualsForSession(renderPipeline, session);
    // WI-17c: restores the newly active session's own already-cached Git
    // diff markers (or clears them, if this session has never been diffed) -
    // pushGitDiffVisualsForSession()'s own comment explains why this is
    // shared with the async kMsgGitDiffReady path instead of only living
    // there.
    pushGitDiffVisualsForSession(renderPipeline, session);
    syncMatchVisuals(session.findReplaceState(), renderPipeline);
    findDialog.setMatchCount(session.findReplaceState().currentMatchIndex,
                          session.findReplaceState().currentMatches.size());
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    syncHorizontalScrollBar(hwnd, renderPipeline, session.viewport());
    csvGridPane.hide();
    csvGridPanePendingSessionToken = nullptr;
    ::SetFocus(hwnd);
}

// WI-11: AutosaveContext's autosaveDir/indexPath are optional (nullopt if
// resolveAppDataDir() failed at startup - see AutosaveContext's own header
// comment, command_dispatch.h). These two wrappers centralize the
// "unwrap-or-silently-no-op" guard in one place so every call site below
// (performSave() success, confirmDiscardIfDirty()'s DontSave branch, the
// periodic/focus-loss autosave sweep) doesn't repeat it.
void clearAutoSaveIfConfigured(const EditorSession& session, const AutosaveContext& autosave) {
    if (!autosave.autosaveDir || !autosave.indexPath) {
        return;
    }
    clearAutoSave(session, *autosave.autosaveDir, autosave.index, *autosave.indexPath);
}

void performAutoSaveIfConfigured(EditorSession& session, const AutosaveContext& autosave) {
    if (!autosave.autosaveDir || !autosave.indexPath) {
        return;
    }
    performAutoSave(session, *autosave.autosaveDir, autosave.index, *autosave.indexPath);
}

// WI-11: fired from cfg.onTimer (kAutoSaveTimerId) and cfg.onFocusLost -
// sweeps EVERY open tab (not just the active one), since autosave exists
// to protect whichever tabs happen to have unsaved changes, not just the
// one currently visible. performAutoSaveIfConfigured()/performAutoSave()
// itself already skip untitled/non-dirty sessions, so this loop costs
// nothing extra for the common case of mostly-clean tabs.
void autoSaveAllDirtySessions(Workspace& workspace, const AutosaveContext& autosave) {
    for (std::size_t i = 0; i < workspace.sessionCount(); ++i) {
        performAutoSaveIfConfigured(workspace.sessionAt(i), autosave);
    }
}

// WI-11: extracted out of wireNormalMode()'s onDeferredInit body purely to
// keep clang-tidy's cognitive-complexity check happy (src/ threshold of 25 -
// wireNormalMode() crossed it once this WI's conditional timer-start was
// added inline there) - same rationale as main.cpp's own WI-11 wWinMain
// extractions. 0 is the documented "autosave disabled" sentinel
// (core::Settings::autoSaveIntervalSeconds's own comment) - simply don't
// start the timer at all rather than starting one with a meaningless 0ms
// interval.
void startAutoSaveTimerIfConfigured(MainWindow& window, const core::Settings& settings) {
    if (settings.autoSaveIntervalSeconds > 0) {
        static_cast<void>(window.startAutoSaveTimer(settings.autoSaveIntervalSeconds * 1000U));
    }
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
// WI-11: records the opened path into `recentFiles` on success (covers
// F12/Grep-click too, not just Ctrl+O - opening a file via any path is
// "recently used" regardless of how it was triggered) and refreshes the
// menu to match.
std::optional<LoadError> openFileAndSyncView(const std::filesystem::path& path,
                                             std::optional<neomifes::document::LineNumber> targetLine,
                                             std::optional<std::uint64_t> targetColumn, HWND hwnd,
                                             EditorSession& session, RenderPipeline& renderPipeline,
                                             FindDialog& findDialog, core::RecentFiles& recentFiles,
                                             const MenuBarHandles& menuHandles, CsvGridPane& csvGridPane,
                                             const void*& csvGridPanePendingSessionToken) {
    auto error = session.openFile(path, targetLine, targetColumn);
    if (error) {
        return error;
    }
    resetViewAfterDocumentSwap(hwnd, renderPipeline, session, findDialog, csvGridPane, csvGridPanePendingSessionToken);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    recentFiles.record(path);
    refreshRecentFilesMenu(menuHandles, hwnd, recentFiles);
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
// WI-11: passes settings.createBackupOnSave through as saveFile()'s new
// keepBackup parameter; on success, records the saved path into
// `recentFiles` + refreshes the menu (covers both a plain Ctrl+S on an
// already-named file - re-recording just bumps it to MRU front, "recently
// USED not just recently opened" - and Ctrl+Shift+S/Save-As assigning a
// brand new path), and clears any stale autosave for this session (the
// autosave copy is now superseded by this real save).
bool performSave(HWND hwnd, EditorSession& session, bool forceSaveAs, const core::Settings& settings,
                 core::RecentFiles& recentFiles, const MenuBarHandles& menuHandles,
                 const AutosaveContext& autosave) {
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
                                                       fileState.lineEnding, fileState.writeBom,
                                                       settings.createBackupOnSave);
    if (error) {
        neomifes::app::showSaveErrorDialog(hwnd, *error);
        return false;
    }
    session.setSavedPath(targetPath);
    recentFiles.record(targetPath);
    refreshRecentFilesMenu(menuHandles, hwnd, recentFiles);
    clearAutoSaveIfConfigured(session, autosave);
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
// WI-11: the DontSave branch clears any autosave for this session - the
// user just explicitly said "discard these changes", so the autosave copy
// holding them must not survive to be wrongly offered as "crash recovery"
// on a later launch (that would resurrect content the user just declined).
bool confirmDiscardIfDirty(HWND hwnd, EditorSession& session, const core::Settings& settings,
                           core::RecentFiles& recentFiles, const MenuBarHandles& menuHandles,
                           const AutosaveContext& autosave) {
    if (!session.isDirty()) {
        return true;
    }
    const std::wstring documentName =
        session.isUntitled() ? L"Untitled" : session.path().filename().wstring();
    switch (neomifes::app::showUnsavedChangesDialog(hwnd, documentName)) {
        case neomifes::app::UnsavedChangesChoice::Save:
            return performSave(hwnd, session, /*forceSaveAs=*/session.isUntitled(), settings, recentFiles,
                               menuHandles, autosave);
        case neomifes::app::UnsavedChangesChoice::DontSave:
            clearAutoSaveIfConfigured(session, autosave);
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
// returns true once the configured chord is confirmed matched - F12 is
// unclaimed everywhere else in this dispatch chain, so there is nothing to
// fall through to whether or not a reference was found/opened (same
// silent-no-op contract EditorSession::openFile() itself guarantees on a
// stale/missing path). WI-04: takes EditorSession& (essentially every
// member). WI-10: gained shiftDown/ctrlDown parameters (previously neither
// was read - the pre-WI-10 check was a bare vkCode==VK_F12 comparison)
// since chordMatches() needs the full modifier state.
bool handleTagJumpKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, EditorSession& session,
                      RenderPipeline& renderPipeline, FindDialog& findDialog, const core::KeyBindings& keyBindings,
                      core::RecentFiles& recentFiles, const MenuBarHandles& menuHandles, CsvGridPane& csvGridPane,
                      const void*& csvGridPanePendingSessionToken) {
    if (!chordMatches(keyBindings, CommandId::TagJump, ctrlDown, shiftDown, false, vkCode)) {
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
                              renderPipeline, findDialog, recentFiles, menuHandles, csvGridPane,
                              csvGridPanePendingSessionToken);
    return true;
}

// Enter/Replace button while the replace edit has focus
// (FindReplaceDialogConfig::onReplaceCurrent, WI-18b - formerly FindDialogConfig::
// onReplaceCurrent, Phase 5b3b) - replaces state.currentMatches[state.currentMatchIndex] with
// `replacementTemplate` expanded against the match's capture groups, then
// re-runs state.currentQuery and jumps to whichever match now occupies the
// same index (clamped, since a replace can only ever remove exactly one
// match, so the count shrinks by at most 1 - see the plan's Context section
// for the out-of-bounds trace). WI-04: takes EditorSession& (document/
// dispatcher/findReplaceState/selection/viewport - 5 members).
template <typename MatchCountSink>
void replaceCurrentMatch(std::u16string_view replacementTemplate, HWND hwnd, EditorSession& session,
                         RenderPipeline& renderPipeline, MatchCountSink& sink) {
    auto& state = session.findReplaceState();
    if (state.currentMatches.empty()) {
        return;
    }
    const std::size_t    replacedIndex = state.currentMatchIndex;
    const Match&           match         = state.currentMatches[replacedIndex];
    const std::u16string expanded = expandReplacementTemplate(replacementTemplate, session.document(), match);
    session.dispatcher().dispatch(std::make_unique<ReplaceRangeCommand>(match.range, expanded));

    refreshMatches(state.currentQuery, session, renderPipeline, sink);
    if (state.currentMatches.empty()) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
        return;
    }
    state.currentMatchIndex = std::min(replacedIndex, state.currentMatches.size() - 1);
    jumpToMatch(hwnd, session, renderPipeline, sink);
}

// Ctrl+Enter/Replace All button while the replace edit has focus
// (FindReplaceDialogConfig::onReplaceAll, WI-18b - formerly FindDialogConfig::
// onReplaceAll, Phase 5b3b) - replaces every current match atomically as one undo step.
// state.currentMatches is already in ascending document order
// (SearchService::findAll()'s guarantee - search_service.h), matching
// applyEditsWithCumulativeShift()'s ordering requirement
// (cumulative_shift_edit.h) directly, so no re-sort is needed before
// building the PerCursorEdit vector. Each replacement's capture-group
// expansion is resolved against the pre-edit document (expandReplacementTemplate()'s
// contract, replacement.h) before any edit is applied.
//
// Does not re-search afterward: match highlighting is simply cleared, same
// as closeFindDialog() - re-matching the just-replaced text against the same
// query would be confusing (looks like the replace silently didn't work)
// rather than informative. WI-04: takes EditorSession& (document/dispatcher/
// selection/findReplaceState - 4 members).
template <typename MatchCountSink>
void replaceAllMatches(std::u16string_view replacementTemplate, HWND hwnd, EditorSession& session,
                       RenderPipeline& renderPipeline, MatchCountSink& sink) {
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
    sink.setMatchCount(0, 0);
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

// Handles WM_MOUSEMOVE while a drag is in progress (onMouseDrag). Pulled
// out of wireNormalMode() for the same cognitive-complexity-budget reason
// as handleMouseDownEvent()/handleHScrollEvent() above. `isDraggingMinimap`
// is read-only here (only handleMouseDownEvent()/tryHandleMinimapClick()
// ever set it) - taken by value rather than by reference.
void handleMouseDragEvent(HWND hwnd, std::int32_t x, std::int32_t y, EditorSession& session,
                          RenderPipeline& renderPipeline, bool isDraggingMinimap) {
    // Highest priority: a minimap drag never falls through to
    // rectangularAnchor/altCursorAnchor/ordinary text-drag handling below -
    // it tracks by Y alone (Phase 7v, see minimapLineAtY()'s comment on why
    // X is ignored once a drag has started).
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
    // Checked in this priority order: a rectangular-selection drag (Phase
    // 4b8a, Shift+Alt+drag) takes precedence over a plain Alt+drag cursor
    // extension (Phase 4b6d), which takes precedence over the default
    // drag-extends-primary-selection behavior (Phase 4b3). At most one of
    // rectangularAnchor/altCursorAnchor is ever meaningfully set at a time -
    // see neomifes::app::dispatchMouseDown()'s comment for why a
    // Shift+Alt+click that turns into a drag safely supersedes whatever the
    // down-click itself did.
    session.freeCursorVirtualColumns().reset();
    bool changed = false;
    const Document& document    = session.document();
    auto&           rectangularAnchor = session.rectangularAnchor();
    auto&           altCursorAnchor   = session.altCursorAnchor();
    if (rectangularAnchor) {
        session.selection().setRectangularSelection(*rectangularAnchor, *hit, document);
        // The rectangle just replaced the entire cursor set, so any
        // altCursorAnchor left over from an earlier plain Alt+click no
        // longer identifies a real cursor - clear it so the next unrelated
        // Shift+Alt+click doesn't silently no-op.
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

// WI-12: Ctrl+A (select all) - a pure selection change, not an ICommand (no
// document mutation, so there is nothing for Undo to reverse).
void dispatchSelectAllCommand(HWND hwnd, RenderPipeline& renderPipeline, EditorSession& session) {
    session.selection().selectAll(session.document());
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

// WI-12: Ctrl+D / Alt+Up / Alt+Down / Ctrl+Shift+K share the same "compute
// a core::LineOperationPlan (core/line_operations.h), skip if it's a no-op,
// dispatch it as one core::LineOperationCommand" shape - factored into this
// one helper so the 4 call sites below stay a single line each.
void dispatchLineOperation(core::LineOperationPlan plan, std::string_view id, HWND hwnd,
                           RenderPipeline& renderPipeline, EditorSession& session) {
    if (plan.edits.empty()) {
        return;  // fully blocked (move-line) or nothing to do - no Undo step
    }
    std::vector<Cursor> before(session.selection().cursors().begin(), session.selection().cursors().end());
    session.dispatcher().dispatch(std::make_unique<core::LineOperationCommand>(
        std::move(plan.edits), std::move(before), std::move(plan.cursorMappings), id));
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

void dispatchDuplicateLineCommand(HWND hwnd, RenderPipeline& renderPipeline, EditorSession& session) {
    dispatchLineOperation(core::computeDuplicateLineEdits(session.document(), session.selection().cursors()),
                          "edit.duplicateLine", hwnd, renderPipeline, session);
}

void dispatchDeleteLineCommand(HWND hwnd, RenderPipeline& renderPipeline, EditorSession& session) {
    dispatchLineOperation(core::computeDeleteLineEdits(session.document(), session.selection().cursors()),
                          "edit.deleteLine", hwnd, renderPipeline, session);
}

// `moveDown` selects Alt+Down (true) vs Alt+Up (false).
void dispatchMoveLineCommand(bool moveDown, HWND hwnd, RenderPipeline& renderPipeline, EditorSession& session) {
    dispatchLineOperation(
        core::computeMoveLineEdits(session.document(), session.selection().cursors(), moveDown),
        moveDown ? "edit.moveLineDown" : "edit.moveLineUp", hwnd, renderPipeline, session);
}

// WI-15d ("JSON整形", command palette only - see this WI's plan for why no
// CommandId/keybinding/menu entry, same "edit.duplicateLine" precedent
// dispatchDuplicateLineCommand() above follows). Reparses the WHOLE
// document (not session.jsonTree(), which may be stale or never populated -
// this command works independent of whether ui::JsonTreePane has ever been
// opened for this session, see the plan's own design note) and, if it's
// valid JSON, replaces the entire document with formatJsonNode()'s output
// as ONE undoable core::ReplaceRangeCommand - the first place in this
// codebase core::ReplaceRangeCommand is used for a whole-document rewrite
// rather than a targeted in-place edit.
void dispatchJsonFormatCommand(HWND hwnd, RenderPipeline& renderPipeline, EditorSession& session) {
    const auto tree = jsontree::parseJsonTree(session.document());
    if (!tree.has_value()) {
        showJsonFormatInvalidDialog(hwnd);
        return;
    }
    const std::u16string formatted = jsontree::formatJsonNode(*tree);
    const document::TextPos length = session.document().length();
    const document::TextRange fullRange{.start = 0, .end = length};
    const std::u16string current = session.document().snapshot()->extract(fullRange);
    if (formatted == current) {
        return;  // already formatted - no Undo step, no dirty-flag flip
    }
    session.dispatcher().dispatch(std::make_unique<core::ReplaceRangeCommand>(fullRange, formatted));
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

// WI-15d ("JSON検証", command palette only, same reasoning as
// dispatchJsonFormatCommand() above). Read-only - never edits the
// document, only reports jsontree::validateJson()'s verdict and (on
// failure) moves the cursor to the error's best-effort position via the
// same jumpToOutlinePosition() every OTHER structural-jump feature in this
// file already uses (OutlinePane/JsonTreePane/CsvGridPane).
void dispatchJsonValidateCommand(HWND hwnd, RenderPipeline& renderPipeline, EditorSession& session) {
    const auto error = jsontree::validateJson(session.document());
    if (!error.has_value()) {
        showJsonValidDialog(hwnd);
        return;
    }
    jumpToOutlinePosition(error->position, hwnd, session, renderPipeline);
    showJsonValidationErrorDialog(hwnd, error->message);
}

// WI-15e ("JSONPathを評価", command palette only via JsonPathBar - see this
// WI's plan for why no CommandId/keybinding/menu entry). Called from
// JsonPathBar's onSubmit (buildJsonPathBarConfig() below), NOT directly from
// buildCommandRegistry()'s action lambda - that lambda only opens the bar
// (json.jsonpath needs a user-supplied expression string first, unlike
// json.format/json.validate above which act on the whole document with no
// argument). Reparses the whole document (same "never trust
// session.jsonTree(), which may be stale/unpopulated" reasoning
// dispatchJsonFormatCommand() already documents) rather than the parsed
// path's own tree, then jumps the cursor to the FIRST match only - cycling
// through multiple matches (F3-style) or highlighting all of them in
// JsonTreePane is explicitly out of this WI's scope (see the plan's own
// "非スコープ" section).
void dispatchJsonPathCommand(std::u16string_view expression, HWND hwnd, RenderPipeline& renderPipeline,
                             EditorSession& session) {
    const auto tree = jsontree::parseJsonTree(session.document());
    if (!tree.has_value()) {
        showJsonPathInvalidJsonDialog(hwnd);
        return;
    }
    const auto path = jsontree::parseJsonPath(expression);
    if (!path.has_value()) {
        showJsonPathSyntaxErrorDialog(hwnd, expression);
        return;
    }
    const auto matches = jsontree::evaluateJsonPath(*tree, *path);
    if (matches.empty()) {
        showJsonPathNoMatchDialog(hwnd);
        return;
    }
    jumpToOutlinePosition(matches.front()->startPos, hwnd, session, renderPipeline);
}

// WI-15i ("XPathを評価", command palette only via the SAME JsonPathBar
// instance JSONPath already uses - see buildJsonPathBarConfig()'s own
// comment for how onSubmit tells the two apart). Direct sibling of
// dispatchJsonPathCommand() above, same "reparse the whole document rather
// than trust a possibly-stale cached tree, jump to the first match only"
// shape. Unlike dispatchJsonPathCommand(), the "not well-formed" check is
// `tree.root.kind == Error`, not `!tree.has_value()` - xmltree::
// parseXmlTree() never returns std::optional (see xml_tree.h's own header
// comment on why XML's contract differs from JSON's fail-fast one).
void dispatchXPathCommand(std::u16string_view expression, HWND hwnd, RenderPipeline& renderPipeline,
                          EditorSession& session) {
    const auto tree = xmltree::parseXmlTree(session.document());
    if (tree.root.kind == xmltree::XmlNodeKind::Error) {
        showXPathInvalidXmlDialog(hwnd);
        return;
    }
    const auto path = xmltree::parseXPath(expression);
    if (!path.has_value()) {
        showXPathSyntaxErrorDialog(hwnd, expression);
        return;
    }
    const auto matches = xmltree::evaluateXPath(tree.root, *path);
    if (matches.empty()) {
        showXPathNoMatchDialog(hwnd);
        return;
    }
    jumpToOutlinePosition(matches.front()->startPos, hwnd, session, renderPipeline);
}

// WI-17c ("Git: Refresh Diff Markers", command palette only - see this WI's
// plan for why no CommandId/keybinding/menu entry, same "edit.duplicateLine"
// precedent dispatchDuplicateLineCommand() above follows). The ONLY call
// site of EditorSession::beginGitDiffIndexing() in this codebase - no
// automatic trigger (on save/open/edit) exists yet, see build_plan.md's
// WI-17c section for why this is deliberately deferred rather than missing
// by oversight. A no-op for an Untitled session (beginGitDiffIndexing()'s
// own contract) - no dialog for that case, unlike json.format/json.validate,
// to keep this WI's scope narrow (a silent no-op matches
// beginGitDiffIndexing()'s own "nothing to diff" semantics rather than
// treating it as an error).
void dispatchGitRefreshDiffCommand(EditorSession& session, git::GitDiffWorker& worker) {
    session.beginGitDiffIndexing(worker);
}

// WI-17e: shows/refreshes GitPane for the active session's repository -
// same role as refreshOutlinePane()/refreshJsonTreePane() above, but async
// like the latter (Workspace's Git status is worker-backed). Unlike
// refreshJsonTreePane()'s "already cached -> show immediately, no request"
// shortcut, this ALWAYS shows whatever Workspace::gitStatus() currently
// holds right away (even if stale or nullopt/"never requested" - the
// placeholder rows git_pane_bridge.h's buildGitPaneItems() produces for
// nullopt make an empty/stale display self-explanatory rather than
// confusing) and then kicks off a fresh request unless one is already in
// flight - matching this WI's own plan §6 "toggle off/on is the manual
// refresh path" contract (no separate "Git: Refresh Status" command).
void refreshGitPane(Workspace& workspace, GitPane& gitPane, git::GitStatusWorker& gitStatusWorker) {
    gitPane.showWith(neomifes::app::buildGitPaneItems(workspace.gitStatus()));
    if (!workspace.gitStatusInFlight()) {
        workspace.beginGitStatusIndexing(gitStatusWorker);
    }
}

// "git.togglePane" command's action body (WI-17e, command-palette only -
// see this WI's plan for why no CommandId/keybinding/menu entry, same
// reasoning dispatchGitRefreshDiffCommand()'s own comment gives for
// git.refreshDiff). Same toggle shape as handleOutlineKey() above.
void toggleGitPane(HWND hwnd, RenderPipeline& renderPipeline, Workspace& workspace,
                   const neomifes::ui::OutlinePane& outlinePane, JsonTreePane& jsonTreePane, GitPane& gitPane,
                   git::GitStatusWorker& gitStatusWorker) {
    if (gitPane.isVisible()) {
        gitPane.hide();
    } else {
        refreshGitPane(workspace, gitPane, gitStatusWorker);
    }
    syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
}

// "git.toggleDiffView" command's action body (WI-17f, command-palette only -
// roadmap's suggested Alt+D is deliberately not wired, same reasoning
// git.refreshDiff/git.togglePane already established). Closing reuses
// syncViewForActiveSession() itself (see that function's own WI-17f
// comment - it already clears the Diff view as a side effect of restoring
// normal session-active state) rather than a dedicated close function -
// opening is the ONLY place that ever touches diffViewDocument's own
// storage, see this file's normal_mode_wiring.h header comment on
// wireNormalMode() for why.
void toggleDiffView(HWND hwnd, RenderPipeline& renderPipeline, Workspace& workspace, FindDialog& findDialog,
                    CsvGridPane& csvGridPane, const void*& csvGridPanePendingSessionToken,
                    std::optional<document::Document>& diffViewDocument) {
    if (renderPipeline.isDiffViewActive()) {
        syncViewForActiveSession(hwnd, renderPipeline, workspace.active(), findDialog, csvGridPane,
                                 csvGridPanePendingSessionToken);
        return;
    }
    EditorSession& session = workspace.active();
    const auto     path    = session.pathIfNamed();
    if (!path.has_value()) {
        return;  // Untitled - nothing to diff (dispatchGitRefreshDiffCommand()'s own precedent)
    }
    auto repo = git::GitRepository::discover(path->parent_path());
    if (!repo.has_value()) {
        return;
    }
    const auto lines = repo->unifiedDiffAgainstHead(*path, session.document());
    if (!lines.has_value()) {
        return;
    }
    diffViewDocument.emplace();
    diffViewDocument->insertText(0, neomifes::app::buildDiffViewDocumentText(*lines));
    renderPipeline.setDocument(&*diffViewDocument);
    renderPipeline.setDiffViewLineRegions(neomifes::app::buildDiffViewLineMarkers(*lines));
    renderPipeline.setDiffViewActive(true);
    // Deliberately NOT syncRenderStateAndInvalidate(hwnd, renderPipeline,
    // session) - that pushes the LIVE session's own topLine/leftColumn/
    // cursor positions, which refer to the live document's line numbering
    // and would be meaningless (or land far past the end) against the
    // synthesized diff document's own, unrelated line count. A fresh view
    // always starts at the top with no cursor to show (read-only).
    renderPipeline.setTopLine(0);
    renderPipeline.setLeftColumn(0);
    renderPipeline.setCursorVisuals({});
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// WI-12: Ctrl+A/Ctrl+D/Ctrl+Shift+K. Deliberately hardcoded VK_* comparisons
// - NOT routed through core::KeyBindings/chordMatches() like
// Copy/Cut/Paste/Undo/Redo in handleClipboardOrUndoRedoKey() below - these 5
// features (this function plus Alt+Up/Alt+Down, handled separately in
// handleSysKeyDownEvent() since Alt implies WM_SYSKEYDOWN, not WM_KEYDOWN)
// stay outside the remappable CommandId system entirely: build_plan.md's
// WI-12 DoD does not ask for preset customization of them, and they are
// closer in spirit to editor_input.cpp's already-hardcoded continuous-
// editing keys (arrows/Home/End/Backspace/Delete) than to the menu/palette-
// facing commands core::KeyBindings governs.
[[nodiscard]] bool handleLineEditKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, Workspace& workspace,
                                     RenderPipeline& renderPipeline) {
    if (!ctrlDown) {
        return false;
    }
    EditorSession& session = workspace.active();
    if (!shiftDown && vkCode == 'A') {
        dispatchSelectAllCommand(hwnd, renderPipeline, session);
        return true;
    }
    if (!shiftDown && vkCode == 'D') {
        dispatchDuplicateLineCommand(hwnd, renderPipeline, session);
        return true;
    }
    if (shiftDown && vkCode == 'K') {
        dispatchDeleteLineCommand(hwnd, renderPipeline, session);
        return true;
    }
    return false;
}

// WI-07 step2: handleClipboardKey()/handleSaveKey()/handleOpenKey()/
// handleNewDocumentKey()/handleTabSwitchKey()/handleTabCloseKey() (Ctrl+C/
// X/V, Ctrl+S/Shift+S, Ctrl+O, Ctrl+N, Ctrl+Tab/Shift+Tab/PgUp/PgDn/1-9,
// Ctrl+W) used to live here as individual vkCode-matching functions in this
// file's dispatch chain. Save/Open/New/tab-switch/tab-close are now reached
// via the global accelerator table (command_dispatch.h's
// buildAcceleratorTable(), wired in main.cpp's runMessageLoop()) -> WM_COMMAND
// -> wireNormalMode()'s cfg.onCommand -> dispatchCommand(), which owns their
// bodies now (moved verbatim, see dispatchCommand()'s own comment below).
// Ctrl+C/X/V stayed OFF the accelerator table (native WC_EDIT conflict, see
// command_dispatch.h's top comment) but their bodies moved into
// dispatchCommand() too, reached via handleClipboardOrUndoRedoKey() below
// instead of the accelerator table.

// Copy/Cut/Paste/Undo/Redo (WI-07 step2): also NOT accelerator-routed (see
// command_dispatch.h's top comment - native WC_EDIT controls claim these
// combinations themselves). A dedicated handleXxxKey()-shaped function (same
// convention as handleFindDialogKey() etc.) so handleKeyDownEvent() itself
// doesn't have to carry this compound condition + CommandId lookup directly
// (keeps handleKeyDownEvent()'s own cognitive complexity under clang-tidy's
// threshold). WI-10: replaces the old clipboardOrUndoCommandForKey() switch
// (which assumed Ctrl+<letter> literals) with a loop over chordMatches()
// against the 5 candidate CommandIds - each is checked against whatever
// chord(s) keyBindings actually has configured for it, not a hardcoded
// letter. Gained a shiftDown parameter for the same reason (chordMatches()
// needs the full modifier state).
[[nodiscard]] bool handleClipboardOrUndoRedoKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown,
                                                Workspace& workspace, RenderPipeline& renderPipeline,
                                                FindDialog& findDialog, const core::KeyBindings& keyBindings,
                                                core::RecentFiles& recentFiles, const MenuBarHandles& menuHandles,
                                                const core::Settings& settings, AutosaveContext& autosave,
                                                CsvGridPane& csvGridPane,
                                                const void*& csvGridPanePendingSessionToken,
                                                git::GitDiffWorker& gitDiffWorker,
                                                SessionManager& sessionManager) {
    constexpr std::array<CommandId, 5> kCandidates{CommandId::Copy, CommandId::Cut, CommandId::Paste,
                                                    CommandId::Undo, CommandId::Redo};
    for (const CommandId candidate : kCandidates) {
        if (!chordMatches(keyBindings, candidate, ctrlDown, shiftDown, false, vkCode)) {
            continue;
        }
        const CommandDispatchContext ctx{.hwnd                           = hwnd,
                                         .workspace                      = workspace,
                                         .renderPipeline                 = renderPipeline,
                                         .findDialog                        = findDialog,
                                         .recentFiles                    = recentFiles,
                                         .menuHandles                    = menuHandles,
                                         .autosave                       = autosave,
                                         .settings                       = settings,
                                         .csvGridPane                    = csvGridPane,
                                         .csvGridPanePendingSessionToken = csvGridPanePendingSessionToken,
                                         .gitDiffWorker                  = gitDiffWorker,
                                         .sessionManager                 = sessionManager};
        dispatchCommand(candidate, ctx);
        return true;
    }
    return false;
}

// Toggles Insert/Overwrite mode (WI-07 step5) - bare VK_INSERT, no
// modifiers by default. Also NOT accelerator-routed, for the same reason
// Copy/Cut/Paste/Undo/Redo aren't (see handleClipboardOrUndoRedoKey()'s own
// comment above): a native WC_EDIT control - like the overlay widgets' own
// text fields - supports a built-in overtype toggle on a bare Insert
// keypress, which a global accelerator entry would intercept before it ever
// reached the focused control.
[[nodiscard]] bool handleOverwriteToggleKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown,
                                            Workspace& workspace, RenderPipeline& renderPipeline,
                                            FindDialog& findDialog, const core::KeyBindings& keyBindings,
                                            core::RecentFiles& recentFiles, const MenuBarHandles& menuHandles,
                                            const core::Settings& settings, AutosaveContext& autosave,
                                            CsvGridPane& csvGridPane,
                                            const void*& csvGridPanePendingSessionToken,
                                            git::GitDiffWorker& gitDiffWorker,
                                            SessionManager& sessionManager) {
    if (!chordMatches(keyBindings, CommandId::ToggleOverwriteMode, ctrlDown, shiftDown, false, vkCode)) {
        return false;
    }
    const CommandDispatchContext ctx{.hwnd                           = hwnd,
                                     .workspace                      = workspace,
                                     .renderPipeline                 = renderPipeline,
                                     .findDialog                        = findDialog,
                                     .recentFiles                    = recentFiles,
                                     .menuHandles                    = menuHandles,
                                     .autosave                       = autosave,
                                     .settings                       = settings,
                                     .csvGridPane                    = csvGridPane,
                                     .csvGridPanePendingSessionToken = csvGridPanePendingSessionToken,
                                     .gitDiffWorker                  = gitDiffWorker,
                                     .sessionManager                 = sessionManager};
    dispatchCommand(CommandId::ToggleOverwriteMode, ctx);
    return true;
}

// Handles WM_KEYDOWN end-to-end: Ctrl+C/X/V/Z/Y first (via dispatchCommand(),
// WI-07 step2), falling through to the regular movement/edit path otherwise.
// Pulled all the way out of wireNormalMode's onKeyDown lambda body (not just
// the branching logic) - a lambda defined inline inside wireNormalMode has
// its body counted toward wireNormalMode's own cognitive complexity even
// when the branching it does is itself delegated to helper functions, so
// leaving any nontrivial control flow in the lambda itself re-creates the
// problem neomifes::app::dispatchMouseDown() was extracted to avoid. WI-04:
// takes EditorSession& (all session-scoped state) plus the widget/mode
// references that are NOT part of any one EditorSession (see this file's
// EditorSession member-placement notes for why FindDialog/CommandPalette/
// GotoLineBar/GrepBar/OutlinePane/freeCursorModeEnabled stay separate).
void handleKeyDownEvent(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, Workspace& workspace,
                        RenderPipeline& renderPipeline, FindDialog& findDialog, FindReplaceDialog& findReplaceDialog,
                        CommandPalette& commandPalette,
                        GotoLineBar& gotoLineBar, GrepBar& grepBar, OutlinePane& outlinePane,
                        JsonTreePane& jsonTreePane, GitPane& gitPane,
                        std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker,
                        std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker,
                        const void*& jsonTreePanePendingSessionToken, CsvGridPane& csvGridPane,
                        std::optional<csvmode::CsvModelWorker>& csvModelWorker,
                        const void*& csvGridPanePendingSessionToken, bool freeCursorModeEnabled, bool imeComposing,
                        const core::KeyBindings& keyBindings, core::RecentFiles& recentFiles,
                        const MenuBarHandles& menuHandles, const core::Settings& settings,
                        AutosaveContext& autosave, git::GitDiffWorker& gitDiffWorker,
                        SessionManager& sessionManager) {
    // WI-06: checked before EVERYTHING else in this dispatch chain (even
    // handleFreeCursorRightArrow()) - while an IME is actively composing,
    // Windows still delivers WM_KEYDOWN for some keys (arrows, Enter, Escape
    // are consumed by the IME itself before this fires, but not
    // universally), and none of this chain's handlers (tab switching,
    // Undo/Redo, cursor movement, ...) should run mid-composition. A side
    // effect: Ctrl+Tab/other tab-switch keys are also suppressed while
    // composing (see wireNormalMode()'s header comment on imeComposing).
    if (imeComposing) {
        return;
    }
    // WI-17f: checked next, before any movement/edit handling below - the
    // Diff view shows a synthesized, non-editable document; every key
    // except Escape (which closes it, restoring the live document via the
    // same syncViewForActiveSession() the toggle command's own close path
    // uses) is silently swallowed here rather than reaching
    // session.document() through the chain below, which would otherwise
    // mutate the REAL (currently invisible) document while the screen kept
    // showing the unrelated diff content.
    if (renderPipeline.isDiffViewActive()) {
        if (vkCode == VK_ESCAPE) {
            syncViewForActiveSession(hwnd, renderPipeline, workspace.active(), findDialog, csvGridPane,
                                     csvGridPanePendingSessionToken);
        }
        return;
    }
    EditorSession& session = workspace.active();
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
    if (handleCommandPaletteKey(vkCode, shiftDown, ctrlDown, commandPalette, keyBindings)) {
        return;
    }
    if (handleGrepKey(vkCode, shiftDown, ctrlDown, grepBar, keyBindings)) {
        return;
    }
    if (handleOutlineKey(hwnd, vkCode, shiftDown, ctrlDown, session, outlinePane, jsonTreePane, gitPane,
                        renderPipeline, keyBindings)) {
        return;
    }
    if (handleJsonTreeKey(hwnd, vkCode, shiftDown, ctrlDown, session, jsonTreePane, outlinePane, gitPane,
                          jsonTreeWorker, xmlTreeWorker, renderPipeline, keyBindings,
                          jsonTreePanePendingSessionToken)) {
        return;
    }
    if (handleCsvGridKey(vkCode, shiftDown, ctrlDown, session, csvGridPane, csvModelWorker, keyBindings,
                         csvGridPanePendingSessionToken)) {
        return;
    }
    if (handleGotoLineKey(vkCode, shiftDown, ctrlDown, gotoLineBar, keyBindings)) {
        return;
    }
    if (handleBookmarkKey(hwnd, vkCode, shiftDown, ctrlDown, session, renderPipeline, keyBindings)) {
        return;
    }
    if (handleTagJumpKey(hwnd, vkCode, shiftDown, ctrlDown, session, renderPipeline, findDialog, keyBindings,
                         recentFiles, menuHandles, csvGridPane, csvGridPanePendingSessionToken)) {
        return;
    }
    // WI-07 step2: Save/Open/New/tab-switch/tab-close no longer appear in
    // this chain - TranslateAcceleratorW (main.cpp's runMessageLoop(),
    // command_dispatch.h's buildAcceleratorTable()) claims their WM_KEYDOWN
    // before it would ever reach here, routing to dispatchCommand() via
    // WM_COMMAND instead (wireNormalMode()'s cfg.onCommand). See
    // command_dispatch.h's top comment for why Find/Grep/CommandPalette/
    // Outline/GotoLine/Bookmark/TagJump (checked above) were NOT moved the
    // same way.
    if (handleFindDialogKey(hwnd, vkCode, shiftDown, ctrlDown, findDialog, findReplaceDialog, session, renderPipeline,
                         keyBindings)) {
        return;
    }
    // Copy/Cut/Paste/Undo/Redo (WI-07 step2): also NOT accelerator-routed
    // (see command_dispatch.h's top comment - native WC_EDIT controls claim
    // these combinations themselves). Kept as its own handleXxxKey()-shaped
    // function (same convention as handleFindDialogKey() etc. above) rather
    // than inline here, so this function's own cognitive complexity stays
    // under clang-tidy's threshold - the compound condition + CommandId
    // lookup would otherwise count directly against it.
    if (handleClipboardOrUndoRedoKey(hwnd, vkCode, shiftDown, ctrlDown, workspace, renderPipeline, findDialog,
                                     keyBindings, recentFiles, menuHandles, settings, autosave, csvGridPane,
                                     csvGridPanePendingSessionToken, gitDiffWorker, sessionManager)) {
        return;
    }
    // WI-12: Ctrl+A/Ctrl+D/Ctrl+Shift+K - see handleLineEditKey()'s own
    // comment for why these stay outside the KeyBindings-driven chain above.
    if (handleLineEditKey(hwnd, vkCode, shiftDown, ctrlDown, workspace, renderPipeline)) {
        return;
    }
    if (handleOverwriteToggleKey(hwnd, vkCode, shiftDown, ctrlDown, workspace, renderPipeline, findDialog, keyBindings,
                                 recentFiles, menuHandles, settings, autosave, csvGridPane,
                                 csvGridPanePendingSessionToken, gitDiffWorker, sessionManager)) {
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
// (neomifes::app::handleChar()/applyOverwriteChar()) whenever a virtual-
// column count is pending. Pulled out of wireNormalMode's onChar lambda for
// the same cognitive-complexity reason as handleKeyDownEvent() above. WI-04:
// takes EditorSession& (dispatcher/selection/viewport/document/
// freeCursorVirtualColumns - 5 members).
// WI-06: `imeComposing` guard here is a defensive backstop, not the primary
// mechanism - by design, committed IME text never reaches WM_CHAR at all
// (MainWindow's WM_IME_COMPOSITION handler never forwards to
// DefWindowProcW, see main_window.h's WI-06 header comment), so this
// early-out should never actually trigger in practice. Kept anyway in case
// some IME/keyboard-layout combination produces an unexpected WM_CHAR while
// composing.
// WI-07 step5: branches on session.overwriteMode() - handleChar() while in
// Insert mode (unchanged), applyOverwriteChar() while in Overwrite mode.
void handleCharEvent(HWND hwnd, wchar_t ch, EditorSession& session, RenderPipeline& renderPipeline,
                     bool imeComposing) {
    if (imeComposing) {
        return;
    }
    // WI-17f: same reasoning as handleKeyDownEvent()'s own guard - typed
    // characters must never reach the (currently invisible) real document
    // while the Diff view is showing.
    if (renderPipeline.isDiffViewActive()) {
        return;
    }
    auto& virtualColumns = session.freeCursorVirtualColumns();
    if (virtualColumns) {
        applyFreeCursorChar(ch, *virtualColumns, hwnd, session, renderPipeline);
        virtualColumns.reset();
        return;
    }
    const bool changed = session.overwriteMode()
                             ? neomifes::app::applyOverwriteChar(ch, session.dispatcher(), session.selection(),
                                                                  session.viewport(), session.document())
                             : neomifes::app::handleChar(ch, session.dispatcher(), session.selection(),
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
    // WI-12: plain Alt+Up/Alt+Down (no Shift) move the current line(s) -
    // checked before the Shift+Alt-only rectangular-selection logic below
    // (which early-returns on !shiftDown), since this is the one case in
    // this function that must NOT require Shift. Always reports "handled"
    // (true) even when computeMoveLineEdits() turned out to be a no-op
    // (e.g. already at the document boundary) - the keystroke is still
    // fully consumed, matching how a boundary Backspace/Delete press is
    // handled elsewhere in this codebase.
    if (!shiftDown && (vkCode == VK_UP || vkCode == VK_DOWN)) {
        dispatchMoveLineCommand(vkCode == VK_DOWN, hwnd, renderPipeline, session);
        return true;
    }
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

// Builds the FindDialogConfig callbacks (Phase 5b3a) - pulled out of
// wireNormalMode's onDeferredInit lambda for the same cognitive-complexity
// reason documented above handleKeyDownEvent(). All captured references
// outlive the returned FindDialogConfig (they are wWinMain-scope locals; the
// config itself is only used immediately, inside findDialog.create()). WI-04:
// takes EditorSession& (document/dispatcher/selection/viewport/
// findReplaceState - 5 members). WI-05 step 1: takes Workspace& instead -
// every callback below is STORED inside FindDialog and invoked later
// (whenever the user next interacts with the Find dialog), so each one
// resolves workspace.active() fresh at its own invocation time rather than
// capturing a single session fixed when this function ran (see
// wireNormalMode()'s header comment for why).
FindDialogConfig buildFindDialogConfig(HWND hwnd, Workspace& workspace, RenderPipeline& renderPipeline,
                                 FindDialog& findDialog, SearchHistory& searchHistory,
                                 FindReplaceDialog& findReplaceDialog) {
    FindDialogConfig config{};
    config.onQueryChanged = [hwnd, &workspace, &renderPipeline, &findDialog](std::u16string_view query,
                                                                          bool caseSensitive, bool wholeWord,
                                                                          bool regex) {
        runFindQuery(query, caseSensitive, wholeWord, regex, hwnd, workspace.active(), renderPipeline, findDialog);
    };
    // Recording happens here (Enter/F3 while the find edit itself has
    // focus), not inside navigateToMatch() - this is the one call site that
    // covers "the user typed a query and asked to act on it" for every
    // realistic flow (Ctrl+F -> type -> Enter). Subsequent F3 presses after
    // focus has moved to the document (handleFindDialogKey()) or via the
    // command palette's Find Next/Previous re-record the SAME
    // already-recorded query - record()'s dedupe makes that a harmless
    // no-op move-to-front rather than a second entry, so those other call
    // sites don't also need searchHistory threaded through them (Phase 5c5).
    config.onHistoryOlder = [&findDialog, &searchHistory](std::u16string_view currentText) {
        if (const auto older = searchHistory.older(currentText)) {
            findDialog.setQueryText(*older);
        }
    };
    config.onHistoryNewer = [&findDialog, &searchHistory](std::u16string_view currentText) {
        if (const auto newer = searchHistory.newer(currentText)) {
            findDialog.setQueryText(*newer);
        }
    };
    config.onFindNext = [hwnd, &workspace, &renderPipeline, &findDialog, &searchHistory]() {
        EditorSession& session = workspace.active();
        searchHistory.record(session.findReplaceState().currentQuery.pattern);
        navigateToMatch(true, hwnd, session, renderPipeline, findDialog);
    };
    config.onFindPrevious = [hwnd, &workspace, &renderPipeline, &findDialog, &searchHistory]() {
        EditorSession& session = workspace.active();
        searchHistory.record(session.findReplaceState().currentQuery.pattern);
        navigateToMatch(false, hwnd, session, renderPipeline, findDialog);
    };
    config.onClosed = [hwnd, &findDialog, &workspace, &renderPipeline]() {
        closeFindDialog(hwnd, findDialog, workspace.active(), renderPipeline);
    };
    // WI-18b: FindDialog no longer has its own replace UI - Ctrl+H while the
    // find edit has focus opens the standalone dialog instead (see
    // FindDialogConfig::onReplaceRequested's own comment for why FindDialog
    // itself needs this callback rather than just relying on MainWindow's
    // global Ctrl+H handling).
    config.onReplaceRequested = [hwnd, &findReplaceDialog]() { findReplaceDialog.show(hwnd); };
    return config;
}

// WI-18b: FindReplaceDialog's counterpart to buildFindDialogConfig() above -
// same runFindQuery()/navigateToMatch()/replaceCurrentMatch()/
// replaceAllMatches() bodies (now templated on the match-count sink, see
// jumpToMatch()'s own comment), just handed `findReplaceDialog` instead of
// `findDialog`. Deliberately does NOT wire onHistoryOlder/onHistoryNewer
// (Ctrl+Up/Down search-history recall) - FindReplaceDialog's
// handleEditKeyDown() has no history keys to fire them from yet; Ctrl+F's
// FindDialog keeps full history support, this is a small, consciously accepted
// gap for the dialog rather than a silently dropped feature.
FindReplaceDialogConfig buildFindReplaceDialogConfig(HWND hwnd, Workspace& workspace, RenderPipeline& renderPipeline,
                                                      FindReplaceDialog& findReplaceDialog) {
    FindReplaceDialogConfig config{};
    config.onQueryChanged = [hwnd, &workspace, &renderPipeline, &findReplaceDialog](
                                std::u16string_view query, bool caseSensitive, bool wholeWord, bool regex) {
        runFindQuery(query, caseSensitive, wholeWord, regex, hwnd, workspace.active(), renderPipeline,
                    findReplaceDialog);
    };
    config.onFindNext = [hwnd, &workspace, &renderPipeline, &findReplaceDialog]() {
        navigateToMatch(true, hwnd, workspace.active(), renderPipeline, findReplaceDialog);
    };
    config.onFindPrevious = [hwnd, &workspace, &renderPipeline, &findReplaceDialog]() {
        navigateToMatch(false, hwnd, workspace.active(), renderPipeline, findReplaceDialog);
    };
    // Unlike closeFindDialog() (which also calls findDialog.hide()):
    // FindReplaceDialog::requestClose() already hides itself before
    // invoking this callback (see that method's own comment), so only the
    // match-highlighting/focus cleanup half is needed here.
    config.onClosed = [hwnd, &workspace, &renderPipeline]() {
        EditorSession& session = workspace.active();
        session.findReplaceState().currentMatches.clear();
        renderPipeline.setMatchVisuals({});
        ::SetFocus(hwnd);
        ::InvalidateRect(hwnd, nullptr, FALSE);
    };
    config.onReplaceCurrent = [hwnd, &workspace, &renderPipeline,
                               &findReplaceDialog](std::u16string_view replacementText) {
        replaceCurrentMatch(replacementText, hwnd, workspace.active(), renderPipeline, findReplaceDialog);
    };
    config.onReplaceAll = [hwnd, &workspace, &renderPipeline,
                           &findReplaceDialog](std::u16string_view replacementText) {
        replaceAllMatches(replacementText, hwnd, workspace.active(), renderPipeline, findReplaceDialog);
    };
    return config;
}

// Builds the command palette's static registry (Phase 5b3c, extended in
// Phase 4b8d/4b8e) - 9 entries, each re-exposing an already-implemented
// WI-10: derives a CommandDescriptor::keybindingLabel from the live
// keyBindings instead of a hardcoded literal (see buildCommandRegistry()'s
// own comment for why). Only the FIRST configured chord is shown - the
// label is display-only, never itself a dispatch path, so a command with 2+
// chords (e.g. tab.next) simply shows one representative binding. Empty
// string if unbound (no chords configured) or the stored chord string fails
// to parse (a hand-edited keybindings.json with a typo) - same "never
// crash, just don't display something" convention as this file's other
// best-effort UI-string helpers.
[[nodiscard]] std::u16string keybindingLabelFor(const core::KeyBindings& keyBindings,
                                                std::u16string_view chordId) {
    const auto chords = keyBindings.chordsFor(chordId);
    if (chords.empty()) {
        return u"";
    }
    const auto chord = neomifes::app::parseKeyChord(chords.front());
    if (!chord) {
        return u"";
    }
    return neomifes::app::keyChordToString(*chord);
}

// WI-14c: the year to assume for RFC 3164 syslog's year-less timestamp
// format (LogPatternRule::timestampFormat has no "%Y" for that rule - a
// property of the RFC itself, see log_pattern.h). Read from the wall clock
// at the moment "Log: Enable" is invoked, not cached - each invocation gets
// whatever year it is right now.
[[nodiscard]] int currentYear() noexcept {
    const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
    return static_cast<int>(std::chrono::year_month_day(today).year());
}

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
// separate (see handleFreeCursorRightArrow()'s comment on why). WI-05 step
// 1: takes Workspace& instead - every CommandDescriptor::action below is
// STORED inside CommandPalette and invoked whenever the user later picks
// that command, so each one resolves workspace.active() fresh at
// invocation time (see wireNormalMode()'s header comment for why). WI-08:
// takes core::Settings& and settingsPath - see the two convert-indentation
// commands (now reading settings.tabWidth instead of a hardcoded 4) and the
// new "settings.reload" command below. WI-10: also takes core::KeyBindings&/
// keyBindingsPath/platform::AcceleratorTableHandle&/ui::CommandPalette& -
// the 6 pre-existing labeled commands below now derive .keybindingLabel
// from the live keyBindings (keybindingLabelFor()) instead of a hardcoded
// literal (fixes the drift risk command_descriptor.h's own comment flagged:
// a hand-typed label had no connection to the actual binding), and 5 new
// "keybindings.*" commands let a reload/preset-switch rebuild BOTH
// accelTable and this very registry (for fresh labels) live, via
// commandPalette.setCommands() - see those commands' own comments below.
// WI-14c: appends the "Log: Enable/Disable/Toggle*/Show*/Jump*" command set
// (要件定義書§8 - 色分け/フィルタ/ERROR抽出/WARNING抽出/時系列ジャンプ, see
// this WI's plan for the full requirements-to-command mapping). Pulled out
// of buildCommandRegistry() into its own function purely to keep that
// function's cognitive complexity under clang-tidy's threshold - the ~20
// command registrations below (5 enable + disable + 7 filter toggles + 3
// filter presets + 2 jump commands) pushed buildCommandRegistry() over the
// limit on their own, same reasoning as every other "pulled out of X for the
// same cognitive-complexity reason" extraction already in this file.
void appendLogModeCommands(std::vector<CommandDescriptor>& commands, HWND hwnd, Workspace& workspace,
                           RenderPipeline& renderPipeline, std::optional<LogIndexWorker>& logIndexWorker,
                           const std::vector<LogPatternRule>& userLogPatterns) {
    // "Log: Enable (...)" - one command per candidate rule (explicit
    // override for when auto-detect picks the wrong rule or fails), drawn
    // from BOTH builtInLogPatterns() and userLogPatterns (WI-14d - a
    // pattern loaded from %APPDATA%\NeoMIFES\log_patterns\, see
    // log_pattern_file.h) - plus one auto-detect command. All
    // CommandId::None, palette-only - same footprint as
    // edit.convertTabsToSpaces/view.theme.* in buildCommandRegistry()
    // (build_plan.md's established "don't invent a dedicated UI surface
    // for something the palette already covers" precedent for this file).
    // logIndexWorker is empty until wireNormalMode()'s onDeferredInit
    // lambda emplace()s it (see normal_mode_wiring.h's own comment) - the
    // null check below is this WI's only defense against a command
    // somehow firing before that happens.
    auto appendEnableCommand = [&commands, &workspace, &logIndexWorker](const LogPatternRule& rule) {
        commands.push_back(CommandDescriptor{
            .id              = std::u16string(u"logmode.enable.") + rule.id,
            .title           = std::u16string(u"Log: Enable (") + rule.displayName + u")",
            .keybindingLabel = u"",
            .commandId       = CommandId::None,
            .action = [&workspace, &logIndexWorker, rule]() {
                if (!logIndexWorker) {
                    return;
                }
                workspace.active().beginLogIndexing(*logIndexWorker, rule, currentYear());
            }});
    };
    for (const LogPatternRule& rule : builtInLogPatterns()) {
        appendEnableCommand(rule);
    }
    for (const LogPatternRule& rule : userLogPatterns) {
        appendEnableCommand(rule);
    }
    commands.push_back(CommandDescriptor{
        .id = u"logmode.enable.auto", .title = u"Log: Enable (Auto-Detect)", .keybindingLabel = u"",
        .commandId = CommandId::None,
        // WI-14d: `candidates` combines builtInLogPatterns() with
        // userLogPatterns so auto-detect can match a user-supplied format
        // too, not just the 4 built-ins - see detectLogPatternRule()'s own
        // header comment for why it takes a candidate span at all.
        .action = [hwnd, &workspace, &logIndexWorker, &userLogPatterns]() {
            if (!logIndexWorker) {
                return;
            }
            EditorSession& session = workspace.active();
            std::vector<LogPatternRule> candidates = builtInLogPatterns();
            candidates.insert(candidates.end(), userLogPatterns.begin(), userLogPatterns.end());
            const auto rule = detectLogPatternRule(session.document(), 100, candidates);
            if (!rule) {
                showLogFormatNotDetectedDialog(hwnd);
                return;
            }
            session.beginLogIndexing(*logIndexWorker, *rule, currentYear());
        }});
    commands.push_back(CommandDescriptor{
        .id = u"logmode.disable", .title = u"Log: Disable", .keybindingLabel = u"",
        .commandId = CommandId::None,
        // Symmetric with beginLogIndexing() above - see
        // EditorSession::disableLogMode()'s own comment.
        .action = [hwnd, &workspace, &renderPipeline]() {
            EditorSession& session = workspace.active();
            session.disableLogMode();
            pushLogVisualsForSession(renderPipeline, session);
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});

    // Per-level filter toggles + 3 presets (要件定義書§8's フィルタ, with
    // errorsOnly/warningsOnly directly satisfying ERROR抽出/WARNING抽出 -
    // see this WI's plan point 設計方針6/table for why one filter mechanism
    // covers all three requirements). Generated from a small table rather
    // than 7 hand-written blocks, same "loop over a choice array" shape
    // kPresetChoices already established in buildCommandRegistry().
    struct LogLevelChoice {
        LogLevel            level;
        std::u16string_view name;  // matches the LogLevel enumerator's own spelling - used for both id suffix and title
    };
    constexpr std::array<LogLevelChoice, 7> kLogLevelChoices{{
        {.level = LogLevel::Trace, .name = u"Trace"},
        {.level = LogLevel::Debug, .name = u"Debug"},
        {.level = LogLevel::Info, .name = u"Info"},
        {.level = LogLevel::Warning, .name = u"Warning"},
        {.level = LogLevel::Error, .name = u"Error"},
        {.level = LogLevel::Fatal, .name = u"Fatal"},
        {.level = LogLevel::Unknown, .name = u"Unknown"},
    }};
    for (const LogLevelChoice& choice : kLogLevelChoices) {
        commands.push_back(CommandDescriptor{
            .id              = std::u16string(u"logmode.filter.toggle") + std::u16string(choice.name),
            .title           = std::u16string(u"Log: Toggle ") + std::u16string(choice.name),
            .keybindingLabel = u"",
            .commandId       = CommandId::None,
            .action = [hwnd, &workspace, &renderPipeline, level = choice.level]() {
                EditorSession& session = workspace.active();
                session.logLevelFilterMask() ^= logLevelFilterBit(level);
                renderPipeline.setLogLevelFilter(session.logLevelFilterMask());
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }});
    }
    commands.push_back(CommandDescriptor{
        .id = u"logmode.filter.showAll", .title = u"Log: Show All Levels", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            EditorSession& session        = workspace.active();
            session.logLevelFilterMask() = kAllLogLevelsVisible;
            renderPipeline.setLogLevelFilter(session.logLevelFilterMask());
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"logmode.filter.errorsOnly", .title = u"Log: Show Only Errors", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            EditorSession& session = workspace.active();
            session.logLevelFilterMask() =
                static_cast<std::uint8_t>(logLevelFilterBit(LogLevel::Error) | logLevelFilterBit(LogLevel::Fatal));
            renderPipeline.setLogLevelFilter(session.logLevelFilterMask());
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"logmode.filter.warningsOnly", .title = u"Log: Show Only Warnings", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            EditorSession& session        = workspace.active();
            session.logLevelFilterMask() = logLevelFilterBit(LogLevel::Warning);
            renderPipeline.setLogLevelFilter(session.logLevelFilterMask());
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});

    // "Log: Jump to Next/Previous Log Entry" - one navigation primitive
    // that, depending on the active filter, doubles as 時系列ジャンプ (no
    // filter: walks every matched line in document order), ERROR抽出
    // navigation (errorsOnly filter active), or WARNING抽出 navigation
    // (warningsOnly filter active) - see this WI's plan point 設計方針6.
    // Same cursor-move/reveal-fold/sync shape as handleBookmarkKey()'s
    // next/previous handling in buildCommandRegistry().
    commands.push_back(CommandDescriptor{
        .id = u"logmode.jump.next", .title = u"Log: Jump to Next Log Entry", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            EditorSession& session = workspace.active();
            if (!session.logModel()) {
                return;
            }
            const Document& document    = session.document();
            const auto      currentLine = document.offsetToLine(session.selection().primaryCursor().position);
            const auto      target =
                nextVisibleLogLine(session.logModel()->lines(), currentLine, session.logLevelFilterMask());
            if (!target) {
                return;
            }
            const auto pos = document.lineToOffset(*target);
            session.selection().moveAllTo(pos);
            session.viewport().ensureVisible(pos, document);
            if (session.folding().revealLine(*target)) {
                syncFoldingState(hwnd, renderPipeline, session.folding());
            }
            syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"logmode.jump.previous", .title = u"Log: Jump to Previous Log Entry", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            EditorSession& session = workspace.active();
            if (!session.logModel()) {
                return;
            }
            const Document& document    = session.document();
            const auto      currentLine = document.offsetToLine(session.selection().primaryCursor().position);
            const auto      target =
                previousVisibleLogLine(session.logModel()->lines(), currentLine, session.logLevelFilterMask());
            if (!target) {
                return;
            }
            const auto pos = document.lineToOffset(*target);
            session.selection().moveAllTo(pos);
            session.viewport().ensureVisible(pos, document);
            if (session.folding().revealLine(*target)) {
                syncFoldingState(hwnd, renderPipeline, session.folding());
            }
            syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
        }});
}

// WI-16c: registers view.jsonTree.toggle/view.csvGrid.toggle in the palette.
// WI-15c's own plan claimed this for JsonTreeToggle but never actually added
// it - a real gap discovered while adding CsvGridToggle's own entry,
// corrected here (in the same commit as CsvGridToggle) rather than left for
// a future session; see build_plan.md's WI-16c entry. Pulled out of
// buildCommandRegistry() into its own function for the same
// cognitive-complexity reason appendLogModeCommands() above was - adding
// these 2 entries inline pushed buildCommandRegistry() to 30 (threshold 25).
// Bodies duplicate handleJsonTreeKey()/handleCsvGridKey()'s own toggle logic
// rather than calling those functions directly - they take vkCode/
// shiftDown/ctrlDown for chordMatches(), which a palette invocation has none
// of - matching the small-duplicated-toggle-body precedent this codebase
// already established across handleXxxKey()/dispatchWidgetShowCommand()'s
// own case bodies for the same commands.
void appendStructuralViewCommands(std::vector<CommandDescriptor>& commands, HWND hwnd, Workspace& workspace,
                                  RenderPipeline& renderPipeline, const core::KeyBindings& keyBindings,
                                  JsonTreePane& jsonTreePane, const neomifes::ui::OutlinePane& outlinePane,
                                  GitPane& gitPane,
                                  std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker,
                                  std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker,
                                  const void*& jsonTreePanePendingSessionToken, CsvGridPane& csvGridPane,
                                  std::optional<csvmode::CsvModelWorker>& csvModelWorker,
                                  const void*& csvGridPanePendingSessionToken) {
    commands.push_back(CommandDescriptor{
        .id = u"view.jsonTree.toggle", .title = u"Toggle Structure Tree",
        .keybindingLabel = keybindingLabelFor(keyBindings, u"view.jsonTree.toggle"),
        .commandId = CommandId::JsonTreeToggle,
        .action = [hwnd, &workspace, &jsonTreePane, &outlinePane, &gitPane, &jsonTreeWorker, &xmlTreeWorker,
                  &renderPipeline, &jsonTreePanePendingSessionToken]() {
            EditorSession& session = workspace.active();
            if (jsonTreePane.isVisible()) {
                jsonTreePane.hide();
                jsonTreePanePendingSessionToken = nullptr;
                syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
            } else {
                refreshStructureTreePane(hwnd, session, jsonTreePane, jsonTreeWorker, xmlTreeWorker, renderPipeline,
                                        jsonTreePanePendingSessionToken);
                syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
            }
        }});
    commands.push_back(CommandDescriptor{
        .id = u"view.csvGrid.toggle", .title = u"Toggle CSV Grid",
        .keybindingLabel = keybindingLabelFor(keyBindings, u"view.csvGrid.toggle"),
        .commandId = CommandId::CsvGridToggle,
        .action = [&workspace, &csvGridPane, &csvModelWorker, &csvGridPanePendingSessionToken]() {
            EditorSession& session = workspace.active();
            if (csvGridPane.isVisible()) {
                csvGridPane.hide();
                csvGridPanePendingSessionToken = nullptr;
            } else {
                refreshCsvGridPane(session, csvGridPane, csvModelWorker, csvGridPanePendingSessionToken);
            }
        }});
}

// WI-21e: "view.wordWrap.toggle"/"view.lineNumbers.toggle"/"view.theme.cycle"
// - palette counterparts to CommandId::WordWrapToggle/LineNumbersToggle/
// ThemeCycle's dispatchWidgetShowCommand() cases (menu/WM_COMMAND path).
// Pulled into its own function for the same cognitive-complexity reason
// appendStructuralViewCommands() above was (see that function's own doc
// comment) - buildCommandRegistry() is already near its threshold. Bodies
// duplicate dispatchWidgetShowCommand()'s own case bodies rather than
// calling a shared helper - same documented small-duplicated-toggle-body
// precedent (see CommandId::WordWrapToggle's own dispatchWidgetShowCommand()
// case comment).
void appendViewToggleCommands(std::vector<CommandDescriptor>& commands, HWND hwnd, Workspace& workspace,
                              RenderPipeline& renderPipeline, core::Settings& settings,
                              const std::optional<std::filesystem::path>& settingsPath) {
    commands.push_back(CommandDescriptor{
        .id = u"view.wordWrap.toggle", .title = u"View: Toggle Word Wrap", .keybindingLabel = u"",
        .commandId = CommandId::WordWrapToggle,
        .action = [hwnd, &workspace, &renderPipeline, &settings, settingsPath]() {
            settings.wordWrap = !settings.wordWrap;
            renderPipeline.setWordWrap(settings.wordWrap);
            workspace.active().viewport().setWordWrapEnabled(settings.wordWrap);
            if (settingsPath) {
                settings.saveTo(*settingsPath);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"view.lineNumbers.toggle", .title = u"View: Toggle Line Numbers", .keybindingLabel = u"",
        .commandId = CommandId::LineNumbersToggle,
        .action = [hwnd, &renderPipeline, &settings, settingsPath]() {
            settings.showLineNumbers = !settings.showLineNumbers;
            renderPipeline.setLineNumbersVisible(settings.showLineNumbers);
            if (settingsPath) {
                settings.saveTo(*settingsPath);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"view.theme.cycle", .title = u"View: Cycle Theme", .keybindingLabel = u"",
        .commandId = CommandId::ThemeCycle,
        .action = [hwnd, &renderPipeline, &settings, settingsPath]() {
            const ThemeKind current = neomifes::app::parseThemeKind(settings.themeName);
            const ThemeKind next    = neomifes::app::nextThemeKind(current);
            settings.themeName = std::u16string(neomifes::app::themeKindToSettingsString(next));
            renderPipeline.setTheme(next);
            if (settingsPath) {
                settings.saveTo(*settingsPath);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
}

std::vector<CommandDescriptor> buildCommandRegistry(
    HWND hwnd, FindDialog& findDialog, FindReplaceDialog& findReplaceDialog, Workspace& workspace,
    RenderPipeline& renderPipeline, core::Settings& settings,
    const std::optional<std::filesystem::path>& settingsPath, core::KeyBindings& keyBindings,
    const std::optional<std::filesystem::path>& keyBindingsPath, platform::AcceleratorTableHandle& accelTable,
    bool& freeCursorModeEnabled, CommandPalette& commandPalette, core::RecentFiles& recentFiles,
    const MenuBarHandles& menuHandles, AutosaveContext& autosave,
    std::optional<LogIndexWorker>& logIndexWorker, std::vector<LogPatternRule>& userLogPatterns,
    const std::optional<std::filesystem::path>& logPatternsDir, JsonTreePane& jsonTreePane,
    const neomifes::ui::OutlinePane& outlinePane,
    std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker, std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker,
    const void*& jsonTreePanePendingSessionToken, CsvGridPane& csvGridPane,
    std::optional<csvmode::CsvModelWorker>& csvModelWorker,
    const void*& csvGridPanePendingSessionToken, JsonPathBar& jsonPathBar, bool& jsonPathBarIsForXml,
    git::GitDiffWorker& gitDiffWorker, GitPane& gitPane, git::GitStatusWorker& gitStatusWorker,
    std::optional<document::Document>& diffViewDocument, SessionManager& sessionManager) {
    std::vector<CommandDescriptor> commands;
    commands.push_back(CommandDescriptor{
        .id = u"find.show", .title = u"Find",
        .keybindingLabel = keybindingLabelFor(keyBindings, u"find.show"),
        .commandId       = CommandId::FindShow,
        .action          = [hwnd, &findDialog]() { findDialog.show(hwnd); }});
    commands.push_back(CommandDescriptor{
        .id = u"find.replace", .title = u"Find and Replace",
        .keybindingLabel = keybindingLabelFor(keyBindings, u"find.replace"),
        .commandId       = CommandId::FindReplace,
        // WI-18b: previously findBar.showWithReplace() - see
        // dispatchWidgetShowCommand()'s identical CommandId::FindReplace
        // case for the full rationale.
        .action          = [hwnd, &findReplaceDialog]() { findReplaceDialog.show(hwnd); }});
    commands.push_back(CommandDescriptor{
        .id = u"find.next", .title = u"Find Next",
        .keybindingLabel = keybindingLabelFor(keyBindings, u"find.next"),
        .commandId = CommandId::FindNext,
        .action = [hwnd, &workspace, &renderPipeline, &findDialog]() {
            navigateToMatch(true, hwnd, workspace.active(), renderPipeline, findDialog);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"find.previous", .title = u"Find Previous",
        .keybindingLabel = keybindingLabelFor(keyBindings, u"find.previous"),
        .commandId = CommandId::FindPrevious,
        .action = [hwnd, &workspace, &renderPipeline, &findDialog]() {
            navigateToMatch(false, hwnd, workspace.active(), renderPipeline, findDialog);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"window.new", .title = u"New Window",
        .keybindingLabel = keybindingLabelFor(keyBindings, u"window.new"),
        .commandId       = CommandId::NewWindow,
        .action          = [&sessionManager]() { static_cast<void>(sessionManager.createWindow(std::nullopt)); }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.undo", .title = u"Undo",
        .keybindingLabel = keybindingLabelFor(keyBindings, u"edit.undo"),
        .commandId = CommandId::Undo,
        // WI-07 step2: routes through dispatchCommand() (command_dispatch.h)
        // instead of duplicating the undo() + syncRenderStateAndInvalidate()
        // body inline - the same case now also backs Ctrl+Z's own explicit
        // handleKeyDownEvent() check (Undo isn't accelerator-routed, see
        // command_dispatch.h's top comment).
        .action = [hwnd, &workspace, &renderPipeline, &findDialog, &recentFiles, menuHandles, &autosave, &settings,
                  &csvGridPane, &csvGridPanePendingSessionToken, &gitDiffWorker, &sessionManager]() {
            const CommandDispatchContext ctx{.hwnd                           = hwnd,
                                             .workspace                      = workspace,
                                             .renderPipeline                 = renderPipeline,
                                             .findDialog                        = findDialog,
                                             .recentFiles                    = recentFiles,
                                             .menuHandles                    = menuHandles,
                                             .autosave                       = autosave,
                                             .settings                       = settings,
                                             .csvGridPane                    = csvGridPane,
                                             .csvGridPanePendingSessionToken = csvGridPanePendingSessionToken,
                                             .gitDiffWorker                  = gitDiffWorker,
                                             .sessionManager                 = sessionManager};
            dispatchCommand(CommandId::Undo, ctx);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.redo", .title = u"Redo",
        .keybindingLabel = keybindingLabelFor(keyBindings, u"edit.redo"),
        .commandId = CommandId::Redo,
        .action = [hwnd, &workspace, &renderPipeline, &findDialog, &recentFiles, menuHandles, &autosave, &settings,
                  &csvGridPane, &csvGridPanePendingSessionToken, &gitDiffWorker, &sessionManager]() {
            const CommandDispatchContext ctx{.hwnd                           = hwnd,
                                             .workspace                      = workspace,
                                             .renderPipeline                 = renderPipeline,
                                             .findDialog                        = findDialog,
                                             .recentFiles                    = recentFiles,
                                             .menuHandles                    = menuHandles,
                                             .autosave                       = autosave,
                                             .settings                       = settings,
                                             .csvGridPane                    = csvGridPane,
                                             .csvGridPanePendingSessionToken = csvGridPanePendingSessionToken,
                                             .gitDiffWorker                  = gitDiffWorker,
                                             .sessionManager                 = sessionManager};
            dispatchCommand(CommandId::Redo, ctx);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.convertTabsToSpaces", .title = u"Convert Tabs to Spaces", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &settings]() {
            EditorSession& session = workspace.active();
            if (neomifes::app::applyIndentationConversion(IndentationConversionTarget::TabsToSpaces,
                                                          session.document(), session.dispatcher(),
                                                          session.selection(), settings.tabWidth)) {
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.convertSpacesToTabs", .title = u"Convert Spaces to Tabs", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &settings]() {
            EditorSession& session = workspace.active();
            if (neomifes::app::applyIndentationConversion(IndentationConversionTarget::SpacesToTabs,
                                                          session.document(), session.dispatcher(),
                                                          session.selection(), settings.tabWidth)) {
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
        }});
    // WI-12: Ctrl+A/Ctrl+D/Alt+Up/Alt+Down/Ctrl+Shift+K - CommandId::None
    // (same "palette-only, no keybindingLabelFor() lookup" pattern as
    // edit.convertTabsToSpaces/convertSpacesToTabs above), deliberately
    // NOT added to ui::kAllRemappableCommandIds/core::KeyBindings (see
    // handleLineEditKey()'s own comment for why) - the labels below are
    // therefore hardcoded literals describing the fixed shortcut, not a
    // keyBindings lookup.
    commands.push_back(CommandDescriptor{
        .id = u"edit.selectAll", .title = u"Select All", .keybindingLabel = u"Ctrl+A",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            dispatchSelectAllCommand(hwnd, renderPipeline, workspace.active());
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.duplicateLine", .title = u"Duplicate Line", .keybindingLabel = u"Ctrl+D",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            dispatchDuplicateLineCommand(hwnd, renderPipeline, workspace.active());
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.moveLineUp", .title = u"Move Line Up", .keybindingLabel = u"Alt+Up",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            dispatchMoveLineCommand(false, hwnd, renderPipeline, workspace.active());
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.moveLineDown", .title = u"Move Line Down", .keybindingLabel = u"Alt+Down",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            dispatchMoveLineCommand(true, hwnd, renderPipeline, workspace.active());
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.deleteLine", .title = u"Delete Line", .keybindingLabel = u"Ctrl+Shift+K",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            dispatchDeleteLineCommand(hwnd, renderPipeline, workspace.active());
        }});
    // WI-15d: "JSON整形"/"JSON検証" - palette-only, same CommandId::None
    // shape as edit.selectAll/edit.duplicateLine/etc. above (see
    // dispatchJsonFormatCommand()/dispatchJsonValidateCommand()'s own
    // comments for why - no CommandId/keybinding/menu entry).
    commands.push_back(CommandDescriptor{
        .id = u"json.format", .title = u"JSON: Format Document", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            dispatchJsonFormatCommand(hwnd, renderPipeline, workspace.active());
        }});
    commands.push_back(CommandDescriptor{
        .id = u"json.validate", .title = u"JSON: Validate", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &workspace, &renderPipeline]() {
            dispatchJsonValidateCommand(hwnd, renderPipeline, workspace.active());
        }});
    // WI-15e: "JSONPathを評価" - palette-only, same CommandId::None shape as
    // json.format/json.validate above, EXCEPT this action only opens
    // jsonPathBar rather than doing the work inline - json.jsonpath needs a
    // user-supplied expression string first (dispatchJsonPathCommand() is
    // called later, from JsonPathBar's own onSubmit - see
    // buildJsonPathBarConfig()). WI-15i: sets jsonPathBarIsForXml = false
    // before showing - see buildJsonPathBarConfig()'s own comment on why.
    commands.push_back(CommandDescriptor{
        .id = u"json.jsonpath", .title = u"JSON: Evaluate JSONPath", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [&jsonPathBar, &jsonPathBarIsForXml]() {
            jsonPathBarIsForXml = false;
            jsonPathBar.show();
        }});
    // WI-15i: "XPathを評価" - the XPath counterpart of json.jsonpath above,
    // reusing the SAME jsonPathBar instance (see buildJsonPathBarConfig()'s
    // own comment) rather than a new ui::XPathBar - deliberately a SEPARATE
    // command/title from json.jsonpath, not folded into one auto-detecting
    // entry the way Ctrl+Shift+J's structure-tree toggle was (WI-15h) - the
    // query SYNTAX itself ($.key vs /tag[1]) is far more visible to the user
    // typing into this bar than a panel toggle ever was, so two clearly-
    // labeled palette entries were chosen as more discoverable than one
    // ambiguous one (confirmed with the user before implementing).
    commands.push_back(CommandDescriptor{
        .id = u"xml.xpath", .title = u"XML: Evaluate XPath", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [&jsonPathBar, &jsonPathBarIsForXml]() {
            jsonPathBarIsForXml = true;
            jsonPathBar.show();
        }});
    // WI-17c: "Git: Refresh Diff Markers" - palette-only, same CommandId::None
    // shape as json.format/json.validate above. The only call site of
    // EditorSession::beginGitDiffIndexing() in this codebase (no automatic
    // trigger yet, see dispatchGitRefreshDiffCommand()'s own comment).
    commands.push_back(CommandDescriptor{
        .id = u"git.refreshDiff", .title = u"Git: Refresh Diff Markers", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [&workspace, &gitDiffWorker]() {
            dispatchGitRefreshDiffCommand(workspace.active(), gitDiffWorker);
        }});
    // WI-17e: "Git: Toggle Changed Files" - palette-only, same CommandId::None
    // shape as git.refreshDiff above (see this WI's own plan for why: the
    // roadmap-proposed Ctrl+Shift+G already belongs to CsvGridToggle, and the
    // user chose "command palette only" over reassigning either keybinding).
    commands.push_back(CommandDescriptor{
        .id = u"git.togglePane", .title = u"Git: Toggle Changed Files", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &renderPipeline, &workspace, &outlinePane, &jsonTreePane, &gitPane, &gitStatusWorker]() {
            toggleGitPane(hwnd, renderPipeline, workspace, outlinePane, jsonTreePane, gitPane, gitStatusWorker);
        }});
    // WI-17f: "Git: Toggle Diff View" - palette-only, same CommandId::None
    // shape as git.togglePane above (roadmap's suggested Alt+D deliberately
    // not wired).
    commands.push_back(CommandDescriptor{
        .id = u"git.toggleDiffView", .title = u"Git: Toggle Diff View", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &renderPipeline, &workspace, &findDialog, &csvGridPane, &csvGridPanePendingSessionToken,
                  &diffViewDocument]() {
            toggleDiffView(hwnd, renderPipeline, workspace, findDialog, csvGridPane, csvGridPanePendingSessionToken,
                           diffViewDocument);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"settings.reload", .title = u"Reload Settings", .keybindingLabel = u"",
        .commandId = CommandId::None,
        // WI-08: re-reads settings.json (missing/corrupt -> safe defaults,
        // same Settings::loadFrom() contract core_settings_test.cpp
        // verifies) and re-applies the 6 live-wired setters (WI-09 added
        // setTheme(), WI-21e added setWordWrap()). This is the only
        // in-app mutation path for `settings`
        // in WI-08 - there is no settings dialog yet (see build_plan.md's
        // WI-08 section for why: out of scope for this WI), so users
        // hand-edit settings.json externally and run this command
        // (Ctrl+Shift+P) to pick it up without restarting. No-op if
        // settingsPath is nullopt (resolveAppDataDir() failed at startup -
        // same graceful degradation as searchHistoryPath's own
        // reload/save paths).
        .action = [hwnd, &renderPipeline, &settings, settingsPath]() {
            if (!settingsPath) {
                return;
            }
            settings = core::Settings::loadFrom(*settingsPath);
            renderPipeline.setFontSettings(settings.fontFamily, settings.fontSizeDips);
            renderPipeline.setTabWidth(settings.tabWidth);
            renderPipeline.setLineNumbersVisible(settings.showLineNumbers);
            renderPipeline.setMinimapVisible(settings.showMinimap);
            renderPipeline.setTheme(neomifes::app::parseThemeKind(settings.themeName));
            // WI-21e: 6th live-wired setter - the active session's Viewport
            // itself picks this up via handlePaintEvent()'s per-frame sync
            // (see that call site's own comment), so nothing further is
            // needed here.
            renderPipeline.setWordWrap(settings.wordWrap);
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
    // WI-09: 3 flat palette-only commands rather than a single
    // showChoiceMenu<T>()-based picker (the pattern the status bar's
    // encoding/line-ending pickers use, handleStatusBarPartClicked() above)
    // - that picker needs a screen POINT to anchor TrackPopupMenu() at,
    // which only exists for a click-driven invocation; a command palette
    // entry has no click position, so reusing it here would need new
    // GetCursorPos()-style fallback machinery for no real benefit over 3
    // small, independently-labeled commands (build_plan.md §2.3's "when in
    // doubt, build small" default). Each command mutates settings.themeName
    // via themeKindToSettingsString() (never the raw string literal
    // directly) so the persisted string and the actually-applied theme can
    // never drift apart, then persists immediately (unlike settings.reload
    // above, which only ever reads) so the choice survives a restart
    // without a separate "save settings" step.
    commands.push_back(CommandDescriptor{
        .id = u"view.theme.dark", .title = u"Theme: Dark", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &renderPipeline, &settings, settingsPath]() {
            constexpr auto kKind = ThemeKind::Dark;
            settings.themeName = std::u16string(neomifes::app::themeKindToSettingsString(kKind));
            renderPipeline.setTheme(kKind);
            if (settingsPath) {
                settings.saveTo(*settingsPath);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"view.theme.light", .title = u"Theme: Light", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &renderPipeline, &settings, settingsPath]() {
            constexpr auto kKind = ThemeKind::Light;
            settings.themeName = std::u16string(neomifes::app::themeKindToSettingsString(kKind));
            renderPipeline.setTheme(kKind);
            if (settingsPath) {
                settings.saveTo(*settingsPath);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"view.theme.highContrast", .title = u"Theme: High Contrast", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &renderPipeline, &settings, settingsPath]() {
            constexpr auto kKind = ThemeKind::HighContrast;
            settings.themeName = std::u16string(neomifes::app::themeKindToSettingsString(kKind));
            renderPipeline.setTheme(kKind);
            if (settingsPath) {
                settings.saveTo(*settingsPath);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"view.toggleFoldAtCursor", .title = u"Fold/Unfold at Cursor", .keybindingLabel = u"",
        .commandId = CommandId::None,
        // Phase 7i: v1 requires the primary cursor to sit exactly on a fold
        // header line - no-op otherwise (see the Phase 7i plan's Context
        // point 6 for why gutter-click toggling is deferred to a later
        // sub-phase; this command is the only way to toggle a fold for now).
        .action = [hwnd, &workspace, &renderPipeline]() {
            EditorSession& session = workspace.active();
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
        .commandId = CommandId::None,
        .action = [hwnd, &renderPipeline, &workspace, &freeCursorModeEnabled]() {
            freeCursorModeEnabled = !freeCursorModeEnabled;
            EditorSession& session = workspace.active();
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
    // WI-10: mirrors settings.reload's "re-read the file, re-apply live"
    // shape - but ALSO rebuilds accelTable (HACCEL) and this very command
    // registry (via commandPalette.setCommands()), since a keybindings
    // change affects both the manual-chain dispatch (automatic, see
    // wireNormalMode()'s header comment - no rebuild needed there) and the
    // HACCEL table + every keybindingLabel shown here (both DO need an
    // explicit rebuild). No-op if keyBindingsPath is nullopt (same
    // graceful-degradation treatment as settings.reload/searchHistoryPath).
    commands.push_back(CommandDescriptor{
        .id = u"keybindings.reload", .title = u"Reload Keybindings", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &findDialog, &findReplaceDialog, &workspace, &renderPipeline, &settings, settingsPath,
                   &keyBindings,
                   keyBindingsPath, &accelTable, &freeCursorModeEnabled, &commandPalette, &recentFiles,
                   menuHandles, &autosave, &logIndexWorker, &userLogPatterns, logPatternsDir, &jsonTreePane,
                   &outlinePane, &jsonTreeWorker, &xmlTreeWorker, &jsonTreePanePendingSessionToken, &csvGridPane,
                   &csvModelWorker, &csvGridPanePendingSessionToken, &jsonPathBar, &jsonPathBarIsForXml,
                   &gitDiffWorker, &gitPane, &gitStatusWorker, &diffViewDocument, &sessionManager]() {
            if (!keyBindingsPath) {
                return;
            }
            keyBindings = core::KeyBindings::loadFrom(*keyBindingsPath);
            accelTable  = neomifes::app::buildAcceleratorTable(keyBindings);
            commandPalette.setCommands(buildCommandRegistry(
                hwnd, findDialog, findReplaceDialog, workspace, renderPipeline, settings, settingsPath, keyBindings,
                keyBindingsPath,
                accelTable, freeCursorModeEnabled, commandPalette, recentFiles, menuHandles, autosave,
                logIndexWorker, userLogPatterns, logPatternsDir, jsonTreePane, outlinePane, jsonTreeWorker,
                xmlTreeWorker, jsonTreePanePendingSessionToken, csvGridPane, csvModelWorker,
                csvGridPanePendingSessionToken, jsonPathBar, jsonPathBarIsForXml, gitDiffWorker, gitPane,
                gitStatusWorker, diffViewDocument, sessionManager));
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});
    // WI-10: 4 flat palette-only commands, same "no click position to
    // anchor a picker at" reasoning view.theme.* (above) already
    // established for this exact situation. Each replaces the ENTIRE live
    // keyBindings with KeyBindings::forPreset(name) (a full swap, not a
    // merge - any hand customization for a command the target preset
    // doesn't cover is discarded, matching WI-09's theme-switch precedent),
    // persists immediately (so the choice survives a restart without a
    // separate save step), then rebuilds accelTable + this registry, same
    // as keybindings.reload above.
    struct PresetChoice {
        std::u16string_view name;         // KeyBindings::forPreset()'s key, and the ".id" suffix
        std::u16string_view displayName;  // palette-facing title only
    };
    constexpr std::array<PresetChoice, 4> kPresetChoices{{
        {.name = u"neomifes", .displayName = u"NeoMIFES"},
        {.name = u"hidemaru", .displayName = u"秀丸 (Hidemaru)"},
        {.name = u"sakura", .displayName = u"サクラ (Sakura)"},
        {.name = u"vscode", .displayName = u"VSCode"},
    }};
    for (const PresetChoice& choice : kPresetChoices) {
        commands.push_back(CommandDescriptor{
            .id              = std::u16string(u"keybindings.preset.") + std::u16string(choice.name),
            .title           = std::u16string(u"Keybindings Preset: ") + std::u16string(choice.displayName),
            .keybindingLabel = u"",
            .commandId       = CommandId::None,
            .action = [hwnd, &findDialog, &findReplaceDialog, &workspace, &renderPipeline, &settings, settingsPath,
                       &keyBindings,
                       keyBindingsPath, &accelTable, &freeCursorModeEnabled, &commandPalette, &recentFiles,
                       menuHandles, &autosave, &logIndexWorker, &userLogPatterns, logPatternsDir, &jsonTreePane,
                       &outlinePane, &jsonTreeWorker, &xmlTreeWorker, &jsonTreePanePendingSessionToken,
                       &csvGridPane, &csvModelWorker, &csvGridPanePendingSessionToken, &jsonPathBar,
                       &jsonPathBarIsForXml, &gitDiffWorker, &gitPane, &gitStatusWorker, &diffViewDocument,
                       &sessionManager, presetName = std::u16string(choice.name)]() {
                keyBindings = core::KeyBindings::forPreset(presetName);
                if (keyBindingsPath) {
                    keyBindings.saveTo(*keyBindingsPath);
                }
                accelTable = neomifes::app::buildAcceleratorTable(keyBindings);
                commandPalette.setCommands(buildCommandRegistry(
                    hwnd, findDialog, findReplaceDialog, workspace, renderPipeline, settings, settingsPath, keyBindings,
                    keyBindingsPath, accelTable, freeCursorModeEnabled, commandPalette, recentFiles,
                    menuHandles, autosave, logIndexWorker, userLogPatterns, logPatternsDir, jsonTreePane,
                    outlinePane, jsonTreeWorker, xmlTreeWorker, jsonTreePanePendingSessionToken, csvGridPane,
                    csvModelWorker, csvGridPanePendingSessionToken, jsonPathBar, jsonPathBarIsForXml,
                    gitDiffWorker, gitPane, gitStatusWorker, diffViewDocument, sessionManager));
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }});
    }

    // WI-14d: mirrors keybindings.reload's shape above - re-scans
    // logPatternsDir (%APPDATA%\NeoMIFES\log_patterns\) and rebuilds this
    // very registry so the palette's "Log: Enable (...)" entries (built by
    // appendLogModeCommands() below) pick up any pattern file the user
    // added/edited/removed since launch, without a restart. Lives here
    // (not inside appendLogModeCommands()) for the same reason
    // keybindings.reload/keybindings.preset.* live in buildCommandRegistry()
    // itself - only this function has commandPalette + every other
    // parameter buildCommandRegistry() needs to call itself again.
    // No-op if logPatternsDir is nullopt (same graceful-degradation
    // treatment as keybindings.reload's keyBindingsPath check above).
    commands.push_back(CommandDescriptor{
        .id = u"logmode.patterns.reload", .title = u"Log: Reload Patterns", .keybindingLabel = u"",
        .commandId = CommandId::None,
        .action = [hwnd, &findDialog, &findReplaceDialog, &workspace, &renderPipeline, &settings, settingsPath,
                   &keyBindings,
                   keyBindingsPath, &accelTable, &freeCursorModeEnabled, &commandPalette, &recentFiles,
                   menuHandles, &autosave, &logIndexWorker, &userLogPatterns, logPatternsDir, &jsonTreePane,
                   &outlinePane, &jsonTreeWorker, &xmlTreeWorker, &jsonTreePanePendingSessionToken, &csvGridPane,
                   &csvModelWorker, &csvGridPanePendingSessionToken, &jsonPathBar, &jsonPathBarIsForXml,
                   &gitDiffWorker, &gitPane, &gitStatusWorker, &diffViewDocument, &sessionManager]() {
            if (!logPatternsDir) {
                return;
            }
            userLogPatterns = loadUserLogPatternsFromDirectory(*logPatternsDir);
            commandPalette.setCommands(buildCommandRegistry(
                hwnd, findDialog, findReplaceDialog, workspace, renderPipeline, settings, settingsPath, keyBindings,
                keyBindingsPath,
                accelTable, freeCursorModeEnabled, commandPalette, recentFiles, menuHandles, autosave,
                logIndexWorker, userLogPatterns, logPatternsDir, jsonTreePane, outlinePane, jsonTreeWorker,
                xmlTreeWorker, jsonTreePanePendingSessionToken, csvGridPane, csvModelWorker,
                csvGridPanePendingSessionToken, jsonPathBar, jsonPathBarIsForXml, gitDiffWorker, gitPane,
                gitStatusWorker, diffViewDocument, sessionManager));
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }});

    // WI-16c: "view.jsonTree.toggle"/"view.csvGrid.toggle" - see
    // appendStructuralViewCommands()'s own doc comment for why this block
    // lives in its own function rather than inline here.
    appendStructuralViewCommands(commands, hwnd, workspace, renderPipeline, keyBindings, jsonTreePane, outlinePane,
                                 gitPane, jsonTreeWorker, xmlTreeWorker, jsonTreePanePendingSessionToken,
                                 csvGridPane, csvModelWorker, csvGridPanePendingSessionToken);

    // WI-21e: "view.wordWrap.toggle"/"view.lineNumbers.toggle"/
    // "view.theme.cycle" - see appendViewToggleCommands()'s own doc comment
    // for why this block lives in its own function rather than inline here.
    appendViewToggleCommands(commands, hwnd, workspace, renderPipeline, settings, settingsPath);

    // WI-14c: "Log: Enable/Disable/Toggle*/Show*/Jump*" - see
    // appendLogModeCommands()'s own doc comment for why this block lives in
    // its own function rather than inline here.
    appendLogModeCommands(commands, hwnd, workspace, renderPipeline, logIndexWorker, userLogPatterns);

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
// rationale as buildFindDialogConfig()/buildCommandRegistry() above. WI-04:
// takes EditorSession& (document/selection/viewport/folding - 4 members).
// WI-05 step 1: takes Workspace& instead - config.onSubmit below is STORED
// inside GotoLineBar and invoked later, so it resolves workspace.active()
// fresh at invocation time (see wireNormalMode()'s header comment for why).
GotoLineBarConfig buildGotoLineBarConfig(HWND hwnd, Workspace& workspace, RenderPipeline& renderPipeline,
                                         GotoLineBar& gotoLineBar) {
    GotoLineBarConfig config{};
    config.onSubmit = [hwnd, &workspace, &renderPipeline, &gotoLineBar](std::u16string_view input) {
        const auto target = neomifes::ui::parseGotoLineInput(input);
        if (target) {
            jumpToGotoTarget(*target, hwnd, workspace.active(), renderPipeline);
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

// Builds the JsonPathBarConfig callbacks (WI-15e) - same extraction
// rationale/shape as buildGotoLineBarConfig() above, including resolving
// workspace.active() fresh at invocation time.
//
// WI-15i: `jsonPathBarIsForXml` is how a SINGLE shared JsonPathBar instance
// (see this file's own xml.xpath command comment on why no new ui::XPathBar
// was introduced - JsonPathBarConfig::onSubmit already only hands back raw
// text, JSON-agnostic) serves two completely different query languages
// ($.key vs /tag[1]). Set to true/false by whichever command
// (json.jsonpath/xml.xpath) just called jsonPathBar.show() - a
// wWinMain-local bool, same "UI-layer state that isn't any one
// EditorSession's concern" placement as freeCursorModeEnabled/
// isDraggingMinimap. Read here, once, at the moment the user actually
// submits - not at show()-time - so it always reflects whichever command
// most recently opened the bar, even across an close-without-submit +
// reopen-via-the-other-command sequence.
JsonPathBarConfig buildJsonPathBarConfig(HWND hwnd, Workspace& workspace, RenderPipeline& renderPipeline,
                                         JsonPathBar& jsonPathBar, const bool& jsonPathBarIsForXml) {
    JsonPathBarConfig config{};
    config.onSubmit = [hwnd, &workspace, &renderPipeline, &jsonPathBar,
                       &jsonPathBarIsForXml](std::u16string_view input) {
        if (jsonPathBarIsForXml) {
            dispatchXPathCommand(input, hwnd, renderPipeline, workspace.active());
        } else {
            dispatchJsonPathCommand(input, hwnd, renderPipeline, workspace.active());
        }
        jsonPathBar.hide();
        ::SetFocus(hwnd);
    };
    config.onClosed = [hwnd, &jsonPathBar]() {
        jsonPathBar.hide();
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
// cached match/bookmark visuals and FindDialog's match count are separate from
// what EditorSession::openFile() itself already resets internally (undo
// history, BookmarkManager, both selection anchors, free-cursor virtual
// columns). Mirrors replaceAllMatches()'s reset sequence above. WI-04:
// takes EditorSession& (essentially every member); grepState stays separate
// (document-independent, see runGrepQuery()'s comment).
void jumpToGrepResult(std::size_t resultIndex, HWND hwnd, const GrepState& grepState, EditorSession& session,
                      RenderPipeline& renderPipeline, FindDialog& findDialog, core::RecentFiles& recentFiles,
                      const MenuBarHandles& menuHandles, CsvGridPane& csvGridPane,
                      const void*& csvGridPanePendingSessionToken) {
    if (resultIndex >= grepState.currentResults.size()) {
        return;
    }
    const GrepMatch& match = grepState.currentResults[resultIndex];
    // Stale result (file moved/deleted since the Grep ran) - openFileAndSyncView()
    // leaves everything untouched on failure, same silent no-op contract as
    // before this WI. No error-toast UI exists yet to surface this.
    (void)openFileAndSyncView(match.path, match.line, match.columnRange.start, hwnd, session,
                              renderPipeline, findDialog, recentFiles, menuHandles, csvGridPane,
                              csvGridPanePendingSessionToken);
}

// Builds the GrepBarConfig callbacks (Phase 5c3) - same extraction rationale
// as buildFindDialogConfig()/buildGotoLineBarConfig() above. WI-04: takes
// EditorSession& for the document-scoped state jumpToGrepResult() needs;
// grepState/searchHistory stay separate (Workspace-wide, not
// document-scoped). WI-05 step 1: takes Workspace& instead -
// config.onResultActivated below is STORED inside GrepBar and invoked
// later, so it resolves workspace.active() fresh at invocation time (see
// wireNormalMode()'s header comment for why).
GrepBarConfig buildGrepBarConfig(HWND hwnd, Workspace& workspace, RenderPipeline& renderPipeline,
                                 FindDialog& findDialog, GrepBar& grepBar, GrepState& grepState,
                                 SearchHistory& searchHistory, core::RecentFiles& recentFiles,
                                 const MenuBarHandles& menuHandles, CsvGridPane& csvGridPane,
                                 const void*& csvGridPanePendingSessionToken) {
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
    config.onResultActivated = [hwnd, &grepState, &workspace, &renderPipeline, &findDialog, &recentFiles, menuHandles,
                                &csvGridPane, &csvGridPanePendingSessionToken](std::size_t resultIndex) {
        jumpToGrepResult(resultIndex, hwnd, grepState, workspace.active(), renderPipeline, findDialog, recentFiles,
                         menuHandles, csvGridPane, csvGridPanePendingSessionToken);
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
// (document/selection/viewport - 3 members). WI-05 step 1: takes
// Workspace& instead - config.onItemSelected below is STORED inside
// OutlinePane and invoked later, so it resolves workspace.active() fresh at
// invocation time (see wireNormalMode()'s header comment for why); the rest
// of this function's own body never touches a session at all.
void createAndPositionOutlinePane(HWND hwnd, HINSTANCE hInstance, Workspace& workspace,
                                  RenderPipeline& renderPipeline, OutlinePane& outlinePane) {
    OutlinePaneConfig config{};
    config.onItemSelected = [hwnd, &workspace, &renderPipeline](std::uint64_t targetPos) {
        jumpToOutlinePosition(targetPos, hwnd, workspace.active(), renderPipeline);
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

// Same "create then prime the first position/size explicitly" shape as
// createAndPositionOutlinePane() above (WI-15c) - see that function's own
// comment for why the explicit onParentResized() call below is required.
// config.onItemSelected reuses jumpToOutlinePosition() as-is: JsonTreePane
// echoes back a targetPos into the SAME open document exactly the way
// OutlinePane does, so no JSON-specific jump logic is needed.
// config.onClosed clears jsonTreePanePendingSessionToken (Escape is one of
// the two paths that must clear it - see handleJsonTreeKey()'s own comment
// on the other, the hide-toggle branch).
void createAndPositionJsonTreePane(HWND hwnd, HINSTANCE hInstance, Workspace& workspace,
                                   RenderPipeline& renderPipeline, JsonTreePane& jsonTreePane,
                                   const void*& jsonTreePanePendingSessionToken) {
    JsonTreePaneConfig config{};
    config.onItemSelected = [hwnd, &workspace, &renderPipeline](std::uint64_t targetPos) {
        jumpToOutlinePosition(targetPos, hwnd, workspace.active(), renderPipeline);
    };
    config.onClosed = [hwnd, &jsonTreePanePendingSessionToken]() {
        jsonTreePanePendingSessionToken = nullptr;
        ::SetFocus(hwnd);
    };
    if (!jsonTreePane.create(hwnd, hInstance, config)) {
        return;
    }
    RECT clientRect{};
    ::GetClientRect(hwnd, &clientRect);
    const auto dpiScale = static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0F;
    jsonTreePane.onParentResized(static_cast<std::uint32_t>(clientRect.right),
                                 static_cast<std::uint32_t>(clientRect.bottom), dpiScale);
}

// WI-17e: GitPaneConfig::onFileActivated - opens the clicked changed file.
// Same open+error-check+sync shape as dispatchRecentFileCommand() below,
// minus the RecentFiles/menu bookkeeping - this is not a "recently opened
// file" feature, so re-recording/refreshing that menu would be a surprising
// side effect of clicking a row in an unrelated panel.
void dispatchGitPaneFileActivated(HWND hwnd, Workspace& workspace, RenderPipeline& renderPipeline, FindDialog& findDialog,
                                  CsvGridPane& csvGridPane, const void*& csvGridPanePendingSessionToken,
                                  const std::filesystem::path& path) {
    const auto result = workspace.openFile(path);
    if (const auto* error = std::get_if<LoadError>(&result)) {
        neomifes::app::showOpenErrorDialog(hwnd, *error);
        return;
    }
    syncViewForActiveSession(hwnd, renderPipeline, workspace.active(), findDialog, csvGridPane,
                             csvGridPanePendingSessionToken);
}

// Same "create then prime the first position/size explicitly" shape as
// createAndPositionOutlinePane()/createAndPositionJsonTreePane() above
// (WI-17e). config.onClosed has no pending-token to clear (unlike
// createAndPositionJsonTreePane()'s own) - see git_pane.h's own class
// comment on why GitPane has no such token.
void createAndPositionGitPane(HWND hwnd, HINSTANCE hInstance, Workspace& workspace, RenderPipeline& renderPipeline,
                              FindDialog& findDialog, CsvGridPane& csvGridPane,
                              const void*& csvGridPanePendingSessionToken, GitPane& gitPane) {
    GitPaneConfig config{};
    config.onFileActivated = [hwnd, &workspace, &renderPipeline, &findDialog, &csvGridPane,
                              &csvGridPanePendingSessionToken](const std::filesystem::path& absolutePath) {
        dispatchGitPaneFileActivated(hwnd, workspace, renderPipeline, findDialog, csvGridPane,
                                     csvGridPanePendingSessionToken, absolutePath);
    };
    config.onClosed = [hwnd]() { ::SetFocus(hwnd); };
    if (!gitPane.create(hwnd, hInstance, config)) {
        return;
    }
    RECT clientRect{};
    ::GetClientRect(hwnd, &clientRect);
    const auto dpiScale = static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0F;
    gitPane.onParentResized(static_cast<std::uint32_t>(clientRect.right),
                           static_cast<std::uint32_t>(clientRect.bottom), dpiScale);
}

// WI-16f: CsvGridPaneConfig::onCellEditCommitted. `rowIndex` is a DISPLAY
// row index (into session.csvRowOrder()), same translation
// onGetCellText/onCellActivated below already perform. Trusts CsvGridPane's
// own guarantee that this only fires when `newText` actually differs from
// the cell's original value (see CsvGridPaneConfig::onCellEditCommitted's
// own comment) - no redundant no-op check here. Escapes `newText` via
// csvmode::escapeCsvCellText() (WI-16f) into raw CSV text, dispatches it as
// one core::ReplaceRangeCommand (same session.dispatcher().dispatch()
// pattern dispatchJsonFormatCommand() above already uses for a document
// rewrite), then re-detects the delimiter and kicks off a full re-index
// (EditorSession::beginCsvIndexing()) - same detectCsvDelimiter() call
// refreshCsvGridPane() below already makes, since CsvModel itself does not
// remember which delimiter it was parsed with. The already-open grid's own
// visual refresh happens later, once that re-index completes, via
// applyCsvIndexReadyMessage()'s now-broadened condition (see that
// function's own comment) - not here.
void applyCsvCellEdit(HWND hwnd, RenderPipeline& renderPipeline, Workspace& workspace,
                      std::optional<csvmode::CsvModelWorker>& csvModelWorker, std::size_t rowIndex,
                      std::size_t colIndex, std::u16string_view newText) {
    EditorSession& session = workspace.active();
    if (!session.csvModel().has_value() || rowIndex >= session.csvRowOrder().size() || !csvModelWorker) {
        return;
    }
    const auto&       model        = *session.csvModel();
    const std::size_t dataRowIndex = session.csvRowOrder()[rowIndex];
    const auto        row          = model.dataRow(dataRowIndex);
    if (colIndex >= row.size()) {
        return;
    }
    const csvmode::CsvCell& cell      = row[colIndex];
    const char16_t          delimiter = csvmode::detectCsvDelimiter(session.document()).value_or(u',');
    const std::u16string    escaped   = csvmode::escapeCsvCellText(newText, delimiter);
    session.dispatcher().dispatch(
        std::make_unique<ReplaceRangeCommand>(TextRange{.start = cell.startPos, .end = cell.endPos()}, escaped));
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    session.beginCsvIndexing(*csvModelWorker, csvmode::CsvParseOptions{.delimiter = delimiter, .hasHeader = true});
}

// Same "create then prime the first position/size explicitly" shape as
// createAndPositionJsonTreePane() above (WI-16c). config.onGetCellText/
// onCellActivated resolve workspace.active() FRESH at invocation time (WI-05's
// own stored-closure convention, see wireNormalMode()'s own header comment) -
// this config is set once here but LVN_GETDISPINFOW/LVN_ITEMACTIVATE fire
// long after, potentially many tab switches later (even though CsvGridPane
// auto-hides on tab switch, see syncViewForActiveSession()'s own comment -
// resolving fresh keeps this robust regardless of exact message ordering).
// config.onCellActivated: colIndex==0 is CsvGridPane's own synthesized
// row-number column (see csv_grid_pane.h's CsvGridPaneConfig::onCellActivated
// comment for why this index space is NOT pre-shifted the way
// onGetCellText's is) - jump to that row's own first cell instead of a real
// CSV column. Reuses jumpToOutlinePosition() as-is (same "targetPos into the
// SAME open document" contract JsonTreePane/OutlinePane already established)
// and then hides the pane itself + clears the pending token - unlike
// OutlinePane/JsonTreePane's onItemSelected, which leaves the pane open
// (see those classes' own header comments), CsvGridPane covers the entire
// client area, so a jump result is invisible until the pane closes.
// WI-16e: onGetCellText/onCellActivated now translate `rowIndex` through
// session.csvRowOrder() before touching the CsvModel - see each lambda's own
// inline comment.
// WI-16f: gained csvModelWorker - onCellEditCommitted needs it to re-index
// after a cell edit (see applyCsvCellEdit()'s own comment).
void createAndPositionCsvGridPane(HWND hwnd, HINSTANCE hInstance, Workspace& workspace,
                                  RenderPipeline& renderPipeline, CsvGridPane& csvGridPane,
                                  const void*& csvGridPanePendingSessionToken,
                                  std::optional<csvmode::CsvModelWorker>& csvModelWorker) {
    CsvGridPaneConfig config{};
    // WI-16e: `rowIndex` from CsvGridPane is now a DISPLAY row index
    // (0..session.csvRowOrder().size()-1, i.e. into the filtered/sorted
    // view), not a raw CsvModel data-row index - session.csvRowOrder()[
    // rowIndex] is the translation step both callbacks below need before
    // touching csvGridCellText()/CsvModel::dataRow() (both of which still
    // expect an actual data-row index, unchanged from WI-16c/WI-16d).
    config.onGetCellText = [&workspace](std::size_t rowIndex, std::size_t colIndex) -> std::u16string {
        const EditorSession& session = workspace.active();
        if (!session.csvModel().has_value() || rowIndex >= session.csvRowOrder().size()) {
            return u"";
        }
        const std::size_t dataRowIndex = session.csvRowOrder()[rowIndex];
        return neomifes::app::csvGridCellText(*session.csvModel(), session.document(), dataRowIndex, colIndex);
    };
    config.onCellActivated = [hwnd, &workspace, &renderPipeline, &csvGridPane, &csvGridPanePendingSessionToken](
                                 std::size_t rowIndex, std::size_t colIndex) {
        EditorSession& session = workspace.active();
        if (!session.csvModel().has_value() || rowIndex >= session.csvRowOrder().size()) {
            return;
        }
        const auto&        model         = *session.csvModel();
        const std::size_t  dataRowIndex  = session.csvRowOrder()[rowIndex];
        const auto         row           = model.dataRow(dataRowIndex);
        const std::size_t  dataColIndex  = colIndex == 0 ? 0 : colIndex - 1;
        if (dataColIndex >= row.size()) {
            return;
        }
        jumpToOutlinePosition(row[dataColIndex].startPos, hwnd, session, renderPipeline);
        csvGridPane.hide();
        csvGridPanePendingSessionToken = nullptr;
    };
    // WI-16e: 150ms-debounced (or Enter-immediate) filter text change - the
    // row COUNT changes but columns/labels don't, so setRowCount() (not
    // showWith()) preserves any column widths the user has drag-resized.
    config.onFilterQueryChanged = [&workspace, &csvGridPane](std::u16string_view query) {
        EditorSession& session = workspace.active();
        if (!session.csvModel().has_value()) {
            return;
        }
        csvmode::CsvFilterOptions filter;
        filter.query = std::u16string(query);
        session.setCsvFilter(std::move(filter));
        csvGridPane.setRowCount(session.csvRowOrder().size());
    };
    // WI-16e: column header click - "#"(colIndex==0) resets to unsorted;
    // a real CSV column cycles Ascending -> Descending -> unsorted on
    // repeated clicks of the SAME column, or jumps straight to Ascending
    // when a DIFFERENT column is clicked. showWith() (not setRowCount()) is
    // required here because the sort-arrow suffix changes a column LABEL,
    // not just the row count.
    config.onSortColumnClicked = [&workspace, &csvGridPane](std::size_t colIndex) {
        EditorSession& session = workspace.active();
        if (!session.csvModel().has_value()) {
            return;
        }
        csvmode::CsvSortOptions sort;  // colIndex==0: stays default (unsorted)
        if (colIndex != 0) {
            const std::size_t              dataCol = colIndex - 1;
            const csvmode::CsvSortOptions& current  = session.csvSort();
            if (current.column == dataCol && current.direction == csvmode::CsvSortDirection::Ascending) {
                sort = csvmode::CsvSortOptions{.column = dataCol, .direction = csvmode::CsvSortDirection::Descending};
            } else if (current.column != dataCol || current.direction != csvmode::CsvSortDirection::Descending) {
                sort = csvmode::CsvSortOptions{.column = dataCol, .direction = csvmode::CsvSortDirection::Ascending};
            }
            // else: 3rd click on the same column - sort stays default
            // (unsorted).
        }
        session.setCsvSort(sort);
        const auto& model = *session.csvModel();
        csvGridPane.showWith(neomifes::app::buildCsvGridColumnLabels(model, session.document(), session.csvSort()),
                             session.csvRowOrder().size());
    };
    config.onClosed = [hwnd, &csvGridPanePendingSessionToken]() {
        csvGridPanePendingSessionToken = nullptr;
        ::SetFocus(hwnd);
    };
    // WI-16f: see applyCsvCellEdit()'s own comment for the full commit flow.
    config.onCellEditCommitted = [hwnd, &renderPipeline, &workspace, &csvModelWorker](
                                     std::size_t rowIndex, std::size_t colIndex, const std::u16string& newText) {
        applyCsvCellEdit(hwnd, renderPipeline, workspace, csvModelWorker, rowIndex, colIndex, newText);
    };
    // WI-16f: vetoes opening a SECOND cell editor while a previous edit's
    // re-index is still in flight - see CsvGridPaneConfig::canBeginCellEdit's
    // own comment for why (stale CsvCell positions would corrupt the
    // document).
    config.canBeginCellEdit = [&workspace]() { return !workspace.active().csvIndexInFlight(); };
    if (!csvGridPane.create(hwnd, hInstance, config)) {
        return;
    }
    RECT clientRect{};
    ::GetClientRect(hwnd, &clientRect);
    const auto dpiScale = static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0F;
    csvGridPane.onParentResized(static_cast<std::uint32_t>(clientRect.right),
                                static_cast<std::uint32_t>(clientRect.bottom), dpiScale, TabBar::heightDips(),
                                StatusBar::heightDips());
}

// WI-05: builds TabBar's item list from workspace's current session list.
// Untitled sessions are numbered by their position among ONLY the
// currently-untitled sessions (1-based) - so 2 simultaneously-open blank
// tabs read "Untitled 1"/"Untitled 2" rather than both showing "Untitled 1"
// with no way to tell them apart (formatTabBaseLabel()'s own comment
// documents this convention; this is the one call site that decides the
// ordinal). At step 2, workspace always holds exactly one session, so this
// always returns a single-element vector - already written for the general
// N-session case step 3 will actually exercise.
std::vector<TabBarItem> buildTabBarItems(const Workspace& workspace) {
    std::vector<TabBarItem> items;
    items.reserve(workspace.sessionCount());
    std::size_t untitledOrdinal = 0;
    for (std::size_t i = 0; i < workspace.sessionCount(); ++i) {
        const EditorSession& session = workspace.sessionAt(i);
        std::optional<std::wstring> filename;
        if (session.isUntitled()) {
            ++untitledOrdinal;
        } else {
            filename = session.path().filename().wstring();
        }
        items.push_back(TabBarItem{
            .label   = neomifes::ui::formatTabBaseLabel(filename, untitledOrdinal),
            .isDirty = session.isDirty(),
        });
    }
    return items;
}

// WI-07 step8: the OS title bar text for the currently ACTIVE session only
// (unlike buildTabBarItems(), which covers every open tab) - reuses
// ui::formatWindowTitle()'s pure formatting, same "filename() only, nullopt
// for untitled" convention buildTabBarItems() already established above.
std::wstring buildWindowTitle(const EditorSession& session) {
    std::optional<std::wstring> filename;
    if (!session.isUntitled()) {
        filename = session.path().filename().wstring();
    }
    return neomifes::ui::formatWindowTitle(filename, session.isDirty());
}

// WI-05 step 2: creates+positions+populates the tab strip, same "create
// then prime the first position/size explicitly" shape
// createAndPositionOutlinePane() above already established (onDeferredInit
// runs strictly after the one-off startup WM_SIZE, so a widget created here
// needs its own first onParentResized() call - see that function's own
// comment for why). Unlike OutlinePane, TabBar also needs an initial
// setTabs() call here: it is always visible (never toggled), so it must
// show real data from its very first paint, not just once first interacted
// with. WI-05 step 3: config.onTabSelected is real now - TCN_SELCHANGE
// (fired when the user clicks a different tab) activates that session and
// restores its view via syncViewForActiveSession(), same as
// dispatchCommand()'s TabNext/TabPrevious/TabSwitch* cases (WI-07 step2).
void createAndPositionTabBar(HWND hwnd, HINSTANCE hInstance, Workspace& workspace, RenderPipeline& renderPipeline,
                             FindDialog& findDialog, TabBar& tabBar, CsvGridPane& csvGridPane,
                             const void*& csvGridPanePendingSessionToken) {
    TabBarConfig config{};
    config.onTabSelected = [hwnd, &workspace, &renderPipeline, &findDialog, &csvGridPane,
                            &csvGridPanePendingSessionToken](std::size_t index) {
        workspace.activate(index);
        syncViewForActiveSession(hwnd, renderPipeline, workspace.active(), findDialog, csvGridPane,
                                 csvGridPanePendingSessionToken);
    };
    if (!tabBar.create(hwnd, hInstance, config)) {
        return;
    }
    RECT clientRect{};
    ::GetClientRect(hwnd, &clientRect);
    const auto dpiScale = static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0F;
    tabBar.onParentResized(static_cast<std::uint32_t>(clientRect.right), dpiScale);
    tabBar.setTabs(buildTabBarItems(workspace), workspace.activeIndex());
}

// WI-07 step6: encoding options presented in the status bar's encoding-part
// click menu - one entry per encoding::Encoding enumerator (13), matching
// formatStatusBarEncoding()'s own switch so the label the user picks is
// exactly what the status bar shows afterward. `writeBom` mirrors the
// enumerator's own *Bom-ness, kept in lockstep so the selection also
// matches what actually gets written on the next Ctrl+S - file_saver.cpp's
// saveFile() derives BOM presence purely from the separate `writeBom` flag
// (session.fileState().encoding's own *Bom suffix is normalized away for
// the body either way, see encoding::withBom()'s header comment), so
// picking "UTF-8 BOM" without also setting writeBom=true would silently
// write no BOM despite what the status bar displays.
struct EncodingMenuItem {
    encoding::Encoding encoding;
    bool                writeBom;
};
constexpr std::array<EncodingMenuItem, 13> kEncodingMenuItems = {{
    {.encoding = encoding::Encoding::Utf8, .writeBom = false},
    {.encoding = encoding::Encoding::Utf8Bom, .writeBom = true},
    {.encoding = encoding::Encoding::Utf16Le, .writeBom = false},
    {.encoding = encoding::Encoding::Utf16LeBom, .writeBom = true},
    {.encoding = encoding::Encoding::Utf16Be, .writeBom = false},
    {.encoding = encoding::Encoding::Utf16BeBom, .writeBom = true},
    {.encoding = encoding::Encoding::Utf32Le, .writeBom = false},
    {.encoding = encoding::Encoding::Utf32LeBom, .writeBom = true},
    {.encoding = encoding::Encoding::Utf32Be, .writeBom = false},
    {.encoding = encoding::Encoding::Utf32BeBom, .writeBom = true},
    {.encoding = encoding::Encoding::ShiftJis, .writeBom = false},
    {.encoding = encoding::Encoding::EucJp, .writeBom = false},
    {.encoding = encoding::Encoding::Iso2022Jp, .writeBom = false},
}};

// WI-07 step6: line-ending options - deliberately excludes
// encoding::LineEnding::Mixed. encoding.h's convertLineEndings() doc
// comment: Mixed "is not a meaningful save target" and is silently treated
// as Lf - offering it as a menu choice a user could actively pick would be
// misleading (it isn't a distinct save format, just a detected property of
// existing content).
constexpr std::array<encoding::LineEnding, 3> kLineEndingMenuItems = {
    encoding::LineEnding::Crlf,
    encoding::LineEnding::Lf,
    encoding::LineEnding::Cr,
};

// Shows a popup menu at `screenPt` with one item per `items[i]`, labeled via
// `formatFn`, and returns the selection (nullopt if dismissed without
// choosing - Escape, click-away). Shared by the encoding/line-ending click
// handlers below, which are otherwise mechanically identical (CLAUDE.md
// rule 4 - avoid the near-duplicate function bodies a copy-paste would
// leave). TPM_RETURNCMD makes TrackPopupMenu() return the chosen item's id
// synchronously instead of posting WM_COMMAND - these ids are only ever
// used within this single call and never collide with CommandId's own
// 40000+ range (see command_ids.h's own comment) since no WM_COMMAND is
// generated at all. The SetForegroundWindow()/PostMessageW(WM_NULL) pair
// is MSDN's own documented TrackPopupMenu idiom ("How to Use the
// TrackPopupMenu Function") - without it the menu can fail to dismiss on
// an outside click if `hwnd` isn't already the foreground window.
template <typename T>
[[nodiscard]] std::optional<T> showChoiceMenu(HWND hwnd, POINT screenPt, std::span<const T> items,
                                              const std::function<std::wstring(T)>& formatFn) {
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        ::AppendMenuW(menu, MF_STRING, i + 1, formatFn(items[i]).c_str());
    }
    ::SetForegroundWindow(hwnd);
    const int selected = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screenPt.x,
                                          screenPt.y, 0, hwnd, nullptr);
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
    ::DestroyMenu(menu);
    if (selected <= 0 || static_cast<std::size_t>(selected) > items.size()) {
        return std::nullopt;
    }
    return items[selected - 1];
}

// WI-07 step6: status bar encoding/line-ending part click handler
// (StatusBarConfig::onPartClicked, wired below) - presents a popup menu and
// writes the selection straight into session.fileState(). No document
// mutation - only the NEXT Ctrl+S's write format changes (performSave()
// already reuses fileState() this same way for every save, see its own
// comment). `partIndex` matches StatusBarParts' field order (2=encoding,
// 3=lineEnding); any other index is a click on a part this step doesn't
// make interactive (position/selectionCount/overwriteMode/language) and is
// silently ignored. workspace.active() is re-resolved at click time (not
// captured), same "never cache a stale EditorSession&" rule this file's
// other Workspace&-capturing lambdas follow.
void handleStatusBarPartClicked(std::size_t partIndex, POINT screenPt, HWND hwnd, Workspace& workspace) {
    EditorSession& session = workspace.active();
    if (partIndex == 2) {
        const auto choice = showChoiceMenu<EncodingMenuItem>(
            hwnd, screenPt, kEncodingMenuItems,
            [](EncodingMenuItem item) { return neomifes::app::formatStatusBarEncoding(item.encoding); });
        if (choice) {
            session.fileState().encoding = choice->encoding;
            session.fileState().writeBom = choice->writeBom;
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else if (partIndex == 3) {
        const auto choice = showChoiceMenu<encoding::LineEnding>(hwnd, screenPt, kLineEndingMenuItems,
                                                                  &neomifes::app::formatStatusBarLineEnding);
        if (choice) {
            session.fileState().lineEnding = *choice;
            ::InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
}

// WI-07 step4: create+position. WI-07 step6: now takes Workspace& too, to
// build a StatusBarConfig::onPartClicked that resolves the active session
// at click time - see handleStatusBarPartClicked() above.
void createAndPositionStatusBar(HWND hwnd, HINSTANCE hInstance, Workspace& workspace, StatusBar& statusBar) {
    StatusBarConfig config{};
    config.onPartClicked = [hwnd, &workspace](std::size_t partIndex, POINT screenPt) {
        handleStatusBarPartClicked(partIndex, screenPt, hwnd, workspace);
    };
    if (!statusBar.create(hwnd, hInstance, config)) {
        return;
    }
    RECT clientRect{};
    ::GetClientRect(hwnd, &clientRect);
    const auto dpiScale = static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0F;
    statusBar.onParentResized(static_cast<std::uint32_t>(clientRect.right),
                              static_cast<std::uint32_t>(clientRect.bottom), dpiScale);
}

// WI-07 step9: right-click context menu - reuses menu_bar.h's kEditMenuItems
// verbatim (Undo/Redo/Cut/Copy/Paste, same CommandIds/labels the Edit menu
// itself already shows) so the two can never drift apart. TrackPopupMenu
// (TPM_RETURNCMD) returns the selected item's raw id - since every id here
// IS already a CommandId (unlike showChoiceMenu<T>() below, which assigns
// synthetic 1-based ids), the return value is handed to dispatchCommand()
// directly with no index-to-value translation, matching the approved
// design ("TPM_RETURNCMDの戻り値をdispatchCommand()へそのまま渡す"). `screenPt`
// is MainWindowConfig::onContextMenu's own screen-coordinate contract -
// MainWindow itself already substitutes the cursor position for a
// keyboard-triggered invocation (Shift+F10), see that field's doc comment.
// No caret movement to the click position - deliberately out of scope for
// this step.
void showEditContextMenu(HWND hwnd, POINT screenPt, Workspace& workspace, RenderPipeline& renderPipeline,
                         FindDialog& findDialog, core::RecentFiles& recentFiles, const MenuBarHandles& menuHandles,
                         const core::Settings& settings, AutosaveContext& autosave, CsvGridPane& csvGridPane,
                         const void*& csvGridPanePendingSessionToken, git::GitDiffWorker& gitDiffWorker,
                         SessionManager& sessionManager) {
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    for (const MenuItemSpec& item : kEditMenuItems) {
        ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(item.commandId), item.label);
    }
    // Same SetForegroundWindow()/PostMessageW(WM_NULL) idiom showChoiceMenu<T>()
    // (WI-07 step6, above) already established for a modal TrackPopupMenu()
    // to dismiss correctly on an outside click.
    ::SetForegroundWindow(hwnd);
    const int selected = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screenPt.x,
                                          screenPt.y, 0, hwnd, nullptr);
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
    ::DestroyMenu(menu);
    if (selected <= 0) {
        return;  // dismissed without a choice (Escape, click-away)
    }
    const CommandDispatchContext ctx{.hwnd                           = hwnd,
                                     .workspace                      = workspace,
                                     .renderPipeline                 = renderPipeline,
                                     .findDialog                        = findDialog,
                                     .recentFiles                    = recentFiles,
                                     .menuHandles                    = menuHandles,
                                     .autosave                       = autosave,
                                     .settings                       = settings,
                                     .csvGridPane                    = csvGridPane,
                                     .csvGridPanePendingSessionToken = csvGridPanePendingSessionToken,
                                     .gitDiffWorker                  = gitDiffWorker,
                                     .sessionManager                 = sessionManager};
    dispatchCommand(static_cast<CommandId>(selected), ctx);
}

// WI-18a: tab strip's right-click menu (閉じる/他のタブを閉じる/すべて閉じる) -
// same TrackPopupMenu(TPM_RETURNCMD)-then-dispatchCommand() shape as
// showEditContextMenu() above. Activates `tabIndex` first if it isn't
// already the active tab - a right-click on a background tab should act on
// the tab actually clicked, matching how a left-click there would also
// switch tabs first; dispatchTabCloseCommand()/dispatchTabCloseOthersCommand()/
// dispatchTabCloseAllCommand() all operate on workspace.active(), so this
// activation is what makes the menu's 3 items act on the right tab.
void showTabContextMenu(HWND hwnd, POINT screenPt, std::size_t tabIndex, Workspace& workspace,
                        RenderPipeline& renderPipeline, FindDialog& findDialog, core::RecentFiles& recentFiles,
                        const MenuBarHandles& menuHandles, const core::Settings& settings, AutosaveContext& autosave,
                        CsvGridPane& csvGridPane, const void*& csvGridPanePendingSessionToken,
                        git::GitDiffWorker& gitDiffWorker, SessionManager& sessionManager) {
    if (tabIndex < workspace.sessionCount() && tabIndex != workspace.activeIndex()) {
        workspace.activate(tabIndex);
        syncViewForActiveSession(hwnd, renderPipeline, workspace.active(), findDialog, csvGridPane,
                                csvGridPanePendingSessionToken);
    }
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(CommandId::TabClose), L"閉じる(&C)");
    ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(CommandId::TabCloseOthers), L"他のタブを閉じる(&O)");
    ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(CommandId::TabCloseAll), L"すべて閉じる(&A)");
    // Same SetForegroundWindow()/PostMessageW(WM_NULL) idiom showEditContextMenu()
    // above uses for a modal TrackPopupMenu() to dismiss correctly on an
    // outside click.
    ::SetForegroundWindow(hwnd);
    const int selected = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, screenPt.x,
                                          screenPt.y, 0, hwnd, nullptr);
    ::PostMessageW(hwnd, WM_NULL, 0, 0);
    ::DestroyMenu(menu);
    if (selected <= 0) {
        return;  // dismissed without a choice (Escape, click-away)
    }
    const CommandDispatchContext ctx{.hwnd                           = hwnd,
                                     .workspace                      = workspace,
                                     .renderPipeline                 = renderPipeline,
                                     .findDialog                        = findDialog,
                                     .recentFiles                    = recentFiles,
                                     .menuHandles                    = menuHandles,
                                     .autosave                       = autosave,
                                     .settings                       = settings,
                                     .csvGridPane                    = csvGridPane,
                                     .csvGridPanePendingSessionToken = csvGridPanePendingSessionToken,
                                     .gitDiffWorker                  = gitDiffWorker,
                                     .sessionManager                 = sessionManager};
    dispatchCommand(static_cast<CommandId>(selected), ctx);
}

// WI-18a: WM_CONTEXTMENU dispatcher - `source` distinguishes a right-click
// that landed directly on this window's own client area from one that
// bubbled up from an unhandled child control (TabBar/StatusBar - Win32's own
// default behavior for a control that doesn't handle WM_CONTEXTMENU itself
// is to forward it to its parent). Previously every right-click anywhere in
// the window - including on the tab strip or status bar, or on the gutter/
// minimap within the main window's own client area - produced the identical
// Undo/Redo/Cut/Copy/Paste menu meant for the text content only.
void handleContextMenuEvent(HWND source, HWND hwnd, std::int32_t xScreen, std::int32_t yScreen, Workspace& workspace,
                            RenderPipeline& renderPipeline, FindDialog& findDialog, TabBar& tabBar,
                            core::RecentFiles& recentFiles, const MenuBarHandles& menuHandles,
                            const core::Settings& settings, AutosaveContext& autosave, CsvGridPane& csvGridPane,
                            const void*& csvGridPanePendingSessionToken, git::GitDiffWorker& gitDiffWorker,
                            SessionManager& sessionManager) {
    const POINT screenPt{.x = xScreen, .y = yScreen};
    if (source == tabBar.hwnd()) {
        POINT clientPt = screenPt;
        ::ScreenToClient(tabBar.hwnd(), &clientPt);
        // `flags` is an output-only field TabCtrl_HitTest() fills in - zero-
        // initialized here only to satisfy clang-tidy's designated-
        // initializer completeness check, its input value is never read.
        TCHITTESTINFO hitTestInfo{.pt = clientPt, .flags = 0};
        const int tabIndex = TabCtrl_HitTest(tabBar.hwnd(), &hitTestInfo);
        if (tabIndex < 0) {
            return;  // right-clicked the tab strip's own empty margin, not a tab
        }
        showTabContextMenu(hwnd, screenPt, static_cast<std::size_t>(tabIndex), workspace, renderPipeline, findDialog,
                           recentFiles, menuHandles, settings, autosave, csvGridPane,
                           csvGridPanePendingSessionToken, gitDiffWorker, sessionManager);
        return;
    }
    if (source != hwnd) {
        return;  // bubbled from StatusBar or any other non-text control - no menu there yet
    }
    POINT clientPt = screenPt;
    ::ScreenToClient(hwnd, &clientPt);
    const auto dpiScale = static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0F;
    // Excludes the left gutter (line numbers/fold markers/diff markers,
    // gutterWidthDips()), the minimap strip (hitTestMinimap()), and any
    // point below/beyond the document's own rendered content (hitTest()
    // returns nullopt there) - the same 3 regions handleMouseDownEvent()
    // above already treats specially for a LEFT click, reused here so a
    // right-click gets consistent treatment.
    if (static_cast<float>(clientPt.x) < renderPipeline.gutterWidthDips() * dpiScale ||
        renderPipeline.hitTestMinimap(clientPt.x, clientPt.y) || !renderPipeline.hitTest(clientPt.x, clientPt.y)) {
        return;
    }
    showEditContextMenu(hwnd, screenPt, workspace, renderPipeline, findDialog, recentFiles, menuHandles, settings,
                        autosave, csvGridPane, csvGridPanePendingSessionToken, gitDiffWorker, sessionManager);
}

// WI-07 step4: derives every ui::StatusBarParts field from EditorSession's
// already-existing state - no new state introduced here. WI-07 step5:
// overwriteMode now reflects the real EditorSession::overwriteMode() toggle
// (was hardcoded false/INS in step4).
StatusBarParts buildStatusBarParts(const EditorSession& session) {
    const Document&                 document = session.document();
    const neomifes::document::TextPos pos    = session.selection().primaryCursor().position;
    const auto                       line    = document.offsetToLine(pos);
    const auto                       column  = static_cast<std::uint32_t>(pos - document.lineToOffset(line));
    return StatusBarParts{
        .position       = neomifes::app::formatStatusBarPosition(line, column),
        .selectionCount = neomifes::app::formatStatusBarSelectionCount(
            neomifes::core::totalSelectedLength(session.selection())),
        .encoding      = neomifes::app::formatStatusBarEncoding(session.fileState().encoding),
        .lineEnding    = neomifes::app::formatStatusBarLineEnding(session.fileState().lineEnding),
        .overwriteMode = neomifes::app::formatStatusBarOverwriteMode(session.overwriteMode()),
        .language      = neomifes::app::formatStatusBarLanguage(session.language()),
    };
}

// WI-02: cfg.onDropFiles body, pulled out of wireNormalMode() for the same
// cognitive-complexity reason as handleMouseDownEvent()/handleKeyDownEvent()
// above. WI-05: opens EVERY dropped path into its own tab via
// Workspace::openFile() (previously only the first path, see build_plan.md's
// original WI-02 scope cut) - the last path opened/activated ends up active
// (Workspace::openFile() updates activeIndex() on every call, VSCode's own
// "last dropped file wins focus" convention), so no extra bookkeeping is
// needed beyond a single trailing syncViewForActiveSession() call. No
// confirmDiscardIfDirty() gate, same reasoning as dispatchCommand()'s Open
// case (WI-07 step2) - opening into new/existing OTHER tabs never touches
// the currently-active tab's content.
void handleDropFilesEvent(HWND hwnd, const std::vector<std::wstring>& paths, Workspace& workspace,
                          RenderPipeline& renderPipeline, FindDialog& findDialog, core::RecentFiles& recentFiles,
                          const MenuBarHandles& menuHandles, CsvGridPane& csvGridPane,
                          const void*& csvGridPanePendingSessionToken) {
    if (paths.empty()) {
        return;
    }
    for (const auto& path : paths) {
        const auto result = workspace.openFile(path);
        if (const auto* error = std::get_if<LoadError>(&result)) {
            neomifes::app::showOpenErrorDialog(hwnd, *error);
        } else {
            recentFiles.record(path);
        }
    }
    refreshRecentFilesMenu(menuHandles, hwnd, recentFiles);
    syncViewForActiveSession(hwnd, renderPipeline, workspace.active(), findDialog, csvGridPane,
                             csvGridPanePendingSessionToken);
}

// WI-06: cfg.onImeStartComposition body. Captures the PRE-collapse primary
// cursor's selection (if any) as the range composing will eventually
// replace, BEFORE calling collapseToPrimary() - collapseToPrimary() itself
// sets the surviving cursor's anchor==position (see selection_model.cpp),
// so capturing the range after that call would always yield an empty
// range and silently drop whatever text was selected when the user started
// typing. The captured range is stored as ImeComposition::anchorRange (via
// an initial empty-text composition) rather than a separate wWinMain-level
// variable - RenderPipeline already owns exactly this "fixed for the whole
// composition session" value (see ImeComposition's own header comment), so
// handleImeCompositionEvent()/handleImeResultEvent() below read it back
// from there instead of duplicating storage.
void handleImeStartComposition(HWND hwnd, EditorSession& session, RenderPipeline& renderPipeline,
                               bool& imeComposing) {
    const Cursor& primaryBefore = session.selection().primaryCursor();
    const TextRange anchorRange{.start = std::min(primaryBefore.position, primaryBefore.anchor),
                                .end   = std::max(primaryBefore.position, primaryBefore.anchor)};
    session.selection().collapseToPrimary();
    renderPipeline.setImeComposition(ImeComposition{
        .anchorRange       = anchorRange,
        .text              = u"",
        .targetClauseRange = std::nullopt,
    });
    imeComposing = true;
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

// WI-06: cfg.onImeComposition body - GCS_COMPSTR (the composition string
// changed). No-op if no composition was ever started (defensive only -
// WM_IME_STARTCOMPOSITION always precedes WM_IME_COMPOSITION per Win32's
// own message ordering, see main_window.h's WI-06 comment). Reuses the
// anchorRange handleImeStartComposition() already stored - see this
// function's own comment for why that is the single source of truth
// instead of a separately threaded variable.
void handleImeCompositionEvent(HWND hwnd, std::u16string text,
                               std::optional<std::pair<std::uint32_t, std::uint32_t>> targetClauseRange,
                               RenderPipeline& renderPipeline) {
    const auto& current = renderPipeline.imeComposition();
    if (!current) {
        return;
    }
    renderPipeline.setImeComposition(ImeComposition{
        .anchorRange       = current->anchorRange,
        .text              = std::move(text),
        .targetClauseRange = targetClauseRange,
    });
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// WI-06: cfg.onImeResult body - GCS_RESULTSTR (the IME just committed
// text). Dispatches exactly ONE ReplaceRangeCommand against the captured
// anchorRange - this is what makes the commit a single Undo step (the
// composition text itself was never written to Document, see
// ImeComposition's header comment) and, since ReplaceRangeCommand replaces
// [anchorRange.start, anchorRange.end), what makes composing over an
// initial selection behave like ordinary typeover. No-op if no composition
// was ever started (same defensive-only reasoning as
// handleImeCompositionEvent() above).
void handleImeResultEvent(HWND hwnd, std::u16string resultText, EditorSession& session,
                          RenderPipeline& renderPipeline) {
    if (!renderPipeline.imeComposition()) {
        return;
    }
    const TextRange anchorRange = renderPipeline.imeComposition()->anchorRange;
    session.dispatcher().dispatch(std::make_unique<ReplaceRangeCommand>(anchorRange, std::move(resultText)));
    renderPipeline.setImeComposition(std::nullopt);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
}

// WI-06: repositions the IME candidate window every frame an in-progress
// composition actually drew one - imeCandidateAnchorPx() is a side effect
// of drawImeCompositionOnLine() (only set when render() actually walked
// past the composition's line), so this must run AFTER render() succeeds,
// not from the onImeComposition hook itself (which fires before the next
// paint has happened). Pulled out of wireNormalMode()'s nested paint-handler
// lambda for the same cognitive-complexity-budget reason wireImeHooks()
// below was - a nested `if` inside a lambda inside wireNormalMode() still
// counts toward wireNormalMode()'s own complexity (see wireImeHooks()'s
// comment).
void updateImeCandidatePosition(MainWindow& window, const RenderPipeline& renderPipeline) noexcept {
    if (const auto candidateAnchor = renderPipeline.imeCandidateAnchorPx()) {
        window.setImeCandidatePosition(*candidateAnchor);
    }
}

// WI-06: assigns the 4 IME hooks - pulled out of wireNormalMode() itself
// for the same "keep cfg.* assignment bodies out of that one giant function"
// reason handleKeyDownEvent()/handleHScrollEvent() above were already
// extracted for (wireNormalMode()'s own cognitive complexity is at the
// clang-tidy threshold; 4 more inline lambda bodies pushed it over). See
// main_window.h's own WI-06 header comment for why MainWindow decodes the
// raw Imm32 payload itself, so these lambdas only ever see already-decoded
// values (std::u16string text, optional target-clause range). onImeResult
// resolves workspace.active() fresh (not the session captured by
// onImeStartComposition) purely for consistency with every other stored
// callback in this function - in practice the two always agree, since
// imeComposing (threaded through wireNormalMode()'s own cfg.onKeyDown)
// suppresses the keyboard-driven tab-switch keys that could otherwise move
// workspace.active() mid-composition (mouse clicks on the tab strip are a
// known, accepted gap - see handleImeStartComposition()'s comment).
void wireImeHooks(MainWindowConfig& cfg, Workspace& workspace, RenderPipeline& renderPipeline,
                  bool& imeComposing) {
    cfg.onImeStartComposition = [&workspace, &renderPipeline, &imeComposing](HWND hwnd) {
        handleImeStartComposition(hwnd, workspace.active(), renderPipeline, imeComposing);
    };
    cfg.onImeComposition = [&renderPipeline](
                               HWND hwnd, std::u16string text,
                               std::optional<std::pair<std::uint32_t, std::uint32_t>> targetClauseRange) {
        handleImeCompositionEvent(hwnd, std::move(text), targetClauseRange, renderPipeline);
    };
    cfg.onImeResult = [&workspace, &renderPipeline](HWND hwnd, std::u16string resultText) {
        handleImeResultEvent(hwnd, std::move(resultText), workspace.active(), renderPipeline);
    };
    cfg.onImeEndComposition = [&renderPipeline, &imeComposing](HWND hwnd) {
        // Clears any leftover overlay even on a committed session (already
        // cleared by handleImeResultEvent() above, so this is a no-op then)
        // - the one case where it's NOT already clear is a cancelled
        // composition (Escape etc.), which never reaches onImeResult at all
        // (see ImeComposition's header comment: only GCS_RESULTSTR writes to
        // Document).
        renderPipeline.setImeComposition(std::nullopt);
        imeComposing = false;
        ::InvalidateRect(hwnd, nullptr, FALSE);
    };
}

// dispatchCommand()'s case bodies, extracted into their own functions (WI-07
// step2) purely to keep dispatchCommand() itself under clang-tidy's
// cognitive-complexity threshold - one flat switch with every case's full
// body inline measured well over it. Grouped by the families
// CommandDispatchContext's own case-label list already falls into; each
// function's body is otherwise unchanged from dispatchCommand()'s former
// inline case. Defined here (inside the anonymous namespace, same as this
// file's other private helpers) rather than next to dispatchCommand() itself
// so they're all in scope via ordinary unqualified lookup - see
// dispatchCommand()'s own comment for why IT must stay outside this
// namespace.

void dispatchSaveCommand(bool forceSaveAs, const CommandDispatchContext& ctx, EditorSession& session) {
    if (performSave(ctx.hwnd, session, forceSaveAs, ctx.settings, ctx.recentFiles, ctx.menuHandles,
                    ctx.autosave)) {
        // WI-17d: auto re-diff on every explicit save - the automatic
        // counterpart to the manual "Git: Refresh Diff Markers" palette
        // command (dispatchGitRefreshDiffCommand(), WI-17c). Safe to call
        // unconditionally, including for a first-time Save-As of an
        // Untitled buffer: performSave() above already called
        // session.setSavedPath() before returning true (setSavedPath()
        // flips isUntitled() to false synchronously), so
        // beginGitDiffIndexing()'s own pathIfNamed() check here already
        // sees the just-assigned path. Fire-and-forget - the result lands
        // later via kMsgGitDiffReady -> applyGitDiffReadyMessage(), which
        // does its own InvalidateRect() once the diff is ready; the
        // InvalidateRect() below is unrelated (tab-strip dirty marker) and
        // does not need to change.
        session.beginGitDiffIndexing(ctx.gitDiffWorker);
        // WI-05: the tab strip's unsaved-changes marker is only refreshed on
        // the next repaint - see handleSaveKey()'s former comment for why
        // this is needed.
        ::InvalidateRect(ctx.hwnd, nullptr, FALSE);
    }
}

void dispatchOpenCommand(const CommandDispatchContext& ctx) {
    const auto chosen = neomifes::app::showOpenFileDialog(ctx.hwnd);
    if (!chosen) {
        return;  // Open dialog cancelled - nothing to do
    }
    const auto result = ctx.workspace.openFile(*chosen);
    if (const auto* error = std::get_if<LoadError>(&result)) {
        neomifes::app::showOpenErrorDialog(ctx.hwnd, *error);
        return;
    }
    ctx.recentFiles.record(*chosen);
    refreshRecentFilesMenu(ctx.menuHandles, ctx.hwnd, ctx.recentFiles);
    syncViewForActiveSession(ctx.hwnd, ctx.renderPipeline, ctx.workspace.active(), ctx.findDialog, ctx.csvGridPane,
                            ctx.csvGridPanePendingSessionToken);
}

void dispatchNewCommand(const CommandDispatchContext& ctx) {
    static_cast<void>(ctx.workspace.openBlank());
    syncViewForActiveSession(ctx.hwnd, ctx.renderPipeline, ctx.workspace.active(), ctx.findDialog, ctx.csvGridPane,
                            ctx.csvGridPanePendingSessionToken);
}

// WI-11: a "最近使ったファイル" submenu click. `index` is already resolved by
// the caller (commandId - kRecentFileIdBase) against recentFiles.entries()'
// current size - out-of-range (stale menu vs. a since-shrunk list) is a
// silent no-op, same defensive posture dispatchCommand()'s own default case
// has for an unrecognized id. Mirrors dispatchOpenCommand()'s open+record+
// refresh+sync sequence; re-recording an already-most-recent entry is
// harmless (just re-writes the same MRU-front position).
void dispatchRecentFileCommand(std::size_t index, const CommandDispatchContext& ctx) {
    const auto& entries = ctx.recentFiles.entries();
    if (index >= entries.size()) {
        return;
    }
    const std::filesystem::path path = entries[index];
    const auto result = ctx.workspace.openFile(path);
    if (const auto* error = std::get_if<LoadError>(&result)) {
        neomifes::app::showOpenErrorDialog(ctx.hwnd, *error);
        return;
    }
    ctx.recentFiles.record(path);
    refreshRecentFilesMenu(ctx.menuHandles, ctx.hwnd, ctx.recentFiles);
    syncViewForActiveSession(ctx.hwnd, ctx.renderPipeline, ctx.workspace.active(), ctx.findDialog, ctx.csvGridPane,
                            ctx.csvGridPanePendingSessionToken);
}

// Handles TabNext/TabPrevious/TabSwitch1..9 - `id` must be one of those (the
// only cases dispatchCommand() routes here).
void dispatchTabSwitchCommand(CommandId id, const CommandDispatchContext& ctx) {
    std::optional<std::size_t> target;
    if (id == CommandId::TabNext) {
        target = nextTabIndex(ctx.workspace.activeIndex(), ctx.workspace.sessionCount());
    } else if (id == CommandId::TabPrevious) {
        target = previousTabIndex(ctx.workspace.activeIndex(), ctx.workspace.sessionCount());
    } else {
        const int digit = static_cast<int>(id) - static_cast<int>(CommandId::TabSwitch1) + 1;
        target = tabIndexForDigit(digit, ctx.workspace.sessionCount());
    }
    if (target && *target != ctx.workspace.activeIndex()) {
        ctx.workspace.activate(*target);
        syncViewForActiveSession(ctx.hwnd, ctx.renderPipeline, ctx.workspace.active(), ctx.findDialog, ctx.csvGridPane,
                            ctx.csvGridPanePendingSessionToken);
    }
}

void dispatchTabCloseCommand(const CommandDispatchContext& ctx, EditorSession& session) {
    if (!confirmDiscardIfDirty(ctx.hwnd, session, ctx.settings, ctx.recentFiles, ctx.menuHandles,
                               ctx.autosave)) {
        return;  // user cancelled the unsaved-changes prompt
    }
    if (ctx.workspace.sessionCount() <= 1) {
        ctx.workspace.active().resetToBlank();
        resetViewAfterDocumentSwap(ctx.hwnd, ctx.renderPipeline, ctx.workspace.active(), ctx.findDialog, ctx.csvGridPane,
                               ctx.csvGridPanePendingSessionToken);
        syncRenderStateAndInvalidate(ctx.hwnd, ctx.renderPipeline, ctx.workspace.active());
        return;
    }
    // confirmDiscardIfDirty() above already obtained the user's explicit
    // consent to discard - mark the about-to-be-destroyed session's document
    // saved so Workspace::closeSession()'s own independent dirty check
    // doesn't redundantly re-block it (see handleTabCloseKey()'s former
    // comment for the full rationale).
    if (ctx.workspace.active().isDirty()) {
        ctx.workspace.active().document().markSaved();
    }
    const std::size_t activeIndex = ctx.workspace.activeIndex();
    if (ctx.workspace.closeSession(activeIndex)) {
        syncViewForActiveSession(ctx.hwnd, ctx.renderPipeline, ctx.workspace.active(), ctx.findDialog, ctx.csvGridPane,
                            ctx.csvGridPanePendingSessionToken);
    }
}

// WI-18a: tab context menu's "他のタブを閉じる". Iterates DESCENDING so that
// each Workspace::closeSession(i) call's own index-shifting (indices after
// the erased one move down by one) only ever affects positions this loop
// hasn't visited yet - by the time index `i` is processed, everything above
// it is already gone and everything at/below it is still at its original
// position, so `keepIndex` (captured once, before any erasure) stays a
// valid comparison throughout. Confirms per tab exactly like
// dispatchTabCloseCommand() (a cancelled prompt on one tab simply skips
// that tab and continues with the rest, rather than aborting the whole
// operation).
void dispatchTabCloseOthersCommand(const CommandDispatchContext& ctx) {
    const std::size_t keepIndex = ctx.workspace.activeIndex();
    for (std::size_t i = ctx.workspace.sessionCount(); i-- > 0;) {
        if (i == keepIndex) {
            continue;
        }
        EditorSession& other = ctx.workspace.sessionAt(i);
        if (!confirmDiscardIfDirty(ctx.hwnd, other, ctx.settings, ctx.recentFiles, ctx.menuHandles, ctx.autosave)) {
            continue;  // user cancelled for this tab - leave it open, keep going with the rest
        }
        if (other.isDirty()) {
            other.document().markSaved();
        }
        ctx.workspace.closeSession(i);
    }
    syncViewForActiveSession(ctx.hwnd, ctx.renderPipeline, ctx.workspace.active(), ctx.findDialog, ctx.csvGridPane,
                             ctx.csvGridPanePendingSessionToken);
}

// WI-18a: tab context menu's "すべて閉じる" - closes every OTHER tab first
// (dispatchTabCloseOthersCommand() above), then runs the exact same close
// path a single active tab already goes through (dispatchTabCloseCommand()),
// which both confirms/discards it and resets it to a blank Untitled
// document rather than actually removing it - Workspace::closeSession()
// never allows the session count to drop below one, so "close all" always
// converges to that same single-blank-tab end state.
void dispatchTabCloseAllCommand(const CommandDispatchContext& ctx) {
    dispatchTabCloseOthersCommand(ctx);
    dispatchTabCloseCommand(ctx, ctx.workspace.active());
}

void dispatchCopyCommand(const CommandDispatchContext& ctx, EditorSession& session) {
    const auto text = neomifes::app::textToCopy(session.selection(), session.document());
    if (text) {
        static_cast<void>(neomifes::platform::setClipboardText(ctx.hwnd, *text));
    }
}

void dispatchCutCommand(const CommandDispatchContext& ctx, EditorSession& session) {
    const auto text = neomifes::app::textToCopy(session.selection(), session.document());
    if (!text || !neomifes::platform::setClipboardText(ctx.hwnd, *text)) {
        return;  // never deletes a selection the user didn't get a copy of
    }
    if (neomifes::app::deleteAllSelections(session.dispatcher(), session.selection(), session.viewport(),
                                           session.document())) {
        syncRenderStateAndInvalidate(ctx.hwnd, ctx.renderPipeline, session);
    }
}

void dispatchPasteCommand(const CommandDispatchContext& ctx, EditorSession& session) {
    const auto text = neomifes::platform::getClipboardText(ctx.hwnd);
    if (!text) {
        return;
    }
    neomifes::app::handlePaste(*text, session.dispatcher(), session.selection(), session.viewport(),
                               session.document());
    syncRenderStateAndInvalidate(ctx.hwnd, ctx.renderPipeline, session);
}

// Handles Undo/Redo - `id` must be one of those (the only cases
// dispatchCommand() routes here).
void dispatchUndoRedoCommand(CommandId id, const CommandDispatchContext& ctx, EditorSession& session) {
    const bool changed = id == CommandId::Undo ? session.dispatcher().undo() : session.dispatcher().redo();
    if (changed) {
        syncRenderStateAndInvalidate(ctx.hwnd, ctx.renderPipeline, session);
    }
}

// WI-07 step3: menu-triggered "show/toggle a widget" commands.
// dispatchCommand() (command_dispatch.h) deliberately does NOT handle these
// (see its own header comment - the reason was accelerator-table focus
// interception, which a menu CLICK never causes) - wireNormalMode()'s
// cfg.onCommand checks this FIRST, before falling through to
// dispatchCommand() for the commands that one does handle. Returns whether
// `id` was recognized (every menu click resolves to exactly one CommandId,
// so "recognized but nothing to do" doesn't arise the way it can for
// keyboard chains).
bool dispatchWidgetShowCommand(CommandId id, HWND hwnd, Workspace& workspace, RenderPipeline& renderPipeline,
                               FindDialog& findDialog, FindReplaceDialog& findReplaceDialog,
                               CommandPalette& commandPalette, GrepBar& grepBar,
                               GotoLineBar& gotoLineBar, neomifes::ui::OutlinePane& outlinePane,
                               JsonTreePane& jsonTreePane, GitPane& gitPane,
                               std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker,
                               std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker,
                               const void*& jsonTreePanePendingSessionToken, CsvGridPane& csvGridPane,
                               std::optional<csvmode::CsvModelWorker>& csvModelWorker,
                               const void*& csvGridPanePendingSessionToken, core::Settings& settings,
                               const std::optional<std::filesystem::path>& settingsPath) {
    switch (id) {
        case CommandId::FindShow:
            findDialog.show(hwnd);
            return true;
        // WI-18b: previously findBar.showWithReplace() (the embedded bar's
        // own replace-mode) - now opens the standalone floating dialog
        // instead, per the user's request for a real Win32 Find/Replace
        // dialog. findDialog itself keeps its Ctrl+F incremental-search-only
        // role (see find_dialog.h's class comment).
        case CommandId::FindReplace:
            findReplaceDialog.show(hwnd);
            return true;
        case CommandId::FindNext:
            navigateToMatch(true, hwnd, workspace.active(), renderPipeline, findDialog);
            return true;
        case CommandId::FindPrevious:
            navigateToMatch(false, hwnd, workspace.active(), renderPipeline, findDialog);
            return true;
        case CommandId::GrepShow:
            grepBar.show();
            return true;
        case CommandId::CommandPaletteShow:
            commandPalette.show();
            return true;
        case CommandId::GotoLineShow:
            gotoLineBar.show();
            return true;
        case CommandId::OutlineToggle: {
            EditorSession& session = workspace.active();
            if (outlinePane.isVisible()) {
                outlinePane.hide();
                syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
            } else {
                refreshOutlinePane(session, outlinePane);
                syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
                syncFoldingState(hwnd, renderPipeline, session.folding());
            }
            return true;
        }
        case CommandId::JsonTreeToggle: {
            EditorSession& session = workspace.active();
            if (jsonTreePane.isVisible()) {
                jsonTreePane.hide();
                jsonTreePanePendingSessionToken = nullptr;
                syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
            } else {
                refreshStructureTreePane(hwnd, session, jsonTreePane, jsonTreeWorker, xmlTreeWorker, renderPipeline,
                                        jsonTreePanePendingSessionToken);
                syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
            }
            return true;
        }
        case CommandId::CsvGridToggle: {
            EditorSession& session = workspace.active();
            if (csvGridPane.isVisible()) {
                csvGridPane.hide();
                csvGridPanePendingSessionToken = nullptr;
            } else {
                refreshCsvGridPane(session, csvGridPane, csvModelWorker, csvGridPanePendingSessionToken);
            }
            return true;
        }
        // WI-21e: same "instant reflect + instant persist" shape the
        // command palette's own view.theme.* entries already established
        // (buildCommandRegistry()) - duplicated here rather than shared via
        // a helper, matching this codebase's own documented precedent for
        // small toggle bodies (see appendStructuralViewCommands()'s header
        // comment on why JsonTreeToggle/CsvGridToggle's palette and
        // dispatchWidgetShowCommand() bodies are likewise independent
        // copies). The active session's Viewport picks up the new word-wrap
        // state immediately here; any OTHER open tab's Viewport catches up
        // the next time it becomes active (handlePaintEvent()'s per-frame
        // sync, see that call site's own comment) rather than needing every
        // session eagerly touched now.
        case CommandId::WordWrapToggle: {
            settings.wordWrap = !settings.wordWrap;
            renderPipeline.setWordWrap(settings.wordWrap);
            workspace.active().viewport().setWordWrapEnabled(settings.wordWrap);
            if (settingsPath) {
                settings.saveTo(*settingsPath);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        case CommandId::LineNumbersToggle: {
            settings.showLineNumbers = !settings.showLineNumbers;
            renderPipeline.setLineNumbersVisible(settings.showLineNumbers);
            if (settingsPath) {
                settings.saveTo(*settingsPath);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        // Steps Dark -> Light -> HighContrast -> Dark - a flat top-level
        // menu has no submenu mechanism for a 3-way choice (see
        // command_ids.h's own ThemeCycle comment), so this reaches every
        // theme via repeated invocation instead. Reads the CURRENT theme
        // from settings.themeName (parseThemeKind()) rather than a
        // RenderPipeline getter - settings.themeName is already the single
        // source of truth every view.theme.* command keeps in lockstep with
        // the live renderer.
        case CommandId::ThemeCycle: {
            const ThemeKind current = neomifes::app::parseThemeKind(settings.themeName);
            const ThemeKind next    = neomifes::app::nextThemeKind(current);
            settings.themeName = std::u16string(neomifes::app::themeKindToSettingsString(next));
            renderPipeline.setTheme(next);
            if (settingsPath) {
                settings.saveTo(*settingsPath);
            }
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return true;
        }
        case CommandId::About: {
            std::wstring message = L"NeoMIFES ";
            for (const char c : neomifes::util::versionString()) {
                message.push_back(static_cast<wchar_t>(c));
            }
            ::MessageBoxW(hwnd, message.c_str(), L"バージョン情報", MB_OK | MB_ICONINFORMATION);
            return true;
        }
        default:
            return false;
    }
}

// WI-14b: LogIndexWorker's background-thread indexing completion signal
// receiver (wireNormalMode()'s cfg.onAppMessage kMsgLogIndexReady branch) -
// extracted into its own function purely to keep wireNormalMode() under the
// cognitive-complexity threshold (this file's established reason for
// extracting inline lambda bodies into named helpers). `wParam` is the
// opaque sessionToken requestIndex() was given back - in practice a
// *EditorSession, but this function never dereferences it directly; it
// only compares the raw pointer VALUE against &workspace.sessionAt(i) for
// each still-open tab (well-defined even if the tab that issued the
// request has since been closed - pointer equality comparison doesn't
// require either operand to point to a live object). No match (tab closed
// mid-flight) silently discards the result, same tolerance SyntaxWorker's
// own stale-response handling already has.
// WI-14c: gained hwnd/renderPipeline (previously just workspace/wParam/
// lParam, WI-14b) so a result landing for the CURRENTLY ACTIVE session can
// push its log visuals immediately - an inactive tab's result is still
// applied to its EditorSession (so it's ready whenever that tab becomes
// active) but doesn't touch RenderPipeline/repaint until
// syncViewForActiveSession() does that for it on the eventual tab switch.
void applyLogIndexReadyMessage(Workspace& workspace, RenderPipeline& renderPipeline, HWND hwnd, WPARAM wParam,
                               LPARAM lParam) {
    const std::unique_ptr<neomifes::logmode::LogModel> model(
        reinterpret_cast<neomifes::logmode::LogModel*>(lParam));
    const auto* const token = reinterpret_cast<const void*>(wParam);
    for (std::size_t i = 0; i < workspace.sessionCount(); ++i) {
        if (static_cast<const void*>(&workspace.sessionAt(i)) == token) {
            workspace.sessionAt(i).applyLogIndexResult(std::move(*model));
            if (i == workspace.activeIndex()) {
                pushLogVisualsForSession(renderPipeline, workspace.sessionAt(i));
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }
    }
}

// WI-17b: GitDiffWorker's background-thread diff completion signal receiver
// (wireNormalMode()'s cfg.onAppMessage kMsgGitDiffReady branch). `wParam` is
// the opaque sessionToken requestDiff() was given back - see
// applyLogIndexReadyMessage()'s own comment for why pointer-VALUE comparison
// against &workspace.sessionAt(i) is safe even if the issuing tab has since
// closed. `lParam` is always non-null (like JsonTreeWorker, unlike
// CsvModelWorker, GitDiffWorker posts even a std::nullopt result - see
// git_diff_worker.h's header comment) and owns a heap-allocated
// std::optional<std::vector<LineDiffRegion>>* that must be reconstructed
// into a unique_ptr immediately to avoid leaking it.
// WI-17c: gained hwnd/renderPipeline (previously just workspace/wParam/
// lParam, WI-17b - "no UI consumes gitDiff() yet" no longer holds), same
// enrichment applyLogIndexReadyMessage() got from WI-14c. The result is
// ALWAYS cached into its owning EditorSession regardless of whether it's the
// active tab (same as applyLogIndexReadyMessage()'s own unconditional
// apply) - only the gutter repaint is gated on "is this the active tab
// right now."
void applyGitDiffReadyMessage(Workspace& workspace, RenderPipeline& renderPipeline, HWND hwnd, WPARAM wParam,
                              LPARAM lParam) {
    const std::unique_ptr<std::optional<std::vector<neomifes::git::LineDiffRegion>>> result(
        reinterpret_cast<std::optional<std::vector<neomifes::git::LineDiffRegion>>*>(lParam));
    const auto* const token = reinterpret_cast<const void*>(wParam);
    for (std::size_t i = 0; i < workspace.sessionCount(); ++i) {
        if (static_cast<const void*>(&workspace.sessionAt(i)) == token) {
            workspace.sessionAt(i).applyGitDiffResult(std::move(*result));
            if (i == workspace.activeIndex()) {
                pushGitDiffVisualsForSession(renderPipeline, workspace.sessionAt(i));
                ::InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        }
    }
}

// WI-15b: JsonTreeWorker's background-thread indexing completion signal
// receiver (wireNormalMode()'s cfg.onAppMessage kMsgJsonTreeReady branch).
// `wParam` is the opaque sessionToken requestIndex() was given back - see
// applyLogIndexReadyMessage()'s own comment for why pointer-VALUE comparison
// against &workspace.sessionAt(i) is safe even if the issuing tab has since
// closed. `lParam` is always non-null (unlike LogIndexWorker, JsonTreeWorker
// posts even a std::nullopt result - see json_tree_worker.h's header
// comment) and owns a heap-allocated std::optional<JsonNode>* that must be
// reconstructed into a unique_ptr immediately to avoid leaking it.
// WI-15c: gained jsonTreePane/renderPipeline/hwnd/jsonTreePanePendingSessionToken
// (previously just workspace/wParam/lParam, WI-15b - "no UI consumes
// jsonTree() yet" no longer holds). The result is ALWAYS cached into its
// owning EditorSession regardless of the token match below (same as
// applyLogIndexReadyMessage()'s own tolerance for a since-closed tab) - the
// pending-token check only gates whether THIS delivery should also push
// visuals into jsonTreePane right now. Two conditions must both hold for
// that: `token` must still equal `jsonTreePanePendingSessionToken` (this is
// the specific toggle-on the pane is still waiting for - not a stale delivery
// after the user already closed the pane, see handleJsonTreeKey()/
// createAndPositionJsonTreePane()'s onClosed, both of which clear the token)
// AND the result's session must still be the ACTIVE tab (the user may have
// switched tabs while indexing was in flight - JsonTreePane, like
// OutlinePane, only ever reflects the active tab, see refreshJsonTreePane()'s
// own comment). The token is cleared unconditionally once matched, even if
// the second condition fails - a sessionToken is only ever delivered once
// per requestIndex() call, so there is nothing left to wait for either way.
void applyJsonTreeReadyMessage(Workspace& workspace, JsonTreePane& jsonTreePane, RenderPipeline& renderPipeline,
                               HWND hwnd, const void*& jsonTreePanePendingSessionToken, WPARAM wParam,
                               LPARAM lParam) {
    const std::unique_ptr<std::optional<neomifes::jsontree::JsonNode>> result(
        reinterpret_cast<std::optional<neomifes::jsontree::JsonNode>*>(lParam));
    const auto* const token = reinterpret_cast<const void*>(wParam);
    for (std::size_t i = 0; i < workspace.sessionCount(); ++i) {
        if (static_cast<const void*>(&workspace.sessionAt(i)) != token) {
            continue;
        }
        EditorSession& session = workspace.sessionAt(i);
        session.applyJsonTreeResult(std::move(*result));
        if (token == jsonTreePanePendingSessionToken) {
            jsonTreePanePendingSessionToken = nullptr;
            if (i == workspace.activeIndex() && session.jsonTree().has_value()) {
                const auto& tree = *session.jsonTree();
                jsonTreePane.showWith({neomifes::app::buildJsonTreeItems(tree)});
                session.folding().setFoldableRegions(neomifes::app::buildJsonFoldRegions(tree, session.document()));
                syncFoldingState(hwnd, renderPipeline, session.folding());
            }
        }
        break;
    }
}

// WI-16b: CsvModelWorker's background-thread indexing completion signal
// receiver (wireNormalMode()'s cfg.onAppMessage kMsgCsvIndexReady branch).
// `wParam` is the opaque sessionToken requestIndex() was given back - see
// applyLogIndexReadyMessage()'s own comment for why pointer-VALUE comparison
// against &workspace.sessionAt(i) is safe even if the issuing tab has since
// closed. `lParam` owns a heap-allocated CsvModel* that must be
// reconstructed into a unique_ptr immediately to avoid leaking it - always
// non-null here, since CsvModelWorker (unlike JsonTreeWorker) never posts on
// failure (see csv_model_worker.h's header comment).
// WI-16c: gained csvGridPane/csvGridPanePendingSessionToken (previously just
// workspace/wParam/lParam, WI-16b - "no UI consumes csvModel() yet" no
// longer holds). Same two-condition auto-populate contract
// applyJsonTreeReadyMessage() established: `token` must still equal
// csvGridPanePendingSessionToken (this specific toggle-on is still being
// waited for - not a stale delivery after the pane already closed, or after
// a tab switch/document swap, both of which clear the token via
// syncViewForActiveSession()/resetViewAfterDocumentSwap()) AND the result's
// session must still be the ACTIVE tab. The token is cleared unconditionally
// once matched, even if the second condition fails - a sessionToken is only
// ever delivered once per requestIndex() call. No folding-gutter integration
// unlike applyJsonTreeReadyMessage() - CsvGridPane has none.
// WI-16e: session.applyCsvIndexResult() itself now recomputes csvRowOrder()
// (using whatever csvFilter()/csvSort() the session already holds - a user
// CAN type into the filter edit while indexing is still in flight, since
// refreshCsvGridPane() shows it right away), so the display update below
// just reads session.csvRowOrder().size() rather than model.dataRowCount().
// WI-16f: broadened the refresh condition. Previously this only refreshed
// the grid on `token == csvGridPanePendingSessionToken` - the pane's very
// first population after being toggled on. A cell edit's own re-index
// (applyCsvCellEdit() -> beginCsvIndexing()) fires AFTER that token has
// already been consumed (nullptr), so without this change the grid would
// silently keep showing the pre-edit cell text even though
// session.applyCsvIndexResult() above already updated the session's own
// state correctly - the user would see no visual change until closing and
// reopening the pane. Now also refreshes whenever the pane is ALREADY
// VISIBLE for the active session, using setRowCount() (not showWith()) for
// that live-refresh case - columns/labels are unchanged by a cell edit
// (escapeCsvCellText() always quotes a value containing the delimiter, so
// an edit can never inject a new column boundary), and setRowCount()
// preserves any column widths the user has drag-resized. LVM_SETITEMCOUNT
// (setRowCount()'s underlying call) invalidates the visible item area
// unconditionally, regardless of whether the count actually changed, so
// this is also enough to make already-painted rows re-query
// LVN_GETDISPINFOW for their new text - no separate forced-redraw call is
// needed.
void applyCsvIndexReadyMessage(Workspace& workspace, CsvGridPane& csvGridPane,
                               const void*& csvGridPanePendingSessionToken, WPARAM wParam, LPARAM lParam) {
    const std::unique_ptr<neomifes::csvmode::CsvModel> result(
        reinterpret_cast<neomifes::csvmode::CsvModel*>(lParam));
    const auto* const token = reinterpret_cast<const void*>(wParam);
    for (std::size_t i = 0; i < workspace.sessionCount(); ++i) {
        if (static_cast<const void*>(&workspace.sessionAt(i)) != token) {
            continue;
        }
        EditorSession& session = workspace.sessionAt(i);
        session.applyCsvIndexResult(std::move(*result));
        const bool isPendingToggle = (token == csvGridPanePendingSessionToken);
        if (isPendingToggle) {
            csvGridPanePendingSessionToken = nullptr;
        }
        if (i == workspace.activeIndex() && session.csvModel().has_value() &&
            (isPendingToggle || csvGridPane.isVisible())) {
            if (isPendingToggle) {
                const auto& model = *session.csvModel();
                csvGridPane.showWith(
                    neomifes::app::buildCsvGridColumnLabels(model, session.document(), session.csvSort()),
                    session.csvRowOrder().size());
            } else {
                csvGridPane.setRowCount(session.csvRowOrder().size());
            }
        }
        break;
    }
}

// WI-15g/h: XmlTreeWorker's background-thread indexing completion signal
// receiver (wireNormalMode()'s cfg.onAppMessage kMsgXmlTreeReady branch) -
// mirrors applyJsonTreeReadyMessage() above at its WI-15c-era shape
// (jsonTreePane/renderPipeline/hwnd/jsonTreePanePendingSessionToken added in
// WI-15h - the WI-15g-era shape was plain workspace/wParam/lParam, "no UI
// consumes xmlTree() yet"). `lParam` is always non-null (XmlTreeWorker
// always posts - see xml_tree_worker.h's header comment) and owns a heap-
// allocated XmlTree* (not std::optional<XmlTree>*, since parseXmlTree()
// itself never returns std::optional - see xml_tree.h) that must be
// reconstructed into a unique_ptr immediately to avoid leaking it.
// jsonTreePanePendingSessionToken is shared with applyJsonTreeReadyMessage()
// (see refreshStructureTreePane()'s own comment on why this is safe: a
// session's language() is fixed at toggle-time, so exactly one of
// kMsgJsonTreeReady/kMsgXmlTreeReady can ever satisfy a given token).
void applyXmlTreeReadyMessage(Workspace& workspace, JsonTreePane& jsonTreePane, RenderPipeline& renderPipeline,
                              HWND hwnd, const void*& jsonTreePanePendingSessionToken, WPARAM wParam,
                              LPARAM lParam) {
    const std::unique_ptr<neomifes::xmltree::XmlTree> result(
        reinterpret_cast<neomifes::xmltree::XmlTree*>(lParam));
    const auto* const token = reinterpret_cast<const void*>(wParam);
    for (std::size_t i = 0; i < workspace.sessionCount(); ++i) {
        if (static_cast<const void*>(&workspace.sessionAt(i)) != token) {
            continue;
        }
        EditorSession& session = workspace.sessionAt(i);
        session.applyXmlTreeResult(std::move(*result));
        if (token == jsonTreePanePendingSessionToken) {
            jsonTreePanePendingSessionToken = nullptr;
            if (i == workspace.activeIndex() && session.xmlTree().has_value()) {
                const auto& tree = *session.xmlTree();
                jsonTreePane.showWith({neomifes::app::buildXmlTreeItems(tree.root)});
                session.folding().setFoldableRegions(
                    neomifes::app::buildXmlFoldRegions(tree.root, session.document()));
                syncFoldingState(hwnd, renderPipeline, session.folding());
            }
        }
        break;
    }
}

// WI-17e: GitStatusWorker's background-thread status-scan completion signal
// receiver (wireNormalMode()'s cfg.onAppMessage kMsgGitStatusReady branch).
// Unlike applyGitDiffReadyMessage()/applyJsonTreeReadyMessage() above, there
// is no per-session loop here and no "...PendingSessionToken" gate to
// consult - Workspace::gitStatus() is Workspace-wide state (see that
// method's own comment for why), and `this` (the ONE Workspace instance the
// whole window has) is the only sessionToken beginGitStatusIndexing() ever
// hands GitStatusWorker::requestStatus(), so the token match below can never
// be ambiguous the way a per-EditorSession token comparison guards against.
// `lParam` is always non-null (GitStatusWorker always posts, even a
// std::nullopt result - see git_status_worker.h's header comment) and owns a
// heap-allocated std::optional<std::vector<GitStatusEntry>>* that must be
// reconstructed into a unique_ptr immediately to avoid leaking it. The
// result is applied to Workspace unconditionally; gitPane's own displayed
// content only refreshes if the pane is CURRENTLY visible - matching
// git_pane.h's own "no live update, toggle off/on is the manual refresh"
// contract (this WI's own plan §6) rather than the pending-token "was this
// delivery specifically awaited" gate JsonTreePane/CsvGridPane use.
void applyGitStatusReadyMessage(Workspace& workspace, GitPane& gitPane, WPARAM wParam, LPARAM lParam) {
    const std::unique_ptr<std::optional<std::vector<neomifes::git::GitStatusEntry>>> result(
        reinterpret_cast<std::optional<std::vector<neomifes::git::GitStatusEntry>>*>(lParam));
    const auto* const token = reinterpret_cast<const void*>(wParam);
    if (token != static_cast<const void*>(&workspace)) {
        return;
    }
    workspace.applyGitStatusResult(std::move(*result));
    if (gitPane.isVisible()) {
        gitPane.showWith(neomifes::app::buildGitPaneItems(workspace.gitStatus()));
    }
}

// MainWindowConfig::onAppMessage's body (WM_APP+N messages MainWindow
// forwards unexamined - neomifes::ui never learns what
// kMsgSyntaxTokensReady/kMsgLogIndexReady/kMsgJsonTreeReady/kMsgCsvIndexReady/
// kMsgXmlTreeReady mean, see that field's own doc comment; this file is the
// layer that already depends on render::/logmode::/jsontree::/csvmode::/
// xmltree:: so it's the only place that can safely compare against the
// constants and reconstruct each payload's real type).
// Extracted out of wireNormalMode() into its own function (WI-14b) purely to
// keep wireNormalMode() under the cognitive-complexity threshold - the
// message branches below were previously inline in wireNormalMode()'s own
// cfg.onAppMessage lambda.
void handleAppMessage(RenderPipeline& renderPipeline, Workspace& workspace, JsonTreePane& jsonTreePane,
                      const void*& jsonTreePanePendingSessionToken, CsvGridPane& csvGridPane,
                      const void*& csvGridPanePendingSessionToken, GitPane& gitPane, HWND hwnd, UINT msg,
                      WPARAM wParam, LPARAM lParam) {
    if (msg == neomifes::git::kMsgGitDiffReady) {
        applyGitDiffReadyMessage(workspace, renderPipeline, hwnd, wParam, lParam);
        return;
    }
    if (msg == kMsgGitStatusReady) {
        applyGitStatusReadyMessage(workspace, gitPane, wParam, lParam);
        return;
    }
    if (msg == neomifes::render::kMsgSyntaxTokensReady) {
        const std::unique_ptr<std::vector<neomifes::syntax::Token>> tokens(
            reinterpret_cast<std::vector<neomifes::syntax::Token>*>(lParam));
        renderPipeline.applyAsyncSyntaxTokens(std::move(*tokens));
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    if (msg == neomifes::logmode::kMsgLogIndexReady) {
        applyLogIndexReadyMessage(workspace, renderPipeline, hwnd, wParam, lParam);
        return;
    }
    if (msg == kMsgJsonTreeReady) {
        applyJsonTreeReadyMessage(workspace, jsonTreePane, renderPipeline, hwnd, jsonTreePanePendingSessionToken,
                                  wParam, lParam);
        return;
    }
    if (msg == kMsgCsvIndexReady) {
        applyCsvIndexReadyMessage(workspace, csvGridPane, csvGridPanePendingSessionToken, wParam, lParam);
        return;
    }
    if (msg == kMsgXmlTreeReady) {
        applyXmlTreeReadyMessage(workspace, jsonTreePane, renderPipeline, hwnd, jsonTreePanePendingSessionToken,
                                 wParam, lParam);
    }
}

// wireNormalMode()'s cfg.onPaint body, pulled all the way out (not just the
// branching) for the same "a lambda defined inline inside wireNormalMode has
// its own body counted toward wireNormalMode's cognitive complexity" reason
// handleKeyDownEvent()/dispatchMouseDown() were already extracted for (see
// this file's own precedent for that phrasing). WI-16f: skips the Direct2D
// document render entirely while CsvGridPane covers the client area - this
// codebase's only "full client-area replacement" pane (OutlinePane/
// JsonTreePane are side-strips that deliberately coexist with the document
// view, so this guard would be wrong for them). Rendering was pointless work
// AND its output was bleeding through the few-DIP gaps CsvGridPane's filter
// row deliberately leaves around its own child controls (WM_PAINT paints
// the whole client area regardless of what native child HWNDs sit on top of
// it) - a persistent visual glitch a real user found during WI-16f
// dogfooding, present since WI-16c (2026-08-19) but never diagnosed until
// window-rect inspection (GetWindowRect on every CsvGridPane child) ruled
// out a layout/position bug and pointed here instead.
void handlePaintEvent(HWND paintHwnd, MainWindow& window, RenderPipeline& renderPipeline, Workspace& workspace,
                      TabBar& tabBar, StatusBar& statusBar, const CsvGridPane& csvGridPane) {
    if (!csvGridPane.isVisible()) {
        const auto rendered = renderPipeline.render();
        if (!rendered) {
            debugLogRenderError("RenderPipeline::render", rendered.error());
            return;
        }
        updateImeCandidatePosition(window, renderPipeline);
    }
    EditorSession& session = workspace.active();
    // WI-03: kept fresh every successful frame rather than only on
    // WM_SIZE - m_charWidthDips (which visibleColumnCount() depends
    // on) isn't measured until the FIRST render() call completes, so
    // relying on resize() alone would leave this at 0 until the user
    // manually resized the window. See syncHorizontalScrollBar()'s
    // comment for why the scrollbar sync itself belongs here too.
    session.viewport().setVisibleColumnCount(renderPipeline.visibleColumnCount());
    // WI-21e: kept fresh every frame, same rationale as
    // setVisibleColumnCount() immediately above - renderPipeline's word-wrap
    // flag is a single global toggle, but core::Viewport is one-per-
    // EditorSession, so whichever session just became active (tab switch,
    // Ctrl+O/Ctrl+N, or simply the very first frame after startup) needs its
    // OWN Viewport to learn the current global state without requiring every
    // session-creation/tab-switch call site to push it individually.
    session.viewport().setWordWrapEnabled(renderPipeline.wordWrapEnabled());
    syncHorizontalScrollBar(paintHwnd, renderPipeline, session.viewport());
    // WI-05 step 3: rebuilt from Workspace's actual session list every
    // frame (not just on tab-count changes) - the simplest way to keep
    // the ● unsaved-changes marker and each tab's label in sync with
    // EditorSession::isDirty()/path() without a separate "did
    // anything tab-relevant change" tracking mechanism. WM_PAINT is
    // already OS-coalesced, so no additional throttling is needed.
    tabBar.setTabs(buildTabBarItems(workspace), workspace.activeIndex());
    // WI-07 step4: same "rebuild every frame, no dirty-check guard"
    // convention as tabBar.setTabs() above.
    statusBar.setParts(buildStatusBarParts(session));
    // text_surface_no_screen_reader_exposure.md's minimal live-region tier:
    // recomputes the cursor's current line every frame (same "cheap, no
    // dirty-check guard here" shape as buildStatusBarParts() just above,
    // which computes an equivalent line number internally but doesn't
    // expose it back to this caller) and hands it to MainWindow, which owns
    // the actual "did this change since last frame" decision and the
    // resulting screen-reader announcement.
    {
        const Document&                          document  = session.document();
        const neomifes::document::TextPos        cursorPos = session.selection().primaryCursor().position;
        const neomifes::document::LineNumber     line      = document.offsetToLine(cursorPos);
        window.announceCurrentLineIfChanged(&session, line,
                                            neomifes::util::toWstringView(document.lineText(line)));
    }
    // WI-07 step8: same "rebuild every frame, no dirty-check guard"
    // convention - MainWindow::setTitle()'s own doc comment explains
    // why no diffing against the previous title is needed here.
    window.setTitle(buildWindowTitle(session));
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

// dispatchCommand() (WI-07 step2, command_dispatch.h) - defined here, not
// command_dispatch.cpp, because every case below reuses this file's own
// private helpers (dispatchSaveCommand()/dispatchOpenCommand()/... above,
// and transitively performSave()/confirmDiscardIfDirty()/
// syncViewForActiveSession()/resetViewAfterDocumentSwap()/
// syncRenderStateAndInvalidate(), all internal-linkage functions inside the
// anonymous namespace above) rather than duplicating their logic - see
// command_dispatch.h's own top comment for the full rationale. Placed after
// the anonymous namespace closes (same as debugLogRenderError() above) so
// it has the external linkage its command_dispatch.h declaration requires,
// while unqualified calls to those internal-linkage helpers still resolve
// correctly - C++ finds anonymous-namespace members via ordinary unqualified
// lookup from anywhere later in the same translation unit.
//
// This function itself is just a switch mapping each CommandId to one of the
// dispatchXxxCommand() helpers defined above (also moved verbatim from this
// file's former handleSaveKey()/handleOpenKey()/handleNewDocumentKey()/
// handleTabSwitchKey()/handleTabCloseKey()/handleClipboardKey(), all removed
// - see handleKeyDownEvent()'s own comment for what replaced their call
// sites - and editor_input.cpp::handleKeyDown()'s former Ctrl+Z/Ctrl+Y
// branches, also removed) - kept as small calls-only cases so
// dispatchCommand() itself stays under clang-tidy's cognitive-complexity
// threshold (a single flat switch with every case's full body inline
// measured well over it). ctx.workspace.active() is resolved fresh at the
// top - same "never cache a stale EditorSession&" rule this file's other
// Workspace&-taking functions already follow (see wireNormalMode()'s header
// comment).
void dispatchCommand(CommandId id, const CommandDispatchContext& ctx) {
    EditorSession& session = ctx.workspace.active();
    // WI-17f: Save/Undo/Redo/Cut/Paste/etc. all reach here via WM_COMMAND
    // (accelerator table or menu click), a completely separate chokepoint
    // from handleKeyDownEvent()'s own Diff-view guard - see that function's
    // own comment. Closing the Diff view first (rather than silently
    // swallowing the command, or leaving it open) means a real action
    // always "just works": the view steps aside and the command proceeds
    // normally afterward, the same as any other command reaching this
    // single choke point.
    if (ctx.renderPipeline.isDiffViewActive()) {
        syncViewForActiveSession(ctx.hwnd, ctx.renderPipeline, session, ctx.findDialog, ctx.csvGridPane,
                                 ctx.csvGridPanePendingSessionToken);
    }
    switch (id) {
        case CommandId::Save:
            dispatchSaveCommand(/*forceSaveAs=*/false, ctx, session);
            return;
        case CommandId::SaveAs:
            dispatchSaveCommand(/*forceSaveAs=*/true, ctx, session);
            return;
        case CommandId::Open:
            dispatchOpenCommand(ctx);
            return;
        case CommandId::New:
            dispatchNewCommand(ctx);
            return;
        case CommandId::NewWindow:
            static_cast<void>(ctx.sessionManager.createWindow(std::nullopt));
            return;
        case CommandId::TabNext:
        case CommandId::TabPrevious:
        case CommandId::TabSwitch1:
        case CommandId::TabSwitch2:
        case CommandId::TabSwitch3:
        case CommandId::TabSwitch4:
        case CommandId::TabSwitch5:
        case CommandId::TabSwitch6:
        case CommandId::TabSwitch7:
        case CommandId::TabSwitch8:
        case CommandId::TabSwitch9:
            dispatchTabSwitchCommand(id, ctx);
            return;
        case CommandId::TabClose:
            dispatchTabCloseCommand(ctx, session);
            return;
        case CommandId::TabCloseOthers:
            dispatchTabCloseOthersCommand(ctx);
            return;
        case CommandId::TabCloseAll:
            dispatchTabCloseAllCommand(ctx);
            return;
        // WI-18a: File menu-only "終了" - just requests the same WM_CLOSE
        // path Alt+F4/the title bar close button already trigger
        // (MainWindow::handleClose() runs the usual unsaved-changes prompt
        // for whichever session is active at the time WM_CLOSE is actually
        // processed - see that method's own comment). No session-specific
        // work belongs here.
        case CommandId::Exit:
            ::PostMessageW(ctx.hwnd, WM_CLOSE, 0, 0);
            return;
        case CommandId::Copy:
            dispatchCopyCommand(ctx, session);
            return;
        case CommandId::Cut:
            dispatchCutCommand(ctx, session);
            return;
        case CommandId::Paste:
            dispatchPasteCommand(ctx, session);
            return;
        case CommandId::Undo:
        case CommandId::Redo:
            dispatchUndoRedoCommand(id, ctx, session);
            return;
        case CommandId::ToggleOverwriteMode:
            // No selection/viewport/document change - just flips the flag
            // handleCharEvent()/buildStatusBarParts() both read. A plain
            // InvalidateRect() (not syncRenderStateAndInvalidate()) is
            // enough since nothing RenderPipeline caches needs resyncing,
            // same reasoning as dispatchSaveCommand()'s post-save repaint.
            session.overwriteMode() = !session.overwriteMode();
            ::InvalidateRect(ctx.hwnd, nullptr, FALSE);
            return;
        case CommandId::None:
        default:
            // Find/Grep/CommandPaletteShow/Outline/GotoLine/Bookmark*/
            // TagJump are deliberately NOT handled here - see this
            // function's own header comment and command_dispatch.h's top
            // comment for why. They remain on handleKeyDownEvent()'s
            // existing handle*Key() dispatch chain, unchanged.
            return;
    }
}

// Real launches only - deferred so it never affects firstPaintNs timing
// (ADR-009). If attach() fails, the window simply keeps the GDI placeholder
// forever; there is no retry policy. Same non-fatal treatment for
// findDialog.create() (originally findBar.create(), Phase 5b3a; replaced by
// ui::FindDialog in WI-24) - a Find dialog that fails to create simply
// isn't available this session, no retry policy either. WI-04: takes
// EditorSession& instead of the ~15 separate refs it used to (document/
// dispatcher/selection/viewport/altCursorAnchor/rectangularAnchor/
// bookmarks/foldingModel/freeCursorVirtualColumns/findReplaceState); the
// remaining individual parameters (findDialog/commandPalette/gotoLineBar/
// grepBar/grepState/searchHistory/outlinePane/freeCursorModeEnabled/
// isDraggingMinimap) are Workspace-wide or process-wide state that stays
// outside any one EditorSession - see this file's EditorSession
// member-placement notes.
// WI-05 step 1: takes Workspace& instead of EditorSession& - see this
// declaration's own header comment (normal_mode_wiring.h) for why. Every
// cfg.* lambda below that touches session state now captures &workspace
// (not &session) and resolves EditorSession& session = workspace.active()
// as its own first statement, since MainWindowConfig's callbacks are
// stored once (MainWindow::create() copies each std::function into its own
// members - see main_window.h) and then invoked repeatedly across the
// window's entire lifetime, potentially long after a tab switch has moved
// workspace.active() to a different EditorSession. cfg.onResize/onCommand/
// onNotify/onAppMessage never reference session state at all and are left
// completely unchanged.
void wireNormalMode(MainWindowConfig& cfg, MainWindow& window, RenderPipeline& renderPipeline,
                    Workspace& workspace, HINSTANCE hInstance, FindDialog& findDialog,
                    FindReplaceDialog& findReplaceDialog,
                    CommandPalette& commandPalette, GotoLineBar& gotoLineBar, JsonPathBar& jsonPathBar,
                    GrepBar& grepBar,
                    GrepState& grepState, SearchHistory& searchHistory, OutlinePane& outlinePane,
                    TabBar& tabBar, StatusBar& statusBar, core::Settings& settings,
                    const std::optional<std::filesystem::path>& settingsPath, core::KeyBindings& keyBindings,
                    const std::optional<std::filesystem::path>& keyBindingsPath,
                    platform::AcceleratorTableHandle& accelTable, bool& freeCursorModeEnabled,
                    bool& isDraggingMinimap, bool& imeComposing, core::RecentFiles& recentFiles,
                    MenuBarHandles menuHandles, AutosaveContext& autosave,
                    std::optional<logmode::LogIndexWorker>& logIndexWorker,
                    std::vector<logmode::LogPatternRule>& userLogPatterns,
                    const std::optional<std::filesystem::path>& logPatternsDir,
                    std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker,
                    std::optional<csvmode::CsvModelWorker>& csvModelWorker, JsonTreePane& jsonTreePane,
                    const void*& jsonTreePanePendingSessionToken, CsvGridPane& csvGridPane,
                    const void*& csvGridPanePendingSessionToken,
                    std::optional<neomifes::git::GitDiffWorker>& gitDiffWorker,
                    std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker, bool& jsonPathBarIsForXml,
                    GitPane& gitPane, std::optional<GitStatusWorker>& gitStatusWorker,
                    std::optional<document::Document>& diffViewDocument, SessionManager& sessionManager) {
    // WI-05: a plain statement here (not inside any lambda) - this value
    // never changes again for the process's lifetime (see
    // setTabBarHeightDips()'s own comment), so there is no reason to defer
    // it to onDeferredInit/onResize the way genuinely per-frame state is.
    renderPipeline.setTabBarHeightDips(TabBar::heightDips());
    // WI-07 step4: same reasoning, bottom-edge counterpart.
    renderPipeline.setStatusBarHeightDips(StatusBar::heightDips());
    // WI-11: unlike WI-07 step3's original design, buildMenuBar() is now
    // called by the CALLER (main.cpp), before this function - it needs
    // recentFiles (buildMenuBar()'s own new WI-11 parameter) and its result
    // (`menuHandles`) must be assigned to cfg.menuBar BEFORE window.create()
    // below (CreateWindowExW's hMenu is fixed at window creation), which is
    // also before wireNormalMode() itself runs. `menuHandles` arrives here
    // purely so refreshRecentFilesMenu() call sites below (opening/saving a
    // file) can reuse the SAME HMENU pair - see this file's own callers of
    // that function.
    cfg.onDeferredInit = [&window, &renderPipeline, &workspace, hInstance, &findDialog, &findReplaceDialog,
                          &commandPalette,
                          &gotoLineBar, &jsonPathBar, &grepBar, &grepState, &searchHistory, &outlinePane, &tabBar,
                          &statusBar, &settings, &settingsPath, &keyBindings, keyBindingsPath, &accelTable,
                          &freeCursorModeEnabled, &recentFiles, menuHandles, &autosave, &logIndexWorker,
                          &userLogPatterns, logPatternsDir, &jsonTreeWorker, &csvModelWorker, &jsonTreePane,
                          &jsonTreePanePendingSessionToken, &csvGridPane,
                          &csvGridPanePendingSessionToken, &gitDiffWorker, &xmlTreeWorker,
                          &jsonPathBarIsForXml, &gitPane, &gitStatusWorker, &diffViewDocument,
                          &sessionManager](HWND hwnd) {
        const auto attached = renderPipeline.attach(hwnd);
        if (!attached) {
            debugLogRenderError("RenderPipeline::attach", attached.error());
            return;
        }
        // WI-14b: LogIndexWorker's constructor requires a real HWND and
        // starts its background thread immediately (no default-construct-
        // then-attach() shape like RenderPipeline above) - onDeferredInit is
        // this codebase's existing place for that kind of HWND-dependent
        // initialization (same timing findDialog.create(hwnd, ...)/
        // outlinePane's CreateWindowExW below already rely on).
        logIndexWorker.emplace(hwnd);
        // WI-15b: same reasoning as logIndexWorker.emplace() above -
        // JsonTreeWorker's constructor also requires a real HWND and starts
        // its own background thread immediately.
        jsonTreeWorker.emplace(hwnd);
        // WI-16b: same reasoning as jsonTreeWorker.emplace() above -
        // CsvModelWorker's constructor also requires a real HWND and starts
        // its own background thread immediately.
        csvModelWorker.emplace(hwnd);
        // WI-17b: same reasoning as jsonTreeWorker.emplace() above -
        // GitDiffWorker's constructor also requires a real HWND and starts
        // its own background thread immediately.
        gitDiffWorker.emplace(hwnd);
        // WI-15g: same reasoning as jsonTreeWorker.emplace() above -
        // XmlTreeWorker's constructor also requires a real HWND and starts
        // its own background thread immediately.
        xmlTreeWorker.emplace(hwnd);
        // WI-17e: same reasoning as jsonTreeWorker.emplace() above -
        // GitStatusWorker's constructor also requires a real HWND and starts
        // its own background thread immediately.
        gitStatusWorker.emplace(hwnd);
        // Resolved once here for this lambda's own synchronous body below -
        // safe (nothing can switch tabs before the window's first deferred
        // init has even run). The nested paint handler lambda just below
        // captures &workspace instead, since IT fires on every WM_PAINT
        // across the window's whole lifetime and must re-resolve fresh each
        // time (see this function's own header comment).
        EditorSession& session = workspace.active();
        renderPipeline.setDocument(&session.document());
        window.setPaintHandler([&window, &renderPipeline, &workspace, &tabBar, &statusBar,
                                &csvGridPane](HWND paintHwnd) {
            handlePaintEvent(paintHwnd, window, renderPipeline, workspace, tabBar, statusBar, csvGridPane);
        });
        const FindDialogConfig findDialogConfig =
            buildFindDialogConfig(hwnd, workspace, renderPipeline, findDialog, searchHistory, findReplaceDialog);
        [[maybe_unused]] const bool findDialogCreated = findDialog.create(hwnd, hInstance, findDialogConfig);

        // WI-18b: same non-fatal treatment as findDialog.create() above - a
        // Find/Replace dialog that fails to create simply isn't available
        // this session (CommandId::FindReplace's dispatch already no-ops
        // safely if findReplaceDialog was never successfully created, same
        // "check before use" convention every other optional overlay here
        // follows).
        const FindReplaceDialogConfig findReplaceDialogConfig =
            buildFindReplaceDialogConfig(hwnd, workspace, renderPipeline, findReplaceDialog);
        [[maybe_unused]] const bool findReplaceDialogCreated =
            findReplaceDialog.create(hwnd, hInstance, findReplaceDialogConfig);

        // Same non-fatal treatment as findDialog.create() above - a palette
        // that fails to create simply isn't available this session.
        CommandPaletteConfig commandPaletteConfig{};
        commandPaletteConfig.onClosed = [hwnd]() { ::SetFocus(hwnd); };
        auto commands = buildCommandRegistry(hwnd, findDialog, findReplaceDialog, workspace, renderPipeline, settings,
                                             settingsPath, keyBindings, keyBindingsPath, accelTable,
                                             freeCursorModeEnabled, commandPalette, recentFiles, menuHandles,
                                             autosave, logIndexWorker,
                                             userLogPatterns, logPatternsDir, jsonTreePane, outlinePane,
                                             jsonTreeWorker, xmlTreeWorker, jsonTreePanePendingSessionToken,
                                             csvGridPane, csvModelWorker, csvGridPanePendingSessionToken,
                                             jsonPathBar, jsonPathBarIsForXml, *gitDiffWorker, gitPane,
                                             *gitStatusWorker, diffViewDocument, sessionManager);
        [[maybe_unused]] const bool commandPaletteCreated =
            commandPalette.create(hwnd, hInstance, commandPaletteConfig, std::move(commands));

        // Same non-fatal treatment as findDialog.create() above.
        const GotoLineBarConfig gotoLineBarConfig =
            buildGotoLineBarConfig(hwnd, workspace, renderPipeline, gotoLineBar);
        [[maybe_unused]] const bool gotoLineBarCreated =
            gotoLineBar.create(hwnd, hInstance, gotoLineBarConfig);

        // WI-15e: same non-fatal treatment as findDialog.create() above.
        const JsonPathBarConfig jsonPathBarConfig =
            buildJsonPathBarConfig(hwnd, workspace, renderPipeline, jsonPathBar, jsonPathBarIsForXml);
        [[maybe_unused]] const bool jsonPathBarCreated =
            jsonPathBar.create(hwnd, hInstance, jsonPathBarConfig);

        // Same non-fatal treatment as findDialog.create() above.
        const GrepBarConfig grepBarConfig = buildGrepBarConfig(hwnd, workspace, renderPipeline, findDialog,
                                                               grepBar, grepState, searchHistory, recentFiles,
                                                               menuHandles, csvGridPane,
                                                               csvGridPanePendingSessionToken);
        [[maybe_unused]] const bool grepBarCreated = grepBar.create(hwnd, hInstance, grepBarConfig);

        // Same non-fatal treatment as findDialog.create() above.
        createAndPositionOutlinePane(hwnd, hInstance, workspace, renderPipeline, outlinePane);
        // WI-15c: same non-fatal treatment as createAndPositionOutlinePane()
        // above.
        createAndPositionJsonTreePane(hwnd, hInstance, workspace, renderPipeline, jsonTreePane,
                                      jsonTreePanePendingSessionToken);
        // WI-16c: same non-fatal treatment as createAndPositionOutlinePane()
        // above.
        createAndPositionCsvGridPane(hwnd, hInstance, workspace, renderPipeline, csvGridPane,
                                     csvGridPanePendingSessionToken, csvModelWorker);
        // WI-17e: same non-fatal treatment as createAndPositionOutlinePane()
        // above.
        createAndPositionGitPane(hwnd, hInstance, workspace, renderPipeline, findDialog, csvGridPane,
                                 csvGridPanePendingSessionToken, gitPane);
        // WI-05 step 2: same non-fatal treatment as findDialog.create() above -
        // a tab strip that fails to create simply isn't available this
        // session (the editor still works, just without visible tabs).
        createAndPositionTabBar(hwnd, hInstance, workspace, renderPipeline, findDialog, tabBar, csvGridPane,
                                csvGridPanePendingSessionToken);
        // WI-07 step4: same non-fatal treatment as findDialog.create() above -
        // a status bar that fails to create simply isn't available this
        // session (the editor still works, just without a bottom status
        // strip).
        createAndPositionStatusBar(hwnd, hInstance, workspace, statusBar);
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
        // WI-11: see startAutoSaveTimerIfConfigured()'s own comment (this
        // file, near autoSaveAllDirtySessions()) for why this is a plain
        // call rather than the conditional inlined here.
        startAutoSaveTimerIfConfigured(window, settings);
    };
    cfg.onResize = [&renderPipeline, &commandPalette, &gotoLineBar, &jsonPathBar, &grepBar,
                    &outlinePane,
                    &jsonTreePane, &gitPane, &csvGridPane, &tabBar,
                    &statusBar](HWND hwnd, std::uint32_t w, std::uint32_t h, float dpiScale) {
        if (renderPipeline.isAttached()) {
            const auto resized = renderPipeline.resize(w, h, dpiScale);
            if (!resized) {
                debugLogRenderError("RenderPipeline::resize", resized.error());
            }
            // WI-15i: re-applies whichever right-pane reservation was already
            // in effect - resize() itself doesn't touch m_rightPaneWidthDips,
            // but a fresh WM_SIZE is also a safe, low-cost point to
            // re-synchronize it in case it was ever missed at a toggle site
            // (defense in depth, not the primary sync path - see
            // syncRightPaneWidthDips()'s own comment for the toggle sites
            // that ARE the primary path).
            syncRightPaneWidthDips(hwnd, renderPipeline, outlinePane, jsonTreePane, gitPane);
        }
        // findDialog: deliberately NOT repositioned here - unlike the old
        // FindBar (a WS_CHILD docked to MainWindow's top-right corner),
        // ui::FindDialog (WI-24) is a genuine top-level owned window that
        // positions itself independently (centered over MainWindow on first
        // show(), then wherever the user leaves it) and never re-docks on
        // resize, matching ui::FindReplaceDialog's same precedent.
        commandPalette.onParentResized(w, dpiScale);
        gotoLineBar.onParentResized(w, dpiScale);
        jsonPathBar.onParentResized(w, dpiScale);
        grepBar.onParentResized(w, dpiScale);
        outlinePane.onParentResized(w, h, dpiScale);
        jsonTreePane.onParentResized(w, h, dpiScale);
        gitPane.onParentResized(w, h, dpiScale);
        csvGridPane.onParentResized(w, h, dpiScale, TabBar::heightDips(), StatusBar::heightDips());
        tabBar.onParentResized(w, dpiScale);
        statusBar.onParentResized(w, h, dpiScale);
    };
    cfg.onCommand = [&findDialog, &findReplaceDialog, &commandPalette, &grepBar, &gotoLineBar, &outlinePane,
                     &jsonTreePane, &gitPane,
                     &jsonTreeWorker, &xmlTreeWorker, &jsonTreePanePendingSessionToken, &csvGridPane,
                     &csvModelWorker, &csvGridPanePendingSessionToken, &workspace, &renderPipeline, &recentFiles,
                     menuHandles, &settings, settingsPath, &autosave, &gitDiffWorker,
                     &sessionManager](HWND hwnd, WPARAM wParam, LPARAM lParam) {
        // findDialog: deliberately NOT forwarded here - unlike the old
        // FindBar (a WS_CHILD whose EN_CHANGE notifications arrived at
        // MainWindow's own WM_COMMAND), ui::FindDialog (WI-24) is a
        // top-level window that routes its own WM_COMMAND internally
        // (inside its own wndProc), the same precedent
        // ui::FindReplaceDialog already established - see find_dialog.cpp's
        // handleCommand().
        commandPalette.handleCommand(wParam, lParam);
        grepBar.handleCommand(wParam, lParam);
        // WI-16e: EN_CHANGE from the filter edit routes here, same "child-
        // control notification, not a CommandId" shape as the 3 calls above.
        csvGridPane.handleCommand(wParam, lParam);
        // WI-07 step2/3: accelerator- or MENU-originated WM_COMMAND
        // (TranslateAcceleratorW/command_dispatch.h's buildAcceleratorTable(),
        // or a menu click on an item neomifes::app::buildMenuBar() built)
        // carries a CommandId in LOWORD(wParam) - distinct from every
        // child-control notification above (command_palette.cpp/grep_bar.cpp/
        // csv_grid_pane.cpp all use control ids 2001-4003, far below
        // CommandId's 40000+ range; FindDialog's own 9101-9107 never reaches
        // here at all - see this lambda's own comment on findDialog above
        // and command_ids.h's range-separation comment).
        const auto rawId = LOWORD(wParam);
        // WI-11: the "最近使ったファイル" submenu's dynamic id range
        // (8001-8020, menu_bar.h's kRecentFileIdBase/kMaxRecentFileMenuItems)
        // sits BELOW CommandId::FindShow's 40000+ range, so it must be
        // checked before the early-return threshold below would otherwise
        // discard it.
        const CommandDispatchContext ctx{.hwnd                           = hwnd,
                                         .workspace                      = workspace,
                                         .renderPipeline                 = renderPipeline,
                                         .findDialog                        = findDialog,
                                         .recentFiles                    = recentFiles,
                                         .menuHandles                    = menuHandles,
                                         .autosave                       = autosave,
                                         .settings                       = settings,
                                         .csvGridPane                    = csvGridPane,
                                         .csvGridPanePendingSessionToken = csvGridPanePendingSessionToken,
                                         .gitDiffWorker                  = *gitDiffWorker,
                                         .sessionManager                 = sessionManager};
        if (rawId >= neomifes::app::kRecentFileIdBase &&
            rawId < neomifes::app::kRecentFileIdBase + neomifes::app::kMaxRecentFileMenuItems) {
            dispatchRecentFileCommand(static_cast<std::size_t>(rawId - neomifes::app::kRecentFileIdBase), ctx);
            return;
        }
        const auto commandId = static_cast<CommandId>(rawId);
        if (commandId < CommandId::FindShow) {
            return;
        }
        // dispatchWidgetShowCommand() first (Find/Grep/CommandPalette/
        // Outline/GotoLine/About - the commands dispatchCommand() itself
        // deliberately does NOT handle, see that function's own comment);
        // falls through to dispatchCommand() for everything else.
        if (dispatchWidgetShowCommand(commandId, hwnd, workspace, renderPipeline, findDialog, findReplaceDialog,
                                      commandPalette, grepBar, gotoLineBar, outlinePane, jsonTreePane, gitPane,
                                      jsonTreeWorker,
                                      xmlTreeWorker, jsonTreePanePendingSessionToken, csvGridPane, csvModelWorker,
                                      csvGridPanePendingSessionToken, settings, settingsPath)) {
            return;
        }
        dispatchCommand(commandId, ctx);
    };
    // Phase 7g: OutlinePane's WC_TREEVIEW is this codebase's first control
    // that notifies via WM_NOTIFY rather than WM_COMMAND - see
    // MainWindowConfig::onNotify's doc comment (main_window.h). WI-05:
    // TabBar's WC_TABCONTROL notifies the same way - MainWindowConfig::
    // onNotify is a single std::function (not a chain), so both are called
    // unconditionally here, each independently checking "is this mine?" via
    // NMHDR::hwndFrom (see OutlinePane::handleNotify()/TabBar::handleNotify()'s
    // own comments for why neither TVN_SELCHANGEDW nor TCN_SELCHANGE require
    // a specific non-zero reply, so discarding one return value is safe).
    cfg.onNotify = [&outlinePane, &jsonTreePane, &gitPane, &csvGridPane, &tabBar,
                    &statusBar](HWND, WPARAM wParam, LPARAM lParam) {
        outlinePane.handleNotify(wParam, lParam);
        jsonTreePane.handleNotify(wParam, lParam);
        gitPane.handleNotify(wParam, lParam);
        csvGridPane.handleNotify(wParam, lParam);
        statusBar.handleNotify(wParam, lParam);
        return tabBar.handleNotify(wParam, lParam);
    };
    // Phase 7c/WI-14b: SyntaxWorker/LogIndexWorker background-thread
    // completion signals - see handleAppMessage()'s own doc comment above
    // for why the actual branching logic lives there, not in this lambda.
    cfg.onAppMessage = [&renderPipeline, &workspace, &jsonTreePane, &jsonTreePanePendingSessionToken, &csvGridPane,
                        &csvGridPanePendingSessionToken, &gitPane](HWND hwnd, UINT msg, WPARAM wParam,
                                                                    LPARAM lParam) {
        handleAppMessage(renderPipeline, workspace, jsonTreePane, jsonTreePanePendingSessionToken, csvGridPane,
                         csvGridPanePendingSessionToken, gitPane, hwnd, msg, wParam, lParam);
    };
    // WI-02: WM_CLOSE veto - goes through confirmDiscardIfDirty() so an
    // unsaved edit is never silently discarded by closing the window. WI-05
    // step 3: checks EVERY open tab, not just the active one - closing the
    // window discards every tab at once, so every one of them needs its own
    // chance to prompt Save/Don't Save/Cancel. confirmDiscardIfDirty()
    // itself is a no-op (returns true immediately) for a tab that isn't
    // dirty, so this loop costs nothing extra for the common case of a
    // single clean tab. Stops at the first Cancel (the whole close is
    // vetoed, remaining tabs are left exactly as they were).
    cfg.onClose = [&workspace, &settings, &recentFiles, menuHandles, &autosave](HWND hwnd) {
        for (std::size_t i = 0; i < workspace.sessionCount(); ++i) {
            if (!confirmDiscardIfDirty(hwnd, workspace.sessionAt(i), settings, recentFiles, menuHandles,
                                       autosave)) {
                return false;
            }
        }
        return true;
    };
    cfg.onDropFiles = [&workspace, &renderPipeline, &findDialog, &recentFiles, menuHandles, &csvGridPane,
                       &csvGridPanePendingSessionToken](HWND hwnd, const std::vector<std::wstring>& paths) {
        handleDropFilesEvent(hwnd, paths, workspace, renderPipeline, findDialog, recentFiles, menuHandles, csvGridPane,
                             csvGridPanePendingSessionToken);
    };
    // WI-11: fired every startAutoSaveTimer() interval (kAutoSaveTimerId is
    // the only timer this window ever starts, so no id comparison is
    // needed here - see MainWindowConfig::onTimer's own comment).
    cfg.onTimer = [&workspace, &autosave](HWND, UINT_PTR) { autoSaveAllDirtySessions(workspace, autosave); };
    // WI-11: fired on WM_KILLFOCUS - the user switching to another window
    // is exactly the kind of "about to walk away" moment autosave exists to
    // protect against, same rationale as the periodic timer above.
    cfg.onFocusLost = [&workspace, &autosave](HWND) { autoSaveAllDirtySessions(workspace, autosave); };
    // WI-07 step9/WI-18a: right-click context menu - see
    // handleContextMenuEvent()'s own comment above for the source-HWND
    // branching (tab strip / main window text content / everything else).
    cfg.onContextMenu = [&workspace, &renderPipeline, &findDialog, &tabBar, &recentFiles, menuHandles, &settings,
                        &autosave, &csvGridPane, &csvGridPanePendingSessionToken, &gitDiffWorker,
                        &sessionManager](HWND source, HWND hwnd, std::int32_t xScreen, std::int32_t yScreen) {
        handleContextMenuEvent(source, hwnd, xScreen, yScreen, workspace, renderPipeline, findDialog, tabBar,
                               recentFiles, menuHandles, settings, autosave, csvGridPane,
                               csvGridPanePendingSessionToken, *gitDiffWorker, sessionManager);
    };
    // WI-06: see wireImeHooks()'s own comment for why the 4 IME hooks were
    // pulled into a standalone function rather than assigned inline here
    // (same cognitive-complexity-budget reasoning as handleKeyDownEvent()/
    // handleHScrollEvent() above).
    wireImeHooks(cfg, workspace, renderPipeline, imeComposing);
    cfg.onKeyDown = [&workspace, &renderPipeline, &findDialog, &findReplaceDialog, &commandPalette, &gotoLineBar,
                     &grepBar, &outlinePane, &jsonTreePane, &gitPane, &jsonTreeWorker, &xmlTreeWorker,
                     &jsonTreePanePendingSessionToken,
                     &csvGridPane, &csvModelWorker, &csvGridPanePendingSessionToken, &freeCursorModeEnabled,
                     &imeComposing, &keyBindings, &recentFiles, menuHandles, &settings, &autosave, &gitDiffWorker,
                     &sessionManager](HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown) {
        handleKeyDownEvent(hwnd, vkCode, shiftDown, ctrlDown, workspace, renderPipeline, findDialog, findReplaceDialog,
                          commandPalette, gotoLineBar, grepBar, outlinePane, jsonTreePane, gitPane, jsonTreeWorker,
                          xmlTreeWorker, jsonTreePanePendingSessionToken, csvGridPane, csvModelWorker,
                          csvGridPanePendingSessionToken, freeCursorModeEnabled, imeComposing, keyBindings,
                          recentFiles, menuHandles, settings, autosave, *gitDiffWorker, sessionManager);
    };
    cfg.onSysKeyDown = [&workspace, &renderPipeline](HWND hwnd, UINT vkCode, bool shiftDown) {
        return handleSysKeyDownEvent(hwnd, vkCode, shiftDown, workspace.active(), renderPipeline);
    };
    cfg.onChar = [&workspace, &renderPipeline, &imeComposing](HWND hwnd, wchar_t ch) {
        handleCharEvent(hwnd, ch, workspace.active(), renderPipeline, imeComposing);
    };
    cfg.onMouseWheel = [&workspace, &renderPipeline](HWND hwnd, short wheelDelta) {
        EditorSession& session = workspace.active();
        session.viewport().scrollTo(neomifes::app::applyMouseWheelScroll(
            wheelDelta, session.viewport().topLine(), session.document().lineCount()));
        syncRenderStateAndInvalidate(hwnd, renderPipeline, session);
    };
    // WI-03: this window's first-ever scrollbar (WS_HSCROLL, added by
    // MainWindow::create() only because this handler is set - see
    // MainWindowConfig::onHScroll's comment). Body lives in
    // handleHScrollEvent() (not inline here) - see that function's comment.
    cfg.onHScroll = [&workspace, &renderPipeline](HWND hwnd, WORD scrollCode, WORD scrollPos) {
        handleHScrollEvent(hwnd, scrollCode, scrollPos, workspace.active(), renderPipeline);
    };
    cfg.onMouseDown = [&workspace, &renderPipeline, &isDraggingMinimap](HWND hwnd, std::int32_t x,
                                                                        std::int32_t y, bool shiftDown,
                                                                        bool altDown, int clickCount) {
        handleMouseDownEvent(hwnd, x, y, shiftDown, altDown, clickCount, workspace.active(), renderPipeline,
                             isDraggingMinimap);
    };
    cfg.onMouseDrag = [&workspace, &renderPipeline, &isDraggingMinimap](HWND hwnd, std::int32_t x,
                                                                        std::int32_t y) {
        handleMouseDragEvent(hwnd, x, y, workspace.active(), renderPipeline, isDraggingMinimap);
    };
}

}  // namespace neomifes::app
