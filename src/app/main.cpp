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
//                                 synthesizes a large one - see
//                                 synthesizeMeasurementDocument().
//
// Command-line options (real launches only):
//   --open <path>  Load a UTF-8 file into the Document at startup so its
//                  content renders. A missing/invalid file falls back to an
//                  empty Document rather than blocking startup. File->Open
//                  dialog / recent-files UI is a later phase.
//
// Search Engine integration arrives in a later phase.

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "neomifes/app/editor_input.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/platform/perf_clock.h"
#include "neomifes/platform/process_metrics.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/ui/main_window.h"

#include "frame_profile.h"
#include "startup_profile.h"

namespace {

using neomifes::app::FrameProfile;
using neomifes::app::StartupProfile;
using neomifes::core::CommandDispatcher;
using neomifes::core::SelectionModel;
using neomifes::core::Viewport;
using neomifes::document::Document;
using neomifes::document::LoadError;
using neomifes::document::LoadResult;
using neomifes::document::TextRange;
using neomifes::platform::currentProcessMemory;
using neomifes::platform::KernelHandle;
using neomifes::platform::PerfClock;
using neomifes::render::RenderPipeline;
using neomifes::ui::kWindowClassName;
using neomifes::ui::MainWindow;
using neomifes::ui::MainWindowConfig;

// Fixed name (not a random GUID) so every launch of this build targets the
// same mutex. "Local\" keeps it session-scoped rather than machine-global.
// A string-literal-initialized C array decays to const wchar_t* for free at
// every call site (CreateMutexW wants LPCWSTR); std::array would need
// .data() everywhere for no safety benefit on a fixed, never-indexed literal.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays)
constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\NeoMIFES_SingleInstance_9F1B2C3D_4E5F_4A6B_8C7D_1234567890AB";

// Named-mutex single-instance check (basic_design.md sec.2.3). Only the
// detection + "activate the existing window" half is implemented here — the
// command-line-handoff-via-IPC half described in basic_design.md requires a
// SessionManager that does not exist yet (Phase 4+), so it is deliberately
// not built speculatively. Returns true if THIS process should proceed to
// create its own window; false if an existing instance was found and
// activated instead (caller should exit without creating a window).
//
// `mutexHolder` receives ownership of the mutex handle so it stays alive for
// the process lifetime (a second launch must still detect this one).
[[nodiscard]] bool claimSingleInstance(KernelHandle& mutexHolder) noexcept {
    HANDLE h = ::CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
    mutexHolder = KernelHandle{h};
    if (h == nullptr) {
        // Mutex creation failing is not fatal to launching normally - treat
        // as "no other instance detected" rather than blocking startup.
        return true;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = ::FindWindowW(kWindowClassName, nullptr);
        if (existing != nullptr) {
            if (::IsIconic(existing)) {
                ::ShowWindow(existing, SW_RESTORE);
            }
            ::SetForegroundWindow(existing);
        }
        return false;
    }
    return true;
}

enum class LaunchMode : std::uint8_t {
    Normal,
    MeasureStartup,
    MeasureMemory,
    MeasureFrame,
};

struct LaunchArgs {
    LaunchMode                           mode = LaunchMode::Normal;
    std::filesystem::path                outputPath;
    // Real-launch-only convenience flag to prove Document content actually
    // renders (Phase 3b). File->Open dialog / recent-files UI is out of
    // scope here - this is the smallest useful slice.
    std::optional<std::filesystem::path> openPath;
};

// Very small hand-rolled parser. We deliberately avoid CommandLineToArgvW-derived
// heap allocations on the fast path when no measurement flags are present.
LaunchArgs parseArgs() noexcept {
    LaunchArgs args;
    int argc      = 0;
    LPWSTR* argv  = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return args;
    }
    for (int i = 1; i < argc; ++i) {
        const std::wstring_view a = argv[i];
        if ((a == L"--measure-startup" || a == L"--measure-memory" || a == L"--measure-frame") &&
            (i + 1) < argc) {
            if (a == L"--measure-startup") {
                args.mode = LaunchMode::MeasureStartup;
            } else if (a == L"--measure-memory") {
                args.mode = LaunchMode::MeasureMemory;
            } else {
                args.mode = LaunchMode::MeasureFrame;
            }
            args.outputPath = argv[i + 1];
            ++i;
        } else if (a == L"--open" && (i + 1) < argc) {
            args.openPath = argv[i + 1];
            ++i;
        }
    }
    // LocalFree takes HLOCAL (== HANDLE == void*); casting LPWSTR* directly
    // is a multi-level pointer conversion that clang-tidy flags. Route via
    // an explicit reinterpret_cast to acknowledge the intent.
    ::LocalFree(reinterpret_cast<HLOCAL>(argv));
    return args;
}

// Enable Per-Monitor V2 DPI awareness. Falls back silently on older Win10 builds.
void enableHighDpi() noexcept {
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) {
        return;
    }
    auto setCtx = reinterpret_cast<SetContextFn>(
        ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setCtx != nullptr) {
        setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

// No logging engine exists yet (basic_design.md sec.6.5 is a later phase);
// this is a deliberate, narrowly-scoped stopgap for render-attach/resize
// failures rather than solving logging prematurely. describe()'s output is
// documented ASCII-only, so OutputDebugStringA (not the W variant) is fine.
void debugLogRenderError(const char* what, const neomifes::render::RenderError& err) noexcept {
#ifndef NDEBUG
    const std::string msg = std::string(what) + ": " + neomifes::render::describe(err) + "\n";
    ::OutputDebugStringA(msg.c_str());
#else
    (void)what;
    (void)err;
#endif
}

// Same non-fatal, debug-only logging shape as debugLogRenderError - a failed
// --open falls back to an empty Document rather than blocking startup.
void debugLogLoadError(const std::filesystem::path& path, LoadError err) noexcept {
#ifndef NDEBUG
    const std::wstring msg = L"loadUtf8File failed for " + path.wstring() +
                             L" (LoadError=" + std::to_wstring(static_cast<int>(err)) + L")\n";
    ::OutputDebugStringW(msg.c_str());
#else
    (void)path;
    (void)err;
#endif
}

int runMessageLoop() noexcept {
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// Real launches only (checked by the caller). A missing/invalid --open path
// falls back to an empty Document rather than blocking startup - pulled out
// of wWinMain to keep its cognitive complexity down.
Document loadStartupDocument(const LaunchArgs& args) {
    Document document;
    if (!args.openPath) {
        return document;
    }
    auto loadResult = neomifes::document::loadUtf8File(*args.openPath);
    if (auto* result = std::get_if<LoadResult>(&loadResult)) {
        document = std::move(*result->document);
    } else {
        debugLogLoadError(*args.openPath, std::get<LoadError>(loadResult));
    }
    return document;
}

// --measure-frame without --open (e.g. the CI PoC step, which passes no
// --open so it stays self-contained with no repo fixture-file dependency)
// synthesizes one large document instead. A single insertText() call rather
// than a per-line loop avoids an O(n) PieceTable::insert loop cost from
// dominating the harness's own setup time.
constexpr std::uint64_t kSyntheticLineCount = 50'000;

Document synthesizeMeasurementDocument() {
    constexpr std::u16string_view kLineText = u"synthetic line for --measure-frame scrolling\n";
    std::u16string text;
    text.reserve(kLineText.size() * kSyntheticLineCount);
    for (std::uint64_t i = 0; i < kSyntheticLineCount; ++i) {
        text += kLineText;
    }
    Document document;
    document.insertText(0, text);
    return document;
}

// Decides which Document a launch needs: --open's file (Normal or
// MeasureFrame), a synthesized large document (MeasureFrame without --open),
// or an unused empty one (MeasureStartup/MeasureMemory don't render at all).
// `syntheticLineCountOut` is set only when the synthetic path was taken, for
// FrameProfile reporting. Pulled out of wWinMain to keep its cognitive
// complexity down (same rationale as loadStartupDocument() above).
Document prepareDocument(const LaunchArgs& args, std::uint64_t& syntheticLineCountOut) {
    syntheticLineCountOut = 0;
    if (args.mode == LaunchMode::MeasureFrame && !args.openPath) {
        syntheticLineCountOut = kSyntheticLineCount;
        return synthesizeMeasurementDocument();
    }
    if (args.mode == LaunchMode::Normal || args.mode == LaunchMode::MeasureFrame) {
        return loadStartupDocument(args);
    }
    return Document{};
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
// complexity reason as loadStartupDocument()/prepareDocument() above.
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

// Bridges core::Viewport/SelectionModel state into RenderPipeline and
// requests a repaint - the shared tail of onKeyDown/onChar/onMouseWheel/
// onMouseDown below (Phase 4b1/4b2). RenderPipeline stays core-agnostic
// (setTopLine/setCaretPosition/setSelectionRange take plain document
// types), so this glue lives here in the app layer rather than in either
// core or render.
void syncRenderStateAndInvalidate(HWND hwnd, RenderPipeline& renderPipeline,
                                  const SelectionModel& selection, const Viewport& viewport) {
    renderPipeline.setTopLine(viewport.topLine());
    const auto& cursor = selection.primaryCursor();
    renderPipeline.setCaretPosition(cursor.position);
    renderPipeline.setSelectionRange(
        TextRange{.start = std::min(cursor.position, cursor.anchor),
                 .end     = std::max(cursor.position, cursor.anchor)});
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Picks which click interpretation applies to a hit-tested WM_LBUTTONDOWN and
// applies it. Pulled out of wireNormalMode's onMouseDown lambda to keep that
// function's cognitive complexity down (same rationale as
// loadStartupDocument()/prepareDocument() above) - Phase 4b5b's altDown
// branch pushed the inline version over clang-tidy's threshold.
bool dispatchMouseDown(neomifes::document::TextPos hit, bool shiftDown, bool altDown, int clickCount,
                       SelectionModel& selectionModel, Viewport& viewport, const Document& document) {
    // Alt+click always adds a cursor, regardless of click count -
    // Alt+double/triple-click's meaning is left undefined rather than
    // guessed at. Otherwise (Phase 4b4) click count dispatches to word/line
    // selection instead of plain cursor placement.
    if (altDown) {
        return neomifes::app::handleAltClick(hit, selectionModel, viewport, document);
    }
    if (clickCount >= 3) {
        return neomifes::app::handleTripleClick(hit, selectionModel, viewport, document);
    }
    if (clickCount == 2) {
        return neomifes::app::handleDoubleClick(hit, selectionModel, viewport, document);
    }
    return neomifes::app::handleMouseDown(hit, shiftDown, selectionModel, viewport, document);
}

// Real launches only - deferred so it never affects firstPaintNs timing
// (ADR-009). If attach() fails, the window simply keeps the GDI placeholder
// forever; there is no retry policy.
void wireNormalMode(MainWindowConfig& cfg, MainWindow& window, RenderPipeline& renderPipeline,
                    Document& document, CommandDispatcher& dispatcher, SelectionModel& selectionModel,
                    Viewport& viewport) {
    cfg.onDeferredInit = [&window, &renderPipeline, &document](HWND hwnd) {
        const auto attached = renderPipeline.attach(hwnd);
        if (!attached) {
            debugLogRenderError("RenderPipeline::attach", attached.error());
            return;
        }
        renderPipeline.setDocument(&document);
        window.setPaintHandler([&renderPipeline](HWND) {
            const auto rendered = renderPipeline.render();
            if (!rendered) {
                debugLogRenderError("RenderPipeline::render", rendered.error());
            }
        });
        ::InvalidateRect(hwnd, nullptr, FALSE);
    };
    cfg.onResize = [&renderPipeline](HWND, std::uint32_t w, std::uint32_t h, float dpiScale) {
        if (!renderPipeline.isAttached()) {
            return;
        }
        const auto resized = renderPipeline.resize(w, h, dpiScale);
        if (!resized) {
            debugLogRenderError("RenderPipeline::resize", resized.error());
        }
    };
    cfg.onKeyDown = [&dispatcher, &selectionModel, &viewport, &document, &renderPipeline](
                        HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown) {
        const bool changed = neomifes::app::handleKeyDown(vkCode, shiftDown, ctrlDown, dispatcher,
                                                          selectionModel, viewport, document);
        if (changed) {
            syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
        }
    };
    cfg.onChar = [&dispatcher, &selectionModel, &viewport, &document, &renderPipeline](
                     HWND hwnd, wchar_t ch) {
        const bool changed =
            neomifes::app::handleChar(ch, dispatcher, selectionModel, viewport, document);
        if (changed) {
            syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
        }
    };
    cfg.onMouseWheel = [&viewport, &selectionModel, &renderPipeline](HWND hwnd, short wheelDelta) {
        viewport.scrollTo(neomifes::app::applyMouseWheelScroll(wheelDelta, viewport.topLine()));
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    };
    cfg.onMouseDown = [&selectionModel, &viewport, &document, &renderPipeline](
                          HWND hwnd, std::int32_t x, std::int32_t y, bool shiftDown, bool altDown,
                          int clickCount) {
        const auto hit = renderPipeline.hitTest(x, y);
        if (!hit) {
            return;
        }
        // Drag (onMouseDrag below) is unaffected by any of dispatchMouseDown's
        // branches - it always calls handleMouseDown directly regardless of
        // what started the drag.
        const bool changed =
            dispatchMouseDown(*hit, shiftDown, altDown, clickCount, selectionModel, viewport, document);
        if (changed) {
            syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
        }
    };
    cfg.onMouseDrag = [&selectionModel, &viewport, &document, &renderPipeline](
                          HWND hwnd, std::int32_t x, std::int32_t y) {
        const auto hit = renderPipeline.hitTest(x, y);
        if (!hit) {
            return;
        }
        // A drag always extends from whatever anchor onMouseDown established
        // (shiftDown=true), regardless of whether the drag started with
        // Shift held (Phase 4b3) - see plan doc rationale.
        const bool changed =
            neomifes::app::handleMouseDown(*hit, /*shiftDown=*/true, selectionModel, viewport, document);
        if (changed) {
            syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
        }
    };
}

// Reuses onDeferredInit exactly like the Normal path does for real
// rendering - no new MainWindow hooks, no mouse/keyboard plumbing. The
// entire synthetic-scroll measurement loop runs synchronously inside this
// one callback, then closes the window.
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

    // Declared before window/renderPipeline so it outlives both (reverse
    // destruction order) - RenderPipeline::setDocument() below hands out a
    // non-owning pointer that must not dangle while the message loop runs.
    std::uint64_t syntheticLineCountUsed = 0;
    Document      document               = prepareDocument(args, syntheticLineCountUsed);
    FrameProfile  frameProfile{};

    // Editor Core state (Phase 4b1) - Normal-mode-only in practice (only
    // wireNormalMode's hooks ever touch these), but declared unconditionally
    // like `document` above since CommandDispatcher must be constructed
    // with a valid Document& regardless of launch mode.
    SelectionModel    selectionModel{0};
    CommandDispatcher dispatcher{document, selectionModel};
    Viewport          viewport;

    MainWindow window;
    MainWindowConfig cfg{};
    RenderPipeline renderPipeline;

    // Each mode's hook wiring lives in its own function (see definitions
    // above) - ordering matters for MeasureStartup/MeasureMemory (window
    // created -> first paint), matters not at all for the others.
    if (args.mode == LaunchMode::MeasureStartup || args.mode == LaunchMode::MeasureMemory) {
        wireMeasureStartupOrMemoryMode(cfg, profile, window);
    } else if (args.mode == LaunchMode::MeasureFrame) {
        wireMeasureFrameMode(cfg, window, renderPipeline, document, frameProfile,
                             syntheticLineCountUsed);
    } else {
        wireNormalMode(cfg, window, renderPipeline, document, dispatcher, selectionModel, viewport);
    }

    if (!window.create(hInstance, cfg)) {
        return 1;
    }

    const int rc = runMessageLoop();

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
