#include "neomifes/app/launch_setup.h"

#include <commctrl.h>
#include <shellapi.h>

#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "neomifes/document/file_loader.h"
#include "neomifes/ui/main_window.h"

namespace neomifes::app {

namespace {

using document::Document;
using document::LoadError;
using document::LoadResult;
using platform::KernelHandle;

// Fixed name (not a random GUID) so every launch of this build targets the
// same mutex. "Local\" keeps it session-scoped rather than machine-global.
// A string-literal-initialized C array decays to const wchar_t* for free at
// every call site (CreateMutexW wants LPCWSTR); std::array would need
// .data() everywhere for no safety benefit on a fixed, never-indexed literal.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays)
constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\NeoMIFES_SingleInstance_9F1B2C3D_4E5F_4A6B_8C7D_1234567890AB";

// Same non-fatal, debug-only logging shape as
// neomifes::app::debugLogRenderError() (normal_mode_wiring.cpp) - a failed
// --open falls back to an empty Document rather than blocking startup.
void debugLogLoadError(const std::filesystem::path& path, LoadError err) noexcept {
#ifndef NDEBUG
    const std::wstring msg = L"loadFile failed for " + path.wstring() +
                             L" (LoadError=" + std::to_wstring(static_cast<int>(err)) + L")\n";
    ::OutputDebugStringW(msg.c_str());
#else
    (void)path;
    (void)err;
#endif
}

// Real launches only (checked by the caller). A missing/invalid --open path
// falls back to an empty Document rather than blocking startup.
Document loadStartupDocument(const LaunchArgs& args, DocumentFileState& fileStateOut,
                             std::optional<std::filesystem::path>& currentDocumentPathOut) {
    Document document;
    if (!args.openPath) {
        return document;
    }
    auto loadResult = neomifes::document::loadFile(*args.openPath);
    if (auto* result = std::get_if<LoadResult>(&loadResult)) {
        document                = std::move(*result->document);
        fileStateOut.encoding   = result->detectedEncoding;
        fileStateOut.lineEnding = result->lineEnding;
        fileStateOut.writeBom   = result->hadBom;
        currentDocumentPathOut  = *args.openPath;
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

}  // namespace

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

bool claimSingleInstance(KernelHandle& mutexHolder) noexcept {
    HANDLE h = ::CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
    mutexHolder = KernelHandle{h};
    if (h == nullptr) {
        // Mutex creation failing is not fatal to launching normally - treat
        // as "no other instance detected" rather than blocking startup.
        return true;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = ::FindWindowW(neomifes::ui::kWindowClassName, nullptr);
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

void initCommonControls() noexcept {
    // ICC_TREEVIEW_CLASSES added for OutlinePane's WC_TREEVIEW (Phase 7g) -
    // this codebase's first control outside ICC_STANDARD_CLASSES.
    // ICC_TAB_CLASSES added for ui::TabBar's WC_TABCONTROL (WI-05 step 2) -
    // without this, WC_TABCONTROLW is never registered as a window class,
    // so TabBar::create()'s CreateWindowExW silently fails (returns
    // nullptr) and createAndPositionTabBar() takes its "unavailable this
    // session" early-return path, leaving RenderPipeline's reserved tab-bar
    // height rendered as a blank gap - a real dogfooding-caught bug, not a
    // hypothetical.
    // ICC_BAR_CLASSES added for ui::StatusBar's STATUSCLASSNAME (WI-07
    // step4) - same TabBar precedent above, applied preemptively rather
    // than waiting to rediscover the identical failure mode.
    const INITCOMMONCONTROLSEX icc{.dwSize = sizeof(icc),
                                   .dwICC  = ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES |
                                             ICC_BAR_CLASSES};
    ::InitCommonControlsEx(&icc);
}

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

Document prepareDocument(const LaunchArgs& args, std::uint64_t& syntheticLineCountOut,
                         DocumentFileState& fileStateOut,
                         std::optional<std::filesystem::path>& currentDocumentPathOut) {
    syntheticLineCountOut = 0;
    if (args.mode == LaunchMode::MeasureFrame && !args.openPath) {
        syntheticLineCountOut = kSyntheticLineCount;
        return synthesizeMeasurementDocument();
    }
    if (args.mode == LaunchMode::Normal || args.mode == LaunchMode::MeasureFrame) {
        return loadStartupDocument(args, fileStateOut, currentDocumentPathOut);
    }
    return Document{};
}

}  // namespace neomifes::app
