#pragma once

// EditorWindow (WI-20a) - everything that used to be a per-window wWinMain
// local (main.cpp, pre-WI-20a), now bundled behind one heap-allocated
// object so SessionManager can hold more than one. Same "raw type <-> app-
// layer wrapper" naming pairing this codebase already uses one level down
// (document::Document <-> app::EditorSession): ui::MainWindow is the raw
// Win32 shell, EditorWindow is the app-layer bundle of everything that
// shell's wireNormalMode() wiring needs a stable address for.
//
// MUST be held behind std::unique_ptr (never by value in a std::vector, and
// never moved) - wireNormalMode() (normal_mode_wiring.h) stores ~40 `[&...]`
// reference-capturing lambdas into MainWindowConfig/FindBarConfig/etc. that
// fire arbitrarily far in the future (any WM_* message); every member below
// must keep the SAME address for the whole window's lifetime, which a
// std::vector<EditorWindow> held by value cannot guarantee across
// reallocation the moment a second window is created. Non-copyable/
// non-movable is enforced explicitly below (same Rule-of-Five-spelled-out
// convention Workspace/Libgit2Guard already follow) even though holding a
// non-movable Workspace member would already make this true implicitly.

#include <filesystem>
#include <optional>

#include "neomifes/app/menu_bar.h"
#include "neomifes/app/normal_mode_wiring.h"
#include "neomifes/app/workspace.h"
#include "neomifes/document/document.h"
#include "neomifes/git/git_diff_worker.h"
#include "neomifes/git/git_status_worker.h"
#include "neomifes/jsontree/json_tree_worker.h"
#include "neomifes/logmode/log_index_worker.h"
#include "neomifes/ui/csv_grid_pane.h"
#include "neomifes/ui/git_pane.h"
#include "neomifes/ui/json_path_bar.h"
#include "neomifes/ui/json_tree_pane.h"
#include "neomifes/xmltree/xml_tree_worker.h"

namespace neomifes::app {

// The JSON/XML/CSV/Git "structural view" family (WI-14/15/16/17) grouped
// into its own sub-bundle - a plain composition-root aggregate with no
// behavior of its own (same "bundle related fields together" convention
// AutosaveContext, command_dispatch.h, already establishes), kept as a
// SEPARATE struct purely so EditorWindow itself doesn't flatten two only-
// loosely-related concerns (core editing chrome vs. this tooling) into one
// oversized class (CLAUDE.md's ~300-line/class guideline). Each worker
// below is EMPTY until wireNormalMode()'s onDeferredInit emplace()s it with
// this window's own real HWND - see normal_mode_wiring.h's own per-worker
// header comments for why (each worker's constructor requires a real HWND
// and starts a background std::thread immediately, so there is no
// default-construct-then-attach() shape available the way
// render::RenderPipeline has). Because each worker posts its completion
// message to the SPECIFIC hwnd it was constructed with, giving every
// EditorWindow its own worker instances needs no cross-window message
// routing to design - Win32 already scopes PostMessageW(hwnd, ...)
// per-HWND for free.
struct StructuralViewState {
    ui::JsonTreePane jsonTreePane;
    ui::CsvGridPane  csvGridPane;
    ui::GitPane      gitPane;
    ui::JsonPathBar  jsonPathBar;
    // WI-15i: which query language the shared jsonPathBar is currently
    // evaluating on submit - see normal_mode_wiring.h's own comment on
    // wireNormalMode()'s matching parameter for the full reasoning.
    bool jsonPathBarIsForXml = false;

    std::optional<logmode::LogIndexWorker>  logIndexWorker;
    std::optional<jsontree::JsonTreeWorker> jsonTreeWorker;
    std::optional<csvmode::CsvModelWorker>  csvModelWorker;
    std::optional<git::GitDiffWorker>       gitDiffWorker;
    std::optional<xmltree::XmlTreeWorker>   xmlTreeWorker;
    std::optional<git::GitStatusWorker>     gitStatusWorker;

    // WI-15c/16c: which EditorSession's still-in-flight async index request
    // should auto-populate the matching pane once it lands - UI-layer state
    // (each pane is one widget per WINDOW, not per EditorSession), compared
    // only by pointer VALUE, never dereferenced - see normal_mode_wiring.h's
    // own comment on wireNormalMode()'s matching parameters for the full
    // safety argument.
    const void* jsonTreePanePendingSessionToken = nullptr;
    const void* csvGridPanePendingSessionToken  = nullptr;

    // WI-17f: owns the Diff view's own synthesized document's storage while
    // it's open - see normal_mode_wiring.h's own comment on
    // wireNormalMode()'s matching parameter for why this is the ONLY thing
    // this member is for.
    std::optional<document::Document> diffViewDocument;
};

// One independent top-level window's complete editing state. Constructed
// with the same 3 startup-document parameters Workspace itself takes
// (forwarded straight through) - everything else here default-constructs
// exactly as it did as a wWinMain local before WI-20a.
class EditorWindow {
public:
    explicit EditorWindow(document::Document                          initialDocument  = document::Document{},
                          DocumentFileState                            initialFileState = {},
                          const std::optional<std::filesystem::path>& initialPath      = std::nullopt);

    EditorWindow(const EditorWindow&)            = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;
    EditorWindow(EditorWindow&&)                 = delete;
    EditorWindow& operator=(EditorWindow&&)      = delete;
    ~EditorWindow() = default;

    Workspace              workspace;
    ui::MainWindow          window;
    render::RenderPipeline  renderPipeline;
    ui::FindBar             findBar;
    ui::FindReplaceDialog   findReplaceDialog;
    ui::CommandPalette      commandPalette;
    ui::GotoLineBar         gotoLineBar;
    ui::GrepBar             grepBar;
    GrepState                grepState;
    ui::OutlinePane          outlinePane;
    ui::TabBar               tabBar;
    ui::StatusBar            statusBar;
    // This window's OWN HMENU pair (buildMenuBar() is called once per
    // window by SessionManager) - a single HMENU cannot be attached to two
    // different windows via CreateWindowExW's hMenu (the first window's
    // DestroyWindow would implicitly destroy the menu out from under the
    // second, per Win32's own attach-implies-ownership contract - see
    // menu_bar.h's own header comment).
    MenuBarHandles menuHandles{};

    // Session-lifetime UI state that isn't per-EditorSession document
    // state - same placement reasoning each had as a wWinMain local before
    // WI-20a (see normal_mode_wiring.h's own per-parameter comments).
    bool isDraggingMinimap     = false;
    bool freeCursorModeEnabled = false;
    bool imeComposing          = false;

    StructuralViewState structuralViews;
};

}  // namespace neomifes::app
