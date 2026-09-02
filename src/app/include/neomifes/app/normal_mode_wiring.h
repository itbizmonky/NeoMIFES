#pragma once

// normal_mode_wiring - wires MainWindowConfig's callbacks for a real
// (non-measurement) launch: keybindings, mouse handling, Find/Grep/Goto/
// Outline/Command Palette overlays, save/open/new document lifecycle. WI-04
// step 3b: moved verbatim out of main.cpp (was `wireNormalMode()` +
// ~46 supporting functions, ~1780 lines) so wWinMain itself could shrink to
// the WI-04 DoD of <=500 lines - main.cpp now owns only wWinMain/window
// creation/the message loop/Workspace+RenderPipeline construction and the
// two measurement-mode wiring functions (wireMeasureStartupOrMemoryMode()/
// wireMeasureFrameMode(), small enough to stay). No behavior changed by
// this move - see this header's/its .cpp's individual function comments,
// carried over unchanged from main.cpp, for the actual design rationale.
//
// Depends on Win32 (HWND/HINSTANCE) and every UI widget/RenderPipeline -
// unlike neomifes::app_input (editor_input.h), this is NOT built as a
// separate headlessly-testable library; it is compiled straight into the
// NeoMIFES executable (see src/app/CMakeLists.txt), same placement as
// file_dialogs.cpp/message_dialogs.cpp.

#include <windows.h>

#include <filesystem>
#include <optional>
#include <vector>

#include "neomifes/app/autosave.h"
#include "neomifes/app/command_dispatch.h"
#include "neomifes/app/editor_session.h"
#include "neomifes/app/menu_bar.h"
#include "neomifes/app/workspace.h"
#include "neomifes/core/autosave_index.h"
#include "neomifes/core/key_bindings.h"
#include "neomifes/core/recent_files.h"
#include "neomifes/core/search_history.h"
#include "neomifes/core/settings.h"
#include "neomifes/csvmode/csv_model_worker.h"
#include "neomifes/git/git_diff_worker.h"
#include "neomifes/git/git_status_worker.h"
#include "neomifes/jsontree/json_tree_worker.h"
#include "neomifes/xmltree/xml_tree_worker.h"
#include "neomifes/logmode/log_index_worker.h"
#include "neomifes/logmode/log_pattern.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/render/render_error.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/search/grep_service.h"
#include "neomifes/ui/command_palette.h"
#include "neomifes/ui/find_bar.h"
#include "neomifes/ui/find_replace_dialog.h"
#include "neomifes/ui/goto_line_bar.h"
#include "neomifes/ui/grep_bar.h"
#include "neomifes/ui/csv_grid_pane.h"
#include "neomifes/ui/git_pane.h"
#include "neomifes/ui/json_path_bar.h"
#include "neomifes/ui/json_tree_pane.h"
#include "neomifes/ui/main_window.h"
#include "neomifes/ui/outline_pane.h"
#include "neomifes/ui/status_bar.h"
#include "neomifes/ui/tab_bar.h"

namespace neomifes::app {

// GrepBarConfig::onRunQuery (Phase 5c3) - builds a search::GrepQuery from
// GrepBar's raw text fields and runs it synchronously. See grep_bar.h's
// class comment for why this is Enter-triggered rather than live/debounced:
// a directory-wide Grep is far more expensive than Find bar's single-document
// incremental search, and this codebase has no async infrastructure to keep
// the UI responsive while one runs. WI-04: unchanged - GrepState is
// document-independent (a Workspace-wide search results pane, not part of
// any one EditorSession - see this file's EditorSession member-placement
// notes). Declared here (not .cpp-local) because wWinMain constructs one
// and passes it by reference into wireNormalMode() below.
struct GrepState {
    std::vector<search::GrepMatch> currentResults;
};

// No logging engine exists yet (basic_design.md sec.6.5 is a later phase);
// this is a deliberate, narrowly-scoped stopgap for render-attach/resize
// failures rather than solving logging prematurely. describe()'s output is
// documented ASCII-only, so OutputDebugStringA (not the W variant) is fine.
// WI-04: moved here from main.cpp (was file-local) since both
// wireNormalMode() below and main.cpp's own wireMeasureFrameMode() call it -
// a single shared definition avoids duplicating this across both files.
void debugLogRenderError(const char* what, const render::RenderError& err) noexcept;

// WI-07 step2: dispatchCommand() (declared in command_dispatch.h, defined in
// normal_mode_wiring.cpp) calls this file's own private helpers
// (syncRenderStateAndInvalidate()/resetViewAfterDocumentSwap()/
// syncViewForActiveSession()/performSave()/confirmDiscardIfDirty(), all
// anonymous-namespace-local) directly via ordinary unqualified lookup - it
// is defined later in the SAME translation unit, so C++ finds them without
// needing a second, redeclared-here entry point. Deliberately NOT declared
// in this header: doing so once created a real bug (MSVC error C2668,
// ambiguous overload) - the anonymous-namespace definition and a
// would-be external-linkage declaration of the identical signature are two
// DIFFERENT overloads from the compiler's perspective, and dispatchCommand()
// could see both.

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
//
// WI-05 step 1: takes Workspace& instead of EditorSession& - once
// Workspace can hold more than one session, every lambda this function (or
// the 5 buildXConfig()/createAndPositionOutlinePane() helpers it calls)
// STORES for later invocation (FindBarConfig/CommandDescriptor::action/
// GotoLineBarConfig/GrepBarConfig/OutlinePaneConfig callbacks, and
// wireNormalMode's own cfg.onKeyDown/onChar/onMouseWheel/onMouseDown/
// onMouseDrag/onHScroll/onSysKeyDown/onClose/onDropFiles/the paint handler)
// must resolve workspace.active() FRESH at invocation time rather than
// capture a single EditorSession& fixed at construction time - otherwise,
// after a tab switch, a stored callback (e.g. the Command Palette's "Undo")
// would silently keep operating on the tab that was active when
// wireNormalMode() ran, not the one the user is currently looking at. Only
// the ~15 functions that build/capture such stored closures actually
// needed this signature change; every other function in this file that is
// only ever called SYNCHRONOUSLY (by a caller that has already freshly
// resolved the correct session) keeps taking EditorSession& unchanged - a
// deliberately narrower, more precise realization of the same
// no-stale-tab-reference guarantee, not a blanket rename.
// WI-05 step 2: takes ui::TabBar& - created/positioned alongside outlinePane
// in cfg.onDeferredInit and populated with the (still, at this step, always
// single) tab derived from workspace's session list. No keybindings route
// through it yet; TCN_SELCHANGE is routed to a no-op placeholder until step
// 3 wires real tab-switching (see wireNormalMode()'s .cpp body).
// WI-06: takes bool& imeComposing - same "session-lifetime UI state, not
// document state" reasoning as freeCursorModeEnabled/isDraggingMinimap (see
// their own comments above). true for the duration of an in-progress IME
// composition (WM_IME_STARTCOMPOSITION..WM_IME_ENDCOMPOSITION); gates
// handleKeyDownEvent()/handleCharEvent() so ordinary key/char dispatch never
// runs while an IME is actively composing (see those functions' .cpp
// comments).
// WI-07 step4: takes ui::StatusBar& - created/positioned alongside tabBar in
// cfg.onDeferredInit (docked along the BOTTOM edge instead of the top - see
// ui::StatusBar's own header comment), repopulated every frame from the
// paint handler (same "no dirty-check guard at this DoD's scale" convention
// tabBar.setTabs() already follows).
// WI-08: takes core::Settings& and settingsPath - settings itself is
// Workspace-wide (not per-EditorSession) process configuration, same
// placement reasoning as searchHistory above. settingsPath is threaded
// through (rather than resolved again here) so the new "settings.reload"
// command (see buildCommandRegistry()'s .cpp body) can re-read the same
// file main.cpp already loaded from at startup; nullopt if
// resolveAppDataDir() failed there, in which case the reload command is a
// silent no-op (same graceful-degradation treatment as searchHistoryPath).
//
// WI-10: takes core::KeyBindings&/keyBindingsPath (same settings/
// settingsPath placement reasoning) plus platform::AcceleratorTableHandle&
// accelTable - the live HACCEL main.cpp's runMessageLoop() reads every
// message-loop iteration. The new "keybindings.reload"/"keybindings.preset.*"
// palette commands (buildCommandRegistry()'s .cpp body) reassign accelTable
// in place (HandleGuard::operator=(HandleGuard&&) destroys the old HACCEL
// first) and mutate keyBindings itself - the latter is ALSO consulted
// per-keystroke by the manual-chain handle*Key() functions (see
// keybinding_dispatch.h's chordMatches()), so a reload/preset-switch takes
// effect immediately for those without any table-rebuild step, unlike the
// HACCEL side.
//
// WI-11: takes core::RecentFiles& (recorded into after every successful
// open/save - dispatchOpenCommand()/handleDropFilesEvent()/
// openFileAndSyncView()/performSave()'s own .cpp bodies - and read from by
// the "最近使ったファイル" menu; same Workspace-wide-not-per-EditorSession
// placement as searchHistory/settings above, saved once at clean exit by
// main.cpp, NOT threaded a *Path here - see core::RecentFiles' own header
// comment on why the batched-at-exit save doesn't need this function to
// know the path). Takes `menuHandles` BY VALUE (an HMENU pair, cheap to
// copy) so refreshRecentFilesMenu() can be called from wherever a
// recentFiles.record() just happened. Takes `autosave` (AutosaveContext,
// see command_dispatch.h) - UNLIKE recentFiles/settings/keyBindings this
// bundles several related refs into one struct rather than adding 3 more
// individual parameters here, since its own callers (performSave()'s
// clearAutoSave() call, confirmDiscardIfDirty()'s DontSave branch, the new
// autoSaveAllDirtySessions() helper this function wires to WM_TIMER/
// WM_KILLFOCUS) all need the same {autosaveDir, index, indexPath} triple
// together, every time.
//
// WI-14b: takes std::optional<logmode::LogIndexWorker>& logIndexWorker,
// EMPTY at the time this function runs (LogIndexWorker's constructor
// requires a real HWND and starts a background std::thread immediately -
// unlike render::RenderPipeline, there is no default-construct-then-attach()
// shape available). cfg.onDeferredInit below emplace()s it once the real
// hwnd is known - same timing this function already uses for
// renderPipeline.attach(hwnd)/findBar.create(hwnd, ...)/outlinePane's
// CreateWindowExW, not the "construct in main.cpp right after
// window.create() returns" phrasing an earlier draft of this WI's plan used
// (window.create() returning true only means CreateWindowExW succeeded -
// onDeferredInit is this codebase's existing, consistent place for
// HWND-dependent initialization that must happen before the window is truly
// usable). No command/UI calls logIndexWorker->requestIndex() yet (WI-14c) -
// this WI only wires the construction plus cfg.onAppMessage's
// kMsgLogIndexReady receiving/routing branch (see this file's .cpp body),
// proven correct by tests/integration/logmode_log_index_worker_test.cpp.
//
// WI-14d: takes `userLogPatterns` (mutable - "Log: Reload Patterns"
// reassigns it) and `logPatternsDir` (the %APPDATA%\NeoMIFES\log_patterns\
// directory resolveLogPatternsStartupState() (main.cpp) already resolved -
// nullopt if that resolution failed, same graceful-degradation contract
// settingsPath/keyBindingsPath above have). Threaded straight through to
// buildCommandRegistry() (this file's own .cpp) - see
// appendLogModeCommands()'s comment for how these two feed the
// "logmode.enable.*"/"Log: Reload Patterns" commands.
//
// WI-15b: takes std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker,
// EMPTY at the time this function runs - same construction-timing reasoning
// as logIndexWorker above (JsonTreeWorker's constructor also requires a real
// HWND and starts a background std::thread immediately). cfg.onDeferredInit
// below emplace()s it once the real hwnd is known. No command/UI calls
// jsonTreeWorker->requestIndex() yet (WI-15c) - this WI only wires the
// construction plus cfg.onAppMessage's kMsgJsonTreeReady receiving/routing
// branch (see this file's .cpp body), proven correct by
// tests/integration/jsontree_json_tree_worker_test.cpp.
//
// WI-16b: takes std::optional<csvmode::CsvModelWorker>& csvModelWorker,
// EMPTY at the time this function runs - same construction-timing reasoning
// as jsonTreeWorker above. cfg.onDeferredInit below emplace()s it once the
// real hwnd is known. No command/UI calls csvModelWorker->requestIndex() yet
// (WI-16c) - this WI only wires the construction plus cfg.onAppMessage's
// kMsgCsvIndexReady receiving/routing branch (see this file's .cpp body),
// proven correct by tests/integration/csvmode_csv_model_worker_test.cpp.
//
// WI-15c: takes ui::JsonTreePane& jsonTreePane (created/positioned in
// cfg.onDeferredInit, same "create then prime the first onParentResized()
// call" shape createAndPositionOutlinePane() already established - see that
// function's own comment) and const void*& jsonTreePanePendingSessionToken.
// The latter is NOT EditorSession state (unlike jsonTree()/
// jsonTreeIndexInFlight(), WI-15b) - jsonTreePane is a single Workspace-wide
// widget, so "which session's still-in-flight async result should
// auto-populate the pane once it lands" is UI-layer state, same
// wWinMain-local placement as freeCursorModeEnabled/isDraggingMinimap above.
// Compared by pointer VALUE only against each kMsgJsonTreeReady message's
// wParam (never dereferenced) - same safety argument as
// applyLogIndexReadyMessage()'s own sessionToken comparison. Cleared to
// nullptr both when the pane is explicitly hidden (toggle-off or Escape) and
// once a matching result has been consumed - see refreshJsonTreePane()/
// handleJsonTreeKey()'s own .cpp comments for why an unclearred stale token
// would let the pane silently reappear after the user closed it.
//
// WI-16c: takes ui::CsvGridPane& csvGridPane and
// const void*& csvGridPanePendingSessionToken - same pending-token contract
// jsonTreePanePendingSessionToken above establishes, with one addition:
// unlike JsonTreePane/OutlinePane (260dip docked strips that coexist with
// the document view), CsvGridPane replaces the ENTIRE client area, so it
// must also be cleared/hidden on every tab switch and document swap (not
// just toggle-off/Escape) - see syncViewForActiveSession()/
// resetViewAfterDocumentSwap()'s own .cpp comments for why a stale
// full-screen grid left open across those events is a materially worse bug
// than JsonTreePane/OutlinePane's own already-accepted "doesn't auto-hide on
// tab switch" gap. CommandDispatchContext (command_dispatch.h) also gained
// these two fields, rather than threading them as 2 more parameters through
// every dispatch*Command() function that calls those two sync functions -
// see that struct's own field comment for the reasoning.
//
// WI-15e: takes ui::JsonPathBar& jsonPathBar - a plain submit-on-Enter
// overlay with no pending-token/tab-switch machinery of its own (unlike
// JsonTreePane/CsvGridPane above), same lifecycle shape as gotoLineBar - it
// only exists while open and is fully resolved (result applied or dropped)
// by the time Enter/Escape returns.
//
// WI-17b: takes std::optional<git::GitDiffWorker>& gitDiffWorker - same
// deferred-construction shape as jsonTreeWorker/csvModelWorker above
// (emplace()d once inside cfg.onDeferredInit, once a real hwnd exists). No
// UI pane parameter yet (unlike jsonTreePane/csvGridPane) - this WI wires
// only the worker + EditorSession + kMsgGitDiffReady routing, no command
// calls beginGitDiffIndexing() yet (see build_plan.md's WI-17b section).
//
// WI-15g: takes std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker - same
// deferred-construction shape as jsonTreeWorker/gitDiffWorker above. No UI
// pane parameter yet (unlike jsonTreePane/csvGridPane) - this WI wires only
// the worker + EditorSession + kMsgXmlTreeReady routing, no command calls
// beginXmlTreeIndexing() yet (mirrors WI-15b/WI-17b's own scoping - UI is a
// later sub-WI).
//
// WI-15h: xmlTreeWorker now feeds the SAME jsonTreePane/
// jsonTreePanePendingSessionToken pair JSON already uses - no new pane
// parameter was added. ui::JsonTreePane was designed from the start (WI-15c,
// see that class's own header comment: "the JSON/XML structure tree panel")
// to serve either format via the generic ui::OutlineItem it already takes,
// and the single Ctrl+Shift+J toggle auto-detects JSON vs XML via
// EditorSession::language() (see normal_mode_wiring.cpp's
// refreshStructureTreePane()). jsonTreePanePendingSessionToken is shared,
// not duplicated, between the two paths - safe because a session's
// language() is fixed at toggle-time, so exactly ONE of
// beginJsonTreeIndexing()/beginXmlTreeIndexing() is ever kicked off per
// toggle-on, meaning the token can only ever be satisfied by that one
// request's own result message (kMsgJsonTreeReady XOR kMsgXmlTreeReady) -
// the same "at most one pending expectation, anything else is stale" logic
// this token already relies on across ordinary tab switches.
// WI-17e: takes ui::GitPane& gitPane and std::optional<git::GitStatusWorker>&
// gitStatusWorker - same deferred-construction shape as jsonTreeWorker/
// gitDiffWorker above for the worker, and the same 260dip-docked-strip
// lifecycle shape as jsonTreePane/outlinePane above for the pane (see
// git_pane.h's own class comment for why it is NOT modeled on CsvGridPane's
// full-client-area replacement). Unlike jsonTreePane/csvGridPane, there is no
// "...PendingSessionToken" parameter here - GitPane's own backing state
// (Workspace::gitStatus()) is Workspace-wide, not per-EditorSession, so there
// is no per-tab race to guard against the way jsonTreePanePendingSessionToken/
// csvGridPanePendingSessionToken exist to resolve (see workspace.h's own
// gitStatus()-placement comment for the full reasoning).
//
// WI-17f: takes std::optional<document::Document>& diffViewDocument - the
// ONLY thing this parameter is for is OWNING the Diff view's synthesized
// document's storage across its open/close lifetime (RenderPipeline::
// setDocument() itself is non-owning). Unlike gitPane/gitStatusWorker
// above, this is deliberately NOT threaded into handleKeyDownEvent()/
// dispatchCommand()'s own Diff-view guards - those only need to ASK "is the
// Diff view showing right now", which RenderPipeline::isDiffViewActive()
// (already reachable via the renderPipeline parameter every relevant
// function already has) answers on its own. Only the git.toggleDiffView
// command's own action (buildCommandRegistry()) ever touches this
// parameter directly.
void wireNormalMode(ui::MainWindowConfig& cfg, ui::MainWindow& window, render::RenderPipeline& renderPipeline,
                    Workspace& workspace, HINSTANCE hInstance, ui::FindBar& findBar,
                    ui::FindReplaceDialog& findReplaceDialog,
                    ui::CommandPalette& commandPalette, ui::GotoLineBar& gotoLineBar,
                    ui::JsonPathBar& jsonPathBar, ui::GrepBar& grepBar,
                    GrepState& grepState, core::SearchHistory& searchHistory, ui::OutlinePane& outlinePane,
                    ui::TabBar& tabBar, ui::StatusBar& statusBar, core::Settings& settings,
                    const std::optional<std::filesystem::path>& settingsPath, core::KeyBindings& keyBindings,
                    const std::optional<std::filesystem::path>& keyBindingsPath,
                    platform::AcceleratorTableHandle& accelTable, bool& freeCursorModeEnabled,
                    bool& isDraggingMinimap, bool& imeComposing, core::RecentFiles& recentFiles,
                    MenuBarHandles menuHandles, AutosaveContext& autosave,
                    std::optional<logmode::LogIndexWorker>& logIndexWorker,
                    std::vector<logmode::LogPatternRule>& userLogPatterns,
                    const std::optional<std::filesystem::path>& logPatternsDir,
                    std::optional<jsontree::JsonTreeWorker>& jsonTreeWorker,
                    std::optional<csvmode::CsvModelWorker>& csvModelWorker, ui::JsonTreePane& jsonTreePane,
                    const void*& jsonTreePanePendingSessionToken, ui::CsvGridPane& csvGridPane,
                    const void*& csvGridPanePendingSessionToken,
                    std::optional<git::GitDiffWorker>& gitDiffWorker,
                    std::optional<xmltree::XmlTreeWorker>& xmlTreeWorker, bool& jsonPathBarIsForXml,
                    ui::GitPane& gitPane, std::optional<git::GitStatusWorker>& gitStatusWorker,
                    std::optional<document::Document>& diffViewDocument);

}  // namespace neomifes::app
