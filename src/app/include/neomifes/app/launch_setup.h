#pragma once

// launch_setup - process-bootstrap helpers wWinMain calls before creating
// any window: command-line parsing, the named-mutex single-instance check,
// DPI/Common-Controls setup, and building the Document a launch should
// start with (loaded from --open, synthesized for --measure-frame, or
// empty). WI-04 step 3b: moved out of main.cpp (~190 lines) alongside
// normal_mode_wiring.{h,cpp} so wWinMain itself could shrink to the WI-04
// DoD of <=500 lines - this file covers everything that runs BEFORE window
// creation to decide "what mode is this launch, what Document does it
// start with, is this the only instance", a natural unit distinct from
// wiring up and running the window itself.

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>

#include "neomifes/app/editor_session.h"
#include "neomifes/document/document.h"
#include "neomifes/platform/handle_guard.h"

namespace neomifes::app {

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

// Very small hand-rolled parser. Deliberately avoids CommandLineToArgvW-derived
// heap allocations on the fast path when no measurement flags are present.
[[nodiscard]] LaunchArgs parseArgs() noexcept;

// Named-mutex single-instance check (basic_design.md sec.2.3). Only the
// detection + "activate the existing window" half is implemented here - the
// command-line-handoff-via-IPC half described in basic_design.md requires a
// SessionManager that does not exist yet (Phase 4+), so it is deliberately
// not built speculatively. Returns true if THIS process should proceed to
// create its own window; false if an existing instance was found and
// activated instead (caller should exit without creating a window).
//
// `mutexHolder` receives ownership of the mutex handle so it stays alive for
// the process lifetime (a second launch must still detect this one).
[[nodiscard]] bool claimSingleInstance(platform::KernelHandle& mutexHolder) noexcept;

// Defensive: FindBar's SetWindowSubclass/DefSubclassProc (Phase 5b3a, first
// comctl32 usage in this codebase) do not strictly require this per
// Microsoft's docs (it is only load-bearing for visual-styles-aware
// controls), but calling it costs nothing and removes any doubt about
// comctl32 being loaded before the first CreateWindowExW(WC_EDITW, ...).
void initCommonControls() noexcept;

// Enable Per-Monitor V2 DPI awareness. Falls back silently on older Win10 builds.
void enableHighDpi() noexcept;

// Decides which Document a launch needs: --open's file (Normal or
// MeasureFrame), a synthesized large document (MeasureFrame without --open),
// or an unused empty one (MeasureStartup/MeasureMemory don't render at all).
// `syntheticLineCountOut` is set only when the synthetic path was taken, for
// FrameProfile reporting. `fileStateOut`/`currentDocumentPathOut` (WI-02)
// are populated ONLY on a successful --open load - a stale path pointing at
// an empty Document would make a later Ctrl+S wipe the original file, not
// just mis-color syntax highlighting - so both stay at their
// freshly-constructed defaults (untitled, UTF-8/CRLF/no-BOM) for the
// synthetic/empty/failed-load cases.
[[nodiscard]] document::Document prepareDocument(
    const LaunchArgs& args, std::uint64_t& syntheticLineCountOut, DocumentFileState& fileStateOut,
    std::optional<std::filesystem::path>& currentDocumentPathOut);

}  // namespace neomifes::app
