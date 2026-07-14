// NeoMIFES - Phase 1 entry point.
//
// Responsibilities:
//   1. Mark process start time (QPC) as early as possible.
//   2. Enable Per-Monitor V2 DPI awareness before any HWND is created.
//   3. Create the top-level MainWindow (Win32 skeleton, GDI paint).
//   4. Run the message loop.
//
// Command-line modes (used by PoC tests, disabled in normal launches):
//   --measure-startup <out.json>  Record startup timings + memory then exit
//                                 immediately after first paint.
//   --measure-memory  <out.json>  Same, but focus on the memory snapshot.
//                                 (Currently identical output — kept separate
//                                 for future divergence.)
//
// Real editor features (Document/Rendering/Search) arrive in later phases.

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <string_view>

#include "neomifes/platform/perf_clock.h"
#include "neomifes/platform/process_metrics.h"
#include "neomifes/ui/main_window.h"

#include "startup_profile.h"

namespace {

using neomifes::app::StartupProfile;
using neomifes::platform::currentProcessMemory;
using neomifes::platform::PerfClock;
using neomifes::ui::MainWindow;
using neomifes::ui::MainWindowConfig;

enum class LaunchMode : std::uint8_t {
    Normal,
    MeasureStartup,
    MeasureMemory,
};

struct LaunchArgs {
    LaunchMode            mode = LaunchMode::Normal;
    std::filesystem::path outputPath;
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

int runMessageLoop() noexcept {
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
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

    enableHighDpi();

    MainWindow window;
    MainWindowConfig cfg{};

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
