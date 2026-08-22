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
#include <memory>
#include <optional>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "neomifes/app/autosave.h"
#include "neomifes/app/command_dispatch.h"
#include "neomifes/app/editor_session.h"
#include "neomifes/app/launch_setup.h"
#include "neomifes/app/menu_bar.h"
#include "neomifes/app/message_dialogs.h"
#include "neomifes/app/normal_mode_wiring.h"
#include "neomifes/app/theme_settings.h"
#include "neomifes/app/workspace.h"
#include "neomifes/core/autosave_index.h"
#include "neomifes/core/key_bindings.h"
#include "neomifes/core/recent_files.h"
#include "neomifes/core/search_history.h"
#include "neomifes/core/settings.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/logmode/log_pattern_file.h"
#include "neomifes/platform/app_data_dir.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/platform/perf_clock.h"
#include "neomifes/platform/process_metrics.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/ui/command_palette.h"
#include "neomifes/ui/csv_grid_pane.h"
#include "neomifes/ui/find_bar.h"
#include "neomifes/ui/goto_line_bar.h"
#include "neomifes/ui/grep_bar.h"
#include "neomifes/ui/json_tree_pane.h"
#include "neomifes/ui/main_window.h"
#include "neomifes/ui/outline_pane.h"
#include "neomifes/ui/status_bar.h"
#include "neomifes/ui/tab_bar.h"

#include "frame_profile.h"
#include "startup_profile.h"

namespace {

using neomifes::app::AutosaveContext;
using neomifes::app::claimSingleInstance;
using neomifes::app::debugLogRenderError;
using neomifes::app::DocumentFileState;
using neomifes::app::EditorSession;
using neomifes::app::enableHighDpi;
using neomifes::app::FrameProfile;
using neomifes::app::GrepState;
using neomifes::app::initCommonControls;
using neomifes::app::LaunchArgs;
using neomifes::app::LaunchMode;
using neomifes::app::MenuBarHandles;
using neomifes::app::parseArgs;
using neomifes::app::parseThemeKind;
using neomifes::app::prepareDocument;
using neomifes::app::RecoverableAutoSave;
using neomifes::app::StartupProfile;
using neomifes::app::wireNormalMode;
using neomifes::app::Workspace;
using neomifes::core::AutosaveIndex;
using neomifes::core::KeyBindings;
using neomifes::core::RecentFiles;
using neomifes::core::SearchHistory;
using neomifes::core::Settings;
using neomifes::csvmode::CsvModelWorker;
using neomifes::document::Document;
using neomifes::git::GitDiffWorker;
using neomifes::jsontree::JsonTreeWorker;
using neomifes::logmode::LogIndexWorker;
using neomifes::platform::currentProcessMemory;
using neomifes::platform::KernelHandle;
using neomifes::platform::PerfClock;
using neomifes::platform::resolveAppDataDir;
using neomifes::render::RenderPipeline;
using neomifes::ui::CommandPalette;
using neomifes::ui::CsvGridPane;
using neomifes::ui::FindBar;
using neomifes::ui::GotoLineBar;
using neomifes::ui::JsonPathBar;
using neomifes::ui::GrepBar;
using neomifes::ui::JsonTreePane;
using neomifes::ui::MainWindow;
using neomifes::ui::MainWindowConfig;
using neomifes::ui::OutlinePane;
using neomifes::ui::StatusBar;
using neomifes::ui::TabBar;

// WI-07 step2: the underlying HACCEL may be nullptr (measurement-mode
// launches never build a meaningful one - see wWinMain - or
// buildAcceleratorTable() itself failed, command_dispatch.h's own
// non-fatal-degradation contract). TranslateAcceleratorW's own documented
// contract requires a valid HACCEL, so this checks explicitly rather than
// relying on how it happens to behave with NULL.
//
// WI-10: takes the AcceleratorTableHandle itself BY REFERENCE (not a plain
// HACCEL captured once at call time) - `.get()` is read fresh every
// GetMessageW loop iteration, so a "keybindings.reload"/"keybindings.preset.*"
// palette command reassigning wWinMain's `accelTable` local mid-run takes
// effect on the very next keystroke, no restart needed.
int runMessageLoop(HWND hwnd, const neomifes::platform::AcceleratorTableHandle& accelTable) noexcept {
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (auto* const haccel = accelTable.get();
            haccel != nullptr && ::TranslateAcceleratorW(hwnd, haccel, &msg) != 0) {
            continue;
        }
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

// WI-11: extracted from wWinMain purely to keep clang-tidy's cognitive-
// complexity check happy (src/ threshold of 25 - wWinMain grew past it once
// this WI's recent-files/autosave-index/crash-recovery resolution logic was
// added inline) - same rationale as settings.cpp's applyFields() split and
// this file's own pre-existing "three cfg-wiring branches" comment above.
// Behavior is unchanged from the inline version this replaced. Mirrors
// wWinMain's own Normal-mode-only gating (no point paying filesystem I/O on
// --measure-* harness invocations).
RecentFiles loadRecentFilesForLaunch(const LaunchArgs&                     args,
                                     std::optional<std::filesystem::path>& outRecentFilesPath) {
    RecentFiles recentFiles;
    if (args.mode == LaunchMode::Normal) {
        outRecentFilesPath = resolveAppDataDir();
        if (outRecentFilesPath) {
            *outRecentFilesPath /= L"recent.json";
            recentFiles = RecentFiles::loadFrom(*outRecentFilesPath);
        }
    }
    return recentFiles;
}

// WI-11: bundles every value resolveAutosaveStartupState() below produces -
// wWinMain needs all 4 as independently named results afterward (autosaveIndex
// is mutated by processRecoverableAutoSaves() below and later bound into
// AutosaveContext by reference, command_dispatch.h), so this exists purely to
// let that resolution logic live in its own function (same cognitive-
// complexity rationale as loadRecentFilesForLaunch() above).
struct AutosaveStartupState {
    std::optional<std::filesystem::path> autosaveDir;
    AutosaveIndex                        autosaveIndex;
    std::optional<std::filesystem::path> autosaveIndexPath;
    std::vector<RecoverableAutoSave>     recoverableAutoSaves;
};

// Unlike resolveAppDataDir() itself, the "autosave" subdirectory is NOT
// created automatically, so it's created explicitly here (best-effort: every
// AutosaveStartupState field stays at its default-constructed/empty value on
// failure, same graceful-degradation contract every other %APPDATA%-backed
// piece of state in this file already has - see AutosaveContext's own header
// comment on how every autosave-related call site tolerates that).
AutosaveStartupState resolveAutosaveStartupState(const LaunchArgs& args) {
    AutosaveStartupState state;
    if (args.mode != LaunchMode::Normal) {
        return state;
    }
    const auto appDataDir = resolveAppDataDir();
    if (!appDataDir) {
        return state;
    }
    const std::filesystem::path candidateDir = *appDataDir / L"autosave";
    std::error_code              ec;
    std::filesystem::create_directories(candidateDir, ec);
    if (ec) {
        return state;
    }
    state.autosaveDir       = candidateDir;
    state.autosaveIndexPath = candidateDir / L"index.json";
    state.autosaveIndex     = AutosaveIndex::loadFrom(*state.autosaveIndexPath);
    state.recoverableAutoSaves =
        neomifes::app::scanForRecoverableAutoSaves(*state.autosaveDir, state.autosaveIndex);
    return state;
}

// WI-14d: bundles the 2 values resolveLogPatternsStartupState() below
// produces - same "own function purely for the cognitive-complexity budget"
// rationale as AutosaveStartupState/resolveAutosaveStartupState() above,
// and the same "%APPDATA% subdirectory NOT created automatically by
// resolveAppDataDir() itself, so create_directories() here, best-effort,
// degrade to empty state on any failure" shape too - a directory of
// user-editable pattern files (log_pattern_file.h) rather than a single
// settings-style file, since a user adding one new format only ever
// touches one new file (see this WI's plan point 設計方針4).
struct LogPatternsStartupState {
    std::optional<std::filesystem::path>       logPatternsDir;
    std::vector<neomifes::logmode::LogPatternRule> userLogPatterns;
};

LogPatternsStartupState resolveLogPatternsStartupState(const LaunchArgs& args) {
    LogPatternsStartupState state;
    if (args.mode != LaunchMode::Normal) {
        return state;
    }
    const auto appDataDir = resolveAppDataDir();
    if (!appDataDir) {
        return state;
    }
    const std::filesystem::path candidateDir = *appDataDir / L"log_patterns";
    std::error_code              ec;
    std::filesystem::create_directories(candidateDir, ec);
    if (ec) {
        return state;
    }
    state.logPatternsDir  = candidateDir;
    state.userLogPatterns = neomifes::logmode::loadUserLogPatternsFromDirectory(candidateDir);
    return state;
}

// WI-11: the startup crash-recovery prompt loop - call once, right after
// `workspace` exists (Workspace::adoptSession() needs it), for every
// candidate resolveAutosaveStartupState() found above. Extracted from
// wWinMain purely to keep clang-tidy's cognitive-complexity check happy
// (same rationale as loadRecentFilesForLaunch()/AutosaveStartupState above) -
// behavior is unchanged from the inline version this replaced. `owner=nullptr`
// for showCrashRecoveryDialog() is deliberate (see that function's own doc
// comment) - `window` hasn't been created yet at this point in wWinMain.
// Regardless of the user's choice, the autosave copy for THIS candidate is
// always cleaned up (tmp file + index entry) - declining must not leave it
// around to be wrongly re-offered on the next launch, and accepting means the
// content has already been adopted into a real session (the .tmp copy is now
// redundant).
void processRecoverableAutoSaves(Workspace& workspace, const std::vector<RecoverableAutoSave>& recoverableAutoSaves,
                                 AutosaveIndex&                              autosaveIndex,
                                 const std::optional<std::filesystem::path>& autosaveIndexPath) {
    for (const RecoverableAutoSave& candidate : recoverableAutoSaves) {
        const bool accepted =
            neomifes::app::showCrashRecoveryDialog(nullptr, candidate.originalPath.filename().wstring());
        if (accepted) {
            auto loaded = neomifes::document::loadFile(candidate.autosaveTmpPath);
            if (auto* result = std::get_if<neomifes::document::LoadResult>(&loaded)) {
                const DocumentFileState recoveredFileState{.encoding   = result->detectedEncoding,
                                                            .lineEnding = result->lineEnding,
                                                            .writeBom   = result->hadBom};
                auto recoveredSession = std::make_unique<EditorSession>(
                    std::move(*result->document), recoveredFileState, candidate.originalPath);
                // The recovered content differs from originalPath on disk
                // (that's the whole point of offering recovery) - a freshly
                // constructed Document starts clean (isDirty()==false), so
                // this must be marked dirty explicitly or the tab would
                // silently look saved despite representing unsaved,
                // recovered content.
                recoveredSession->document().markDirty();
                static_cast<void>(workspace.adoptSession(std::move(recoveredSession)));
            }
            // A failed load (LoadError) leaves nothing adopted - the
            // autosave copy is still cleaned up below, same as a decline.
        }
        if (autosaveIndexPath) {
            std::error_code ec;
            std::filesystem::remove(candidate.autosaveTmpPath, ec);
            autosaveIndex.remove(candidate.hash);
            autosaveIndex.saveTo(*autosaveIndexPath);
        }
    }
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

    // WI-11: "最近使ったファイル" MRU list - same Normal-mode-only resolution
    // as searchHistory/settings/keyBindings above (no point paying
    // filesystem I/O on --measure-* harness invocations), same batched-
    // save-at-exit contract as searchHistory (see core::RecentFiles' own
    // header comment on why this is safe, unlike core::AutosaveIndex below).
    // (loadRecentFilesForLaunch() defined above, near this file's other
    // cognitive-complexity-motivated wWinMain extractions.)
    std::optional<std::filesystem::path> recentFilesPath;
    RecentFiles                          recentFiles = loadRecentFilesForLaunch(args, recentFilesPath);

    // WI-11: autosave index + directory - same Normal-mode-only resolution
    // as recentFiles above. Candidates gathered here (before `workspace`
    // exists, since scanning only needs autosaveDir/autosaveIndex) but only
    // actually prompted-for/adopted further below (processRecoverableAutoSaves()),
    // once `workspace` exists for adoptSession() to append to.
    // (resolveAutosaveStartupState() defined above.)
    AutosaveStartupState autosaveStartup = resolveAutosaveStartupState(args);

    // WI-14d: user-editable log-pattern files - same Normal-mode-only,
    // resolve-once-at-startup shape as autosaveStartup above.
    // (resolveLogPatternsStartupState() defined above.)
    LogPatternsStartupState logPatternsStartup = resolveLogPatternsStartupState(args);

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

    // WI-11: crash-recovery prompt loop - runs once, right after `workspace`
    // exists (adoptSession() needs it), for every candidate
    // resolveAutosaveStartupState() found above. (processRecoverableAutoSaves()
    // defined above, near this file's other cognitive-complexity-motivated
    // wWinMain extractions - see its own comment for the full behavior
    // description this replaced inline.)
    processRecoverableAutoSaves(workspace, autosaveStartup.recoverableAutoSaves, autosaveStartup.autosaveIndex,
                                autosaveStartup.autosaveIndexPath);

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
    // "JSON: Evaluate JSONPath" command's input overlay (WI-15e) - palette-
    // only (no CommandId/keybinding), same single-WC_EDIT shape as
    // gotoLineBar above.
    JsonPathBar jsonPathBar;
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
    // JSON/XML structure tree panel (Ctrl+Shift+J, WI-15c) - a single
    // WC_TREEVIEW, same shape as outlinePane above (see json_tree_pane.h's
    // class comment for why it is an independent class, not a refactor of
    // OutlinePane).
    JsonTreePane jsonTreePane;
    // CSV grid panel (Ctrl+Shift+G, WI-16c) - a single virtual-mode
    // WC_LISTVIEW. Unlike outlinePane/jsonTreePane above (260dip right-docked
    // strips), this replaces the entire client area between the tab strip
    // and the status bar (see csv_grid_pane.h's class comment for why).
    CsvGridPane csvGridPane;
    // Tab strip (WI-05 step 2) - a single WC_TABCONTROL, always visible
    // (unlike outlinePane above), docked full-width along the top edge. See
    // tab_bar.h's class comment.
    TabBar tabBar;
    // Status bar (WI-07 step4) - a single STATUSCLASSNAME control, always
    // visible, docked full-width along the BOTTOM edge (tabBar's
    // counterpart at the other edge). See status_bar.h's class comment.
    StatusBar statusBar;
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
    // Persisted user settings (WI-08) - font/tab width/line numbers/minimap
    // are applied to renderPipeline once below (before window.create(), same
    // reasoning as renderPipeline.setLanguage() further down: setters only
    // touch plain member state, no attach() required yet) and re-applied
    // live via the "Reload Settings" command palette entry
    // (normal_mode_wiring.cpp's buildCommandRegistry()). Same Normal-mode-
    // only resolution as searchHistoryPath above - no point paying
    // filesystem I/O on --measure-* harness invocations. Settings::saveTo()
    // is deliberately never called from main.cpp - there is no in-app
    // mutation path for `settings` in WI-08 (see build_plan.md's WI-08
    // section); saveTo() exists purely for the loadFrom()/saveTo() round-
    // trip unit test and for a future settings-editor UI.
    Settings                              settings;
    std::optional<std::filesystem::path> settingsPath;
    if (args.mode == LaunchMode::Normal) {
        settingsPath = resolveAppDataDir();
        if (settingsPath) {
            *settingsPath /= L"settings.json";
            settings = Settings::loadFrom(*settingsPath);
        }
    }
    // WI-10: user-configurable keybindings, same Normal-mode-only
    // resolution as settings/searchHistory above. keyBindings itself is
    // consumed twice below: once to build the initial accelerator table
    // (before window.create()) and once threaded into wireNormalMode() so
    // the manual per-keystroke handle*Key() chain (normal_mode_wiring.cpp)
    // and the "keybindings.reload"/"keybindings.preset.*" palette commands
    // can consult/replace it live.
    KeyBindings                           keyBindings = KeyBindings::forPreset(u"neomifes");
    std::optional<std::filesystem::path> keyBindingsPath;
    if (args.mode == LaunchMode::Normal) {
        keyBindingsPath = resolveAppDataDir();
        if (keyBindingsPath) {
            *keyBindingsPath /= L"keybindings.json";
            keyBindings = KeyBindings::loadFrom(*keyBindingsPath);
        }
    }
    // Free cursor mode (Phase 4b8e, simplified - see approved plan). Toggled
    // via the command palette ("Toggle Free Cursor Mode") - session-lifetime
    // UI state, not document state (see normal_mode_wiring.cpp's
    // handleFreeCursorRightArrow() comment), so it stays a wWinMain local
    // rather than an EditorSession member.
    bool freeCursorModeEnabled = false;
    // WI-06: true for the duration of an in-progress IME composition - same
    // "session-lifetime UI state, not document state" reasoning as
    // freeCursorModeEnabled just above (see
    // normal_mode_wiring.cpp's handleKeyDownEvent()/handleCharEvent()
    // comments for what this gates).
    bool imeComposing = false;
    // WI-15c: which EditorSession's still-in-flight async JSON-tree index
    // request should auto-populate jsonTreePane once it lands - UI-layer
    // state (jsonTreePane is one Workspace-wide widget, not per-session), so
    // it stays a wWinMain local here for the same reason freeCursorModeEnabled
    // above does, not an EditorSession member. Never dereferenced - compared
    // only by pointer VALUE against each async result's sessionToken, see
    // normal_mode_wiring.h's own comment on wireNormalMode()'s matching
    // parameter for the full safety argument.
    const void* jsonTreePanePendingSessionToken = nullptr;
    // WI-16c: same jsonTreePanePendingSessionToken reasoning above, for
    // csvGridPane - cleared on toggle-off/Escape AND on every tab switch/
    // document swap (syncViewForActiveSession()/resetViewAfterDocumentSwap(),
    // normal_mode_wiring.cpp), unlike jsonTreePanePendingSessionToken which
    // only needs the first two - see csv_grid_pane.h's class comment for why
    // csvGridPane's full-client-area placement requires the extra two clear
    // sites.
    const void* csvGridPanePendingSessionToken = nullptr;
    // WI-14b: empty until wireNormalMode()'s onDeferredInit lambda
    // emplace()s it with a real HWND - LogIndexWorker's constructor
    // requires one and starts a background std::thread immediately, so
    // (unlike renderPipeline below) there is no default-construct-here
    // shape available. Declared unconditionally (harmless/unused for
    // MeasureStartup/MeasureMemory/MeasureFrame launches, which never call
    // wireNormalMode() at all - same "harmless unused" treatment as
    // menuHandles above) rather than branching on `args.mode` here too.
    std::optional<LogIndexWorker> logIndexWorker;
    // WI-15b: same construction-timing reasoning as logIndexWorker above -
    // JsonTreeWorker's constructor also requires a real HWND and starts a
    // background std::thread immediately.
    std::optional<JsonTreeWorker> jsonTreeWorker;
    // WI-16b: same construction-timing reasoning as jsonTreeWorker above -
    // CsvModelWorker's constructor also requires a real HWND and starts a
    // background std::thread immediately.
    std::optional<CsvModelWorker> csvModelWorker;
    // WI-17b: same construction-timing reasoning as jsonTreeWorker above -
    // GitDiffWorker's constructor also requires a real HWND and starts a
    // background std::thread immediately.
    std::optional<GitDiffWorker> gitDiffWorker;

    MainWindow window;
    MainWindowConfig cfg{};
    RenderPipeline renderPipeline;
    // WI-08: applied once here, before attach()/window.create() - setters
    // only touch plain member state (same reasoning as setLanguage() below),
    // so this is safe pre-attach. Harmless no-op for MeasureStartup/
    // MeasureMemory/MeasureFrame launches too: `settings` stays
    // default-constructed there (settingsPath is only resolved in Normal
    // mode above), and Settings' defaults match RenderPipeline's own
    // pre-WI-08 hardcoded values exactly (see settings.h).
    renderPipeline.setFontSettings(settings.fontFamily, settings.fontSizeDips);
    renderPipeline.setTabWidth(settings.tabWidth);
    renderPipeline.setLineNumbersVisible(settings.showLineNumbers);
    renderPipeline.setMinimapVisible(settings.showMinimap);
    renderPipeline.setTheme(parseThemeKind(settings.themeName));

    // WI-10: moved here (was after window.create() below) and made
    // non-const - CreateAcceleratorTableW needs no HWND, so building it
    // before the window exists is harmless, and wireNormalMode()'s
    // "keybindings.reload"/"keybindings.preset.*" palette commands need a
    // mutable reference to reassign at runtime. HandleGuard::operator=
    // (HandleGuard&&) destroys the old HACCEL before taking the new one, so
    // later reassignment is a safe, leak-free live swap (see
    // command_dispatch.h's buildAcceleratorTable() comment). Built
    // unconditionally (harmless/unused for measurement-mode launches, which
    // never generate real keyboard input) rather than branching on
    // `args.mode` here too. AcceleratorTableHandle's falsy state
    // (construction failure) is handled by runMessageLoop() itself (see its
    // own comment).
    neomifes::platform::AcceleratorTableHandle accelTable = neomifes::app::buildAcceleratorTable(keyBindings);

    // WI-11: default-constructed {nullptr, nullptr} for measurement-mode
    // launches (never assigned - MenuBarHandles::menuBar stays nullptr,
    // same "harmless unused" treatment as menuBar's own MainWindowConfig
    // default). Only the Normal-mode branch below assigns a real value, via
    // buildMenuBar(recentFiles).
    MenuBarHandles menuHandles{};
    // WI-11: bundles the 3 refs every autosave call site needs together -
    // see command_dispatch.h's AutosaveContext for why. Constructed here
    // (not deferred into the Normal-mode branch below) since it holds a
    // REFERENCE into autosaveStartup (resolveAutosaveStartupState() above),
    // already fully resolved above; nothing about it depends on which mode
    // is being wired.
    AutosaveContext autosave{.autosaveDir = autosaveStartup.autosaveDir,
                             .index        = autosaveStartup.autosaveIndex,
                             .indexPath    = autosaveStartup.autosaveIndexPath};

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
        // WI-11: built here (not inside wireNormalMode()) since MainWindowConfig::
        // menuBar must be set BEFORE window.create() below - buildMenuBar()
        // needs no HWND (same "no window required yet" reasoning
        // accelTable's construction above already relies on).
        menuHandles     = neomifes::app::buildMenuBar(recentFiles);
        cfg.menuBar     = menuHandles.menuBar;
        // WI-05 step 1: passes workspace itself (not workspace.active()) -
        // wireNormalMode() now resolves the active session fresh wherever a
        // stored/later-invoked callback needs it, so a future tab switch is
        // reflected everywhere without this call site changing again.
        wireNormalMode(cfg, window, renderPipeline, workspace, hInstance, findBar, commandPalette,
                       gotoLineBar, jsonPathBar, grepBar, grepState, searchHistory, outlinePane, tabBar, statusBar,
                       settings, settingsPath, keyBindings, keyBindingsPath, accelTable,
                       freeCursorModeEnabled, isDraggingMinimap, imeComposing, recentFiles, menuHandles,
                       autosave, logIndexWorker, logPatternsStartup.userLogPatterns,
                       logPatternsStartup.logPatternsDir, jsonTreeWorker, csvModelWorker, jsonTreePane,
                       jsonTreePanePendingSessionToken, csvGridPane, csvGridPanePendingSessionToken,
                       gitDiffWorker);
        // Phase 7b/7d: reflect the startup document's language before the
        // first paint - attach() itself happens later inside onDeferredInit,
        // but setLanguage() only touches plain member state, so it's safe to
        // call before RenderPipeline is attached.
        renderPipeline.setLanguage(workspace.active().language());
    }

    if (!window.create(hInstance, cfg)) {
        return 1;
    }

    const int rc = runMessageLoop(window.hwnd(), accelTable);

    // Persist search history once, at clean exit (not after every search) -
    // a crash loses only the current session's newly-recorded entries, an
    // acceptable trade-off against writing to disk on every keystroke-driven
    // search (Phase 5c5).
    if (searchHistoryPath) {
        searchHistory.saveTo(*searchHistoryPath);
    }
    // WI-11: same batched-at-exit contract as searchHistory above (see
    // core::RecentFiles' own header comment on why this - unlike
    // core::AutosaveIndex - doesn't need to be written on every record()
    // call).
    if (recentFilesPath) {
        recentFiles.saveTo(*recentFilesPath);
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
