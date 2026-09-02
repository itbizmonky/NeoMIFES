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

#include "neomifes/app/command_dispatch.h"
#include "neomifes/app/editor_session.h"
#include "neomifes/app/launch_setup.h"
#include "neomifes/app/session_manager.h"
#include "neomifes/app/theme_settings.h"
#include "neomifes/app/workspace.h"
#include "neomifes/core/key_bindings.h"
#include "neomifes/core/settings.h"
#include "neomifes/document/document.h"
#include "neomifes/git/git_init.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/platform/perf_clock.h"
#include "neomifes/platform/process_metrics.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/ui/main_window.h"

#include "frame_profile.h"
#include "startup_profile.h"

namespace {

using neomifes::app::claimSingleInstance;
using neomifes::app::debugLogRenderError;
using neomifes::app::DocumentFileState;
using neomifes::app::enableHighDpi;
using neomifes::app::FrameProfile;
using neomifes::app::initCommonControls;
using neomifes::app::LaunchArgs;
using neomifes::app::LaunchMode;
using neomifes::app::parseArgs;
using neomifes::app::parseThemeKind;
using neomifes::app::prepareDocument;
using neomifes::app::SessionManager;
using neomifes::app::StartupProfile;
using neomifes::app::Workspace;
using neomifes::core::KeyBindings;
using neomifes::core::Settings;
using neomifes::document::Document;
using neomifes::platform::currentProcessMemory;
using neomifes::platform::KernelHandle;
using neomifes::platform::PerfClock;
using neomifes::render::RenderPipeline;
using neomifes::ui::MainWindow;
using neomifes::ui::MainWindowConfig;

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
// palette command reassigning the live accelerator table mid-run takes
// effect on the very next keystroke, no restart needed.
//
// WI-20a: no longer takes a fixed HWND - resolves the correct top-level
// owner PER MESSAGE via GetAncestor(msg.hwnd, GA_ROOT) instead. A fixed
// HWND was correct when exactly one window could ever exist; with
// SessionManager able to hold several, a message destined for the SECOND
// (or later) window's own hwnd (or one of its child controls) would
// otherwise be translated against the WRONG window's ancestor, silently
// breaking every accelerator (Ctrl+S, Ctrl+O, tab switching, ...) in every
// window except whichever one happened to be the fixed hwnd. Single-window
// callers (measurement modes) are unaffected: GetAncestor on a lone
// top-level window's own hwnd (or its only child controls) returns that
// same hwnd, identical to the old fixed-hwnd behavior.
int runMessageLoop(const neomifes::platform::AcceleratorTableHandle& accelTable) noexcept {
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Not `const HWND` - HWND is a pointer typedef, so `const HWND`
        // would const-qualify the pointer itself rather than what it points
        // to (misc-misplaced-const); harmless either way since this local
        // is never reassigned, but clang-tidy flags it as a warning-turned-
        // error under this project's build config.
        HWND root = ::GetAncestor(msg.hwnd, GA_ROOT);
        if (auto* const haccel = accelTable.get();
            haccel != nullptr && root != nullptr && ::TranslateAcceleratorW(root, haccel, &msg) != 0) {
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

// WI-17c: this codebase's own missing-wire bug, found by this WI's
// dogfooding step - neomifes::git::initializeLibgit2() (WI-17a) had never
// actually been called from anywhere in src/app/, only from test fixtures'
// own SetUp() methods. Without it, every GitRepository::discover() call in
// the real running app silently failed (git_repository_open_ext() against an
// uninitialized libgit2 runtime), so Git diff markers never rendered in any
// real session - a pure plumbing gap, not a design flaw in WI-17a/b's own
// contract (git_repository.h's own header comment already documented this
// precondition correctly; nothing downstream ever satisfied it). RAII rather
// than a call paired with every one of wWinMain()'s several return points
// below (measurement-mode early exits, message-loop failure) - shutdownLibgit2()
// is documented safe to call unconditionally (a no-op if init was never
// called or returned false), so a scope guard covers every exit path without
// needing to track init's own success/failure here.
struct Libgit2Guard {
    Libgit2Guard()  = default;
    ~Libgit2Guard() { neomifes::git::shutdownLibgit2(); }

    // A copy/move would each independently call shutdownLibgit2() at
    // destruction (double-teardown) - not reachable today (the single
    // wWinMain() local below is never copied/moved), but deleted explicitly
    // per this codebase's Rule-of-Five convention rather than left to
    // clang-tidy's cppcoreguidelines-special-member-functions to flag.
    Libgit2Guard(const Libgit2Guard&)            = delete;
    Libgit2Guard& operator=(const Libgit2Guard&) = delete;
    Libgit2Guard(Libgit2Guard&&)                 = delete;
    Libgit2Guard& operator=(Libgit2Guard&&)      = delete;
};

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
    if (args.mode == LaunchMode::Normal && !claimSingleInstance(singleInstanceMutex, args)) {
        return 0;
    }

    enableHighDpi();
    // Phase 5b3a: defensive, see initCommonControls()'s comment. Cheap and
    // harmless even on modes that never create a FindBar (Measure*).
    initCommonControls();

    // WI-17c bugfix: see Libgit2Guard's own comment above for why this call
    // (and its RAII shutdown counterpart) was missing from every real run of
    // this app until now. A `false` return is not gated on anywhere below -
    // no established pattern for a multi-layer "Git features disabled" state
    // exists in this codebase yet (GitDiffWorker's construction and the
    // command-palette "Git: Refresh Diff Markers" entry are both
    // unconditional), and per git_init.h's own header comment,
    // git_libgit2_init() only sets up in-process global state (no I/O, no
    // network - USE_HTTPS/USE_SSH are both OFF per ADR-022), so a failure
    // here would be exceptionally unlikely in practice. Same "best-effort,
    // no retry policy" treatment RenderPipeline::attach() failure already
    // gets elsewhere in this file, not a claim that every later
    // neomifes::git call remains safe if this returned false.
    [[maybe_unused]] const bool libgit2Initialized = neomifes::git::initializeLibgit2();
    const Libgit2Guard libgit2Guard;

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

    // WI-20a: a real (Normal-mode) launch delegates entirely to
    // SessionManager from here on - it owns every piece of app-wide state
    // (Settings/KeyBindings+accelerator table/RecentFiles/SearchHistory/the
    // autosave index) that used to be a wWinMain local, plus the one
    // EditorWindow this launch creates (adoptFirstWindow() reuses
    // `document`/`fileState`/`currentDocumentPath` already produced by
    // prepareDocument() above, rather than reloading). See
    // session_manager.h's own header comment for the full design (why one
    // process/one SessionManager/many windows is safe with no additional
    // locking - basic_design.md sec.2.3).
    if (args.mode == LaunchMode::Normal) {
        SessionManager sessionManager(hInstance);
        if (!sessionManager.adoptFirstWindow(std::move(document), fileState, currentDocumentPath)) {
            return 1;
        }
        const int rc = runMessageLoop(sessionManager.acceleratorTable());
        sessionManager.persistOnExit();
        return rc;
    }

    // Measurement-mode launches (PoC/CI harnesses, --measure-startup/
    // --measure-memory/--measure-frame) never go through SessionManager -
    // they need exactly one window, never load any %APPDATA%-persisted
    // state, and must keep their existing timing contract untouched by this
    // WI. `document` above is either --open's file, a synthesized large
    // document (MeasureFrame with no --open), or empty, per
    // prepareDocument()'s own mode handling.
    Workspace workspace(std::move(document), fileState, currentDocumentPath);

    MainWindow window;
    MainWindowConfig cfg{};
    RenderPipeline renderPipeline;
    // `settings` stays default-constructed (measurement modes never load
    // settings.json) - its defaults match RenderPipeline's own pre-WI-08
    // hardcoded values exactly (see settings.h), so applying them below is
    // a harmless no-op, kept only for parity with the real-launch path.
    const Settings settings;
    renderPipeline.setFontSettings(settings.fontFamily, settings.fontSizeDips);
    renderPipeline.setTabWidth(settings.tabWidth);
    renderPipeline.setLineNumbersVisible(settings.showLineNumbers);
    renderPipeline.setMinimapVisible(settings.showMinimap);
    renderPipeline.setTheme(parseThemeKind(settings.themeName));

    // Measurement modes never load keybindings.json either - a default
    // HACCEL is built purely so runMessageLoop() has something to call
    // TranslateAcceleratorW with; harmless/unused since these launches
    // never generate real keyboard input.
    const KeyBindings keyBindings = KeyBindings::forPreset(u"neomifes");
    const neomifes::platform::AcceleratorTableHandle accelTable =
        neomifes::app::buildAcceleratorTable(keyBindings);

    // Each mode's hook wiring lives in its own function (see definitions
    // above) - ordering matters for MeasureStartup/MeasureMemory (window
    // created -> first paint), matters not at all for MeasureFrame.
    if (args.mode == LaunchMode::MeasureStartup || args.mode == LaunchMode::MeasureMemory) {
        wireMeasureStartupOrMemoryMode(cfg, profile, window);
    } else {
        wireMeasureFrameMode(cfg, window, renderPipeline, workspace.active().document(), frameProfile,
                             syntheticLineCountUsed);
    }

    if (!window.create(hInstance, cfg)) {
        return 1;
    }

    const int rc = runMessageLoop(accelTable);

    if (args.mode == LaunchMode::MeasureStartup || args.mode == LaunchMode::MeasureMemory) {
        profile.measuredExitNs = PerfClock::nanosSinceProcessStart();
        // Failure to write is fatal for the PoC — surface it via non-zero exit.
        if (!profile.writeJson(args.outputPath)) {
            return 2;
        }
    } else if (!frameProfile.writeJson(args.outputPath)) {
        return 2;
    }
    return rc;
}
