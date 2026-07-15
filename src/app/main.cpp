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

#include "neomifes/platform/handle_guard.h"
#include "neomifes/platform/perf_clock.h"
#include "neomifes/platform/process_metrics.h"
#include "neomifes/ui/main_window.h"

#include "startup_profile.h"

namespace {

using neomifes::app::StartupProfile;
using neomifes::platform::currentProcessMemory;
using neomifes::platform::KernelHandle;
using neomifes::platform::PerfClock;
using neomifes::ui::kWindowClassName;
using neomifes::ui::MainWindow;
using neomifes::ui::MainWindowConfig;

// Fixed name (not a random GUID) so every launch of this build targets the
// same mutex. "Local\" keeps it session-scoped rather than machine-global.
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

    // Single-instance check applies to real user launches only. --measure-startup
    // / --measure-memory are PoC/CI harness invocations that intentionally spawn
    // fresh isolated processes for benchmarking; gating those on this check would
    // make CI runs flaky if two measurement runs ever overlapped.
    KernelHandle singleInstanceMutex;
    if (args.mode == LaunchMode::Normal && !claimSingleInstance(singleInstanceMutex)) {
        return 0;
    }

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
