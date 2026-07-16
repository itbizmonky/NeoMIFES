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

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/platform/perf_clock.h"
#include "neomifes/platform/process_metrics.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/ui/main_window.h"

#include "startup_profile.h"

namespace {

using neomifes::app::StartupProfile;
using neomifes::document::Document;
using neomifes::document::LoadError;
using neomifes::document::LoadResult;
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
        if ((a == L"--measure-startup" || a == L"--measure-memory") && (i + 1) < argc) {
            args.mode       = (a == L"--measure-startup")
                                  ? LaunchMode::MeasureStartup
                                  : LaunchMode::MeasureMemory;
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
    Document document =
        args.mode == LaunchMode::Normal ? loadStartupDocument(args) : Document{};

    MainWindow window;
    MainWindowConfig cfg{};
    RenderPipeline renderPipeline;

    // In measurement mode, capture both timing markers via hooks so their
    // ordering matches the actual UI event sequence (window created ->
    // first paint), rather than the order in which we return from create().
    if (args.mode != LaunchMode::Normal) {
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
    } else {
        // Real launches only - deferred so it never affects firstPaintNs
        // timing (ADR-009). If attach() fails, the window simply keeps the
        // GDI placeholder forever; Phase 3a has no retry policy.
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
    }

    if (!window.create(hInstance, cfg)) {
        return 1;
    }

    const int rc = runMessageLoop();

    if (args.mode != LaunchMode::Normal) {
        profile.measuredExitNs = PerfClock::nanosSinceProcessStart();
        // Failure to write is fatal for the PoC — surface it via non-zero exit.
        if (!profile.writeJson(args.outputPath)) {
            return 2;
        }
    }
    return rc;
}
