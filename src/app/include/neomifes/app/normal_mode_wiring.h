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

#include <vector>

#include "neomifes/app/editor_session.h"
#include "neomifes/app/workspace.h"
#include "neomifes/core/search_history.h"
#include "neomifes/render/render_error.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/search/grep_service.h"
#include "neomifes/ui/command_palette.h"
#include "neomifes/ui/find_bar.h"
#include "neomifes/ui/goto_line_bar.h"
#include "neomifes/ui/grep_bar.h"
#include "neomifes/ui/main_window.h"
#include "neomifes/ui/outline_pane.h"
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
void wireNormalMode(ui::MainWindowConfig& cfg, ui::MainWindow& window, render::RenderPipeline& renderPipeline,
                    Workspace& workspace, HINSTANCE hInstance, ui::FindBar& findBar,
                    ui::CommandPalette& commandPalette, ui::GotoLineBar& gotoLineBar, ui::GrepBar& grepBar,
                    GrepState& grepState, core::SearchHistory& searchHistory, ui::OutlinePane& outlinePane,
                    ui::TabBar& tabBar, bool& freeCursorModeEnabled, bool& isDraggingMinimap,
                    bool& imeComposing);

}  // namespace neomifes::app
