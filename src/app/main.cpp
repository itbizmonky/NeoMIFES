// NeoMIFES - application entry point.
//
// Responsibilities:
//   1. Mark process start time (QPC) as early as possible.
//   2. Enable Per-Monitor V2 DPI awareness before any HWND is created.
//   3. Create the top-level MainWindow (Win32 skeleton).
//   4. On real launches, attach a RenderPipeline (Direct2D/DXGI) after the
//      first paint (Phase 3a, ADR-009) - measurement modes skip this so
//      --measure-startup's timing contract is untouched. The Document (see
//      --open below, or an empty Document by default) is handed to the
//      RenderPipeline so it can draw real content (Phase 3b, ADR-010).
//   5. Run the message loop.
//
// Command-line modes (used by PoC tests, disabled in normal launches):
//   --measure-startup <out.json>  Record startup timings + memory then exit
//                                 immediately after first paint.
//   --measure-memory  <out.json>  Same, but focus on the memory snapshot.
//                                 (Currently identical output — kept separate
//                                 for future divergence.)
//   --measure-frame   <out.json>  Drive a synthetic scroll (setTopLine() over
//                                 N frames) through the attached Document,
//                                 timing each render() call, then write a
//                                 FrameProfile (Phase 3c, ADR-011). Uses
//                                 --open's document if given, otherwise
//                                 synthesizes a large one (launch_setup.cpp).
//
// Command-line options (real launches only):
//   --open <path>  Load a UTF-8 file into the Document at startup so its
//                  content renders. A missing/invalid file falls back to an
//                  empty Document rather than blocking startup. File->Open
//                  dialog / recent-files UI is a later phase.
//
// WI-04 step 3b: this file now owns only wWinMain itself, window creation,
// the message loop, and Workspace/RenderPipeline construction - the
// process-bootstrap helpers (command-line parsing, single-instance check,
// DPI/Common-Controls setup, startup Document construction) moved to
// launch_setup.{h,cpp}, and the real (non-measurement) launch's callback
// wiring (~1780 lines: wireNormalMode() + ~46 supporting functions) moved
// to normal_mode_wiring.{h,cpp} - both extracted so this file could shrink
// to the WI-04 DoD of <=500 lines. See those two files' headers for why the
// split falls where it does.

#include <windows.h>

// WI-02: TaskDialogIndirect (message_dialogs.cpp) requires the Common
// Controls v6 ComCtl32 that only loads with this embedded manifest
// dependency - v5.82 (the default without one) does not export it. No
// .manifest/.rc file exists anywhere in this repo, so this single-line
// linker pragma is the standard way to opt in without adding a new build
// file. Harmless for every other Win32 API this codebase already uses.
// Lives here (not launch_setup.cpp) because it is a whole-executable
// manifest concern, not tied to any one function.
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include "neomifes/app/editor_session.h"
#include "neomifes/app/launch_setup.h"
#include "neomifes/app/normal_mode_wiring.h"
#include "neomifes/app/workspace.h"
#include "neomifes/core/search_history.h"
#include "neomifes/document/document.h"
#include "neomifes/platform/app_data_dir.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/platform/perf_clock.h"
#include "neomifes/platform/process_metrics.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/ui/command_palette.h"
#include "neomifes/ui/find_bar.h"
#include "neomifes/ui/goto_line_bar.h"
#include "neomifes/ui/grep_bar.h"
#include "neomifes/ui/main_window.h"
#include "neomifes/ui/outline_pane.h"

#include "frame_profile.h"
#include "startup_profile.h"

namespace {

using neomifes::app::claimSingleInstance;
using neomifes::app::debugLogRenderError;
using neomifes::app::DocumentFileState;
using neomifes::app::enableHighDpi;
using neomifes::app::FrameProfile;
using neomifes::app::GrepState;
using neomifes::app::initCommonControls;
using neomifes::app::LaunchArgs;
using neomifes::app::LaunchMode;
using neomifes::app::parseArgs;
using neomifes::app::prepareDocument;
using neomifes::app::StartupProfile;
using neomifes::app::wireNormalMode;
using neomifes::app::Workspace;
using neomifes::core::SearchHistory;
using neomifes::document::Document;
using neomifes::platform::currentProcessMemory;
using neomifes::platform::KernelHandle;
using neomifes::platform::PerfClock;
using neomifes::platform::resolveAppDataDir;
using neomifes::render::RenderPipeline;
using neomifes::ui::CommandPalette;
using neomifes::ui::FindBar;
using neomifes::ui::GotoLineBar;
using neomifes::ui::GrepBar;
using neomifes::ui::MainWindow;
using neomifes::ui::MainWindowConfig;
using neomifes::ui::OutlinePane;

int runMessageLoop() noexcept {
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ~5s at 60fps - long enough to surface an occasional dropped-frame spike
// without making the CI PoC step slow.
constexpr std::uint32_t kMeasureFrameCount = 300;

// Drives a synthetic scroll through `pipeline`'s attached Document, timing
// each render() call. Deliberately times the FULL render() call including
// Present1's vsync wait (this proves the Phase 3 DoD wording "60fps scroll
// verification" directly - a frame that keeps pace with vsync without
// spiking - rather than isolating TextLayoutCache's own CPU cost, which
// render_text_layout_cache_bench.cpp already does without any device/vsync
// involved at all).
FrameProfile runFrameMeasurement(RenderPipeline& pipeline, std::uint64_t syntheticLineCount) {
    std::vector<std::int64_t> durationsNs;
    durationsNs.reserve(kMeasureFrameCount);
    for (std::uint32_t i = 0; i < kMeasureFrameCount; ++i) {
        pipeline.setTopLine(i);
        const auto start    = PerfClock::now();
        const auto rendered = pipeline.render();
        const auto end       = PerfClock::now();
        if (rendered) {
            durationsNs.push_back((end - start).count());
        }
    }
    return FrameProfile::fromDurations(std::move(durationsNs), syntheticLineCount,
                                       pipeline.layoutCacheStats());
}

// The three cfg-wiring branches below are each pulled into their own
// function (rather than inlined in wWinMain) for the same cognitive-
// complexity reason normal_mode_wiring.cpp's own helpers were extracted.
void wireMeasureStartupOrMemoryMode(MainWindowConfig& cfg, StartupProfile& profile,
                                    MainWindow& window) {
    cfg.onWindowCreated = [&profile](HWND) {
        profile.windowCreatedNs = PerfClock::nanosSinceProcessStart();
    };
    cfg.onFirstPaint = [&profile, &window](HWND) {
        profile.firstPaintNs = PerfClock::nanosSinceProcessStart();
        const auto mem = currentProcessMemory();
        profile.workingSetBytesAtFirstPaint        = mem.workingSetBytes;
        profile.privateWorkingSetBytesAtFirstPaint = mem.privateWorkingSetBytes;
        window.requestClose();
    };
}

// Reuses onDeferredInit exactly like the Normal path does for real
// rendering - no new MainWindow hooks, no mouse/keyboard plumbing. The
// entire synthetic-scroll measurement loop runs synchronously inside this
// one callback, then closes the window. WI-04: still takes a plain
// Document& (not EditorSession&) - only ever needs setDocument(), and this
// mode never touches SelectionModel/CommandDispatcher/etc. at all.
void wireMeasureFrameMode(MainWindowConfig& cfg, MainWindow& window, RenderPipeline& renderPipeline,
                          Document& document, FrameProfile& frameProfile,
                          std::uint64_t syntheticLineCount) {
    cfg.onDeferredInit = [&window, &renderPipeline, &document, &frameProfile,
                          syntheticLineCount](HWND hwnd) {
        const auto attached = renderPipeline.attach(hwnd);
        if (attached) {
            renderPipeline.setDocument(&document);
            frameProfile = runFrameMeasurement(renderPipeline, syntheticLineCount);
        } else {
            debugLogRenderError("RenderPipeline::attach", attached.error());
        }
        window.requestClose();
    };
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance,
                    HINSTANCE /*hPrevInstance*/,
                    PWSTR     /*pCmdLine*/,
                    int       /*nCmdShow*/) {
    // Marker #0: process wall-clock origin. Called as the very first thing.
    PerfClock::markProcessStart();

    StartupProfile profile{};
    profile.winMainEnterNs = 0;  // by definition, zero relative to markProcessStart()

    const LaunchArgs args = parseArgs();

    // Single-instance check applies to real user launches only. --measure-startup
    // / --measure-memory are PoC/CI harness invocations that intentionally spawn
    // fresh isolated processes for benchmarking; gating those on this check would
    // make CI runs flaky if two measurement runs ever overlapped.
    KernelHandle singleInstanceMutex;
    if (args.mode == LaunchMode::Normal && !claimSingleInstance(singleInstanceMutex)) {
        return 0;
    }

    enableHighDpi();
    // Phase 5b3a: defensive, see initCommonControls()'s comment. Cheap and
    // harmless even on modes that never create a FindBar (Measure*).
    initCommonControls();

    // WI-02: currentDocumentPath/fileState are populated together with
    // `document` by prepareDocument() (same loadFile() call) - see
    // launch_setup.h's comment on why currentDocumentPath must only be set
    // on a SUCCESSFUL load (a stale path pointing at an empty `document`
    // would make a later Ctrl+S wipe the original file, not just mis-color
    // syntax highlighting the way an incorrect path did before saveFile()
    // existed). All three are consumed immediately below to construct
    // `workspace` and are not read again afterward.
    std::uint64_t                        syntheticLineCountUsed = 0;
    std::optional<std::filesystem::path> currentDocumentPath;
    DocumentFileState                    fileState;
    Document document =
        prepareDocument(args, syntheticLineCountUsed, fileState, currentDocumentPath);
    FrameProfile  frameProfile{};

    // WI-04: "the currently open document"'s complete state (Document/
    // SelectionModel/CommandDispatcher/Viewport/FoldingModel/
    // BookmarkManager/find-replace state/file path - previously ~15
    // separate wWinMain locals) now lives in one EditorSession, owned by a
    // Workspace (workspace.h) - the container WI-05's tab UI will grow to
    // hold more than one session; today it always holds exactly one.
    // Declared before window/renderPipeline so it outlives both (reverse
    // destruction order) - RenderPipeline::setDocument() below hands out a
    // non-owning pointer into workspace.active().document() that must not
    // dangle while the message loop runs, same reasoning as the pre-WI-04
    // `document` local. Existing keybindings (Ctrl+O etc.) still operate on
    // workspace.active() directly - see workspace.h's header comment on why
    // Workspace::openFile()/closeSession() stay unused until WI-05.
    Workspace workspace(std::move(document), fileState, currentDocumentPath);

    // Phase 7v: true while a minimap click-and-drag is in progress. Reset to
    // false at the top of every handleMouseDownEvent() call (the only
    // reliable reset point - MainWindow exposes no onMouseUp hook, see
    // normal_mode_wiring.cpp's handleMouseDownEvent() comment) and set back
    // to true only by tryHandleMinimapClick() when the down-click itself
    // lands on the strip. WI-04: stays a wWinMain local, not an
    // EditorSession member - it is a gesture on RenderPipeline's single
    // minimap widget (Workspace-wide), not per-document state (see
    // normal_mode_wiring.h's EditorSession member-placement notes).
    bool isDraggingMinimap = false;
    // Find bar state (Phase 5b3a) - lives here (not inside FindBar itself)
    // so FindBar can stay decoupled from neomifes::search, same rationale as
    // core::ReplaceAllCommand staying decoupled from neomifes::search in
    // Phase 5b2 (see docs/history/TIMELINE.md's Phase 5b3a entry).
    FindBar findBar;
    // Command palette state (Phase 5b3c) - a second, independent overlay
    // reusing the WC_EDIT+SetWindowSubclass pattern findBar established
    // (see command_palette.h's class comment for how it differs: a second
    // control type, WC_LISTBOX, is subclassed too).
    CommandPalette commandPalette;
    // Ctrl+G goto-line/column overlay (Phase 4b8b) - simplest of the three
    // overlays (single WC_EDIT, no debounce/listbox).
    GotoLineBar gotoLineBar;
    // Ctrl+Shift+F Grep results pane (Phase 5c3) - two WC_EDIT + one
    // WC_LISTBOX, see grep_bar.h's class comment. grepState holds the actual
    // search::GrepMatch data GrepBar itself never sees (same rationale as
    // findReplaceState). WI-04: stays a wWinMain local, not an
    // EditorSession member - Grep searches the whole project, not the
    // currently open document (see normal_mode_wiring.cpp's runGrepQuery()
    // comment).
    GrepBar   grepBar;
    GrepState grepState;
    // Symbol outline panel (Ctrl+Shift+O, Phase 7g) - a single WC_TREEVIEW,
    // see outline_pane.h's class comment for how it differs from the
    // overlays above (WM_NOTIFY routing, stays open after a jump).
    OutlinePane outlinePane;
    // Search-pattern history (Phase 5c5) - shared by Find bar's find edit
    // and the Grep dialog's query edit (deliberately NOT the command
    // palette - core::search_history.h's file comment explains why). Only
    // meaningful in Normal mode (the other launch modes never create
    // FindBar/GrepBar), so the %APPDATA% resolution + load only happens
    // there - no point paying filesystem I/O on every --measure-* CI
    // harness invocation. searchHistoryPath stays nullopt (and saving at
    // exit becomes a no-op) if resolveAppDataDir() fails - see
    // platform::resolveAppDataDir()'s doc comment for why that's treated as
    // a graceful degradation, not a startup failure.
    SearchHistory                         searchHistory;
    std::optional<std::filesystem::path> searchHistoryPath;
    if (args.mode == LaunchMode::Normal) {
        searchHistoryPath = resolveAppDataDir();
        if (searchHistoryPath) {
            *searchHistoryPath /= L"search_history.json";
            searchHistory = SearchHistory::loadFrom(*searchHistoryPath);
        }
    }
    // Free cursor mode (Phase 4b8e, simplified - see approved plan). Toggled
    // via the command palette ("Toggle Free Cursor Mode") - session-lifetime
    // UI state, not document state (see normal_mode_wiring.cpp's
    // handleFreeCursorRightArrow() comment), so it stays a wWinMain local
    // rather than an EditorSession member.
    bool freeCursorModeEnabled = false;

    MainWindow window;
    MainWindowConfig cfg{};
    RenderPipeline renderPipeline;

    // Each mode's hook wiring lives in its own function (see definitions
    // above, or normal_mode_wiring.cpp for the Normal case) - ordering
    // matters for MeasureStartup/MeasureMemory (window created -> first
    // paint), matters not at all for the others.
    if (args.mode == LaunchMode::MeasureStartup || args.mode == LaunchMode::MeasureMemory) {
        wireMeasureStartupOrMemoryMode(cfg, profile, window);
    } else if (args.mode == LaunchMode::MeasureFrame) {
        wireMeasureFrameMode(cfg, window, renderPipeline, workspace.active().document(), frameProfile,
                             syntheticLineCountUsed);
    } else {
        wireNormalMode(cfg, window, renderPipeline, workspace.active(), hInstance, findBar, commandPalette,
                       gotoLineBar, grepBar, grepState, searchHistory, outlinePane,
                       freeCursorModeEnabled, isDraggingMinimap);
        // Phase 7b/7d: reflect the startup document's language before the
        // first paint - attach() itself happens later inside onDeferredInit,
        // but setLanguage() only touches plain member state, so it's safe to
        // call before RenderPipeline is attached.
        renderPipeline.setLanguage(workspace.active().language());
    }

    if (!window.create(hInstance, cfg)) {
        return 1;
    }

    const int rc = runMessageLoop();

    // Persist search history once, at clean exit (not after every search) -
    // a crash loses only the current session's newly-recorded entries, an
    // acceptable trade-off against writing to disk on every keystroke-driven
    // search (Phase 5c5).
    if (searchHistoryPath) {
        searchHistory.saveTo(*searchHistoryPath);
    }

    if (args.mode == LaunchMode::MeasureStartup || args.mode == LaunchMode::MeasureMemory) {
        profile.measuredExitNs = PerfClock::nanosSinceProcessStart();
        // Failure to write is fatal for the PoC — surface it via non-zero exit.
        if (!profile.writeJson(args.outputPath)) {
            return 2;
        }
    } else if (args.mode == LaunchMode::MeasureFrame) {
        if (!frameProfile.writeJson(args.outputPath)) {
            return 2;
        }
    }
    return rc;
}
