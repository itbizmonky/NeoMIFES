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
#include <commctrl.h>
#include <shellapi.h>

// WI-02: TaskDialogIndirect (message_dialogs.cpp) requires the Common
// Controls v6 ComCtl32 that only loads with this embedded manifest
// dependency - v5.82 (the default without one) does not export it. No
// .manifest/.rc file exists anywhere in this repo, so this single-line
// linker pragma is the standard way to opt in without adding a new build
// file. Harmless for every other Win32 API this codebase already uses.
#pragma comment(linker, \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "neomifes/app/document_open.h"
#include "neomifes/app/editor_input.h"
#include "neomifes/app/file_dialogs.h"
#include "neomifes/app/fold_bridge.h"
#include "neomifes/app/grep_query_builder.h"
#include "neomifes/app/grep_result_formatting.h"
#include "neomifes/app/message_dialogs.h"
#include "neomifes/app/outline_bridge.h"
#include "neomifes/app/syntax_language.h"
#include "neomifes/app/tag_jump.h"
#include "neomifes/core/bookmark_manager.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/folding_model.h"
#include "neomifes/core/indentation_conversion.h"
#include "neomifes/core/replace_all_command.h"
#include "neomifes/core/search_history.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/document/file_saver.h"
#include "neomifes/encoding/encoding.h"
#include "neomifes/platform/app_data_dir.h"
#include "neomifes/platform/clipboard.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/platform/perf_clock.h"
#include "neomifes/platform/process_metrics.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/search/grep_service.h"
#include "neomifes/search/replacement.h"
#include "neomifes/search/search_service.h"
#include "neomifes/ui/command_descriptor.h"
#include "neomifes/ui/command_palette.h"
#include "neomifes/ui/find_bar.h"
#include "neomifes/ui/find_navigation.h"
#include "neomifes/ui/goto_line_bar.h"
#include "neomifes/ui/goto_line_parser.h"
#include "neomifes/ui/grep_bar.h"
#include "neomifes/ui/main_window.h"
#include "neomifes/ui/outline_pane.h"
#include "neomifes/util/tag_jump_parser.h"

#include "frame_profile.h"
#include "startup_profile.h"

namespace {

using neomifes::app::FrameProfile;
using neomifes::app::StartupProfile;
using neomifes::core::BookmarkManager;
using neomifes::core::CommandDispatcher;
using neomifes::core::computeIndentationConversionEdits;
using neomifes::core::Cursor;
using neomifes::core::FoldingModel;
using neomifes::core::FoldRegion;
using neomifes::core::IndentationConversionTarget;
using neomifes::core::MovementKind;
using neomifes::core::moveTextPos;
using neomifes::core::PerCursorEdit;
using neomifes::core::ReplaceAllCommand;
using neomifes::core::ReplaceRangeCommand;
using neomifes::core::SearchHistory;
using neomifes::core::SelectionModel;
using neomifes::core::Viewport;
using neomifes::document::Document;
using neomifes::document::LoadError;
using neomifes::document::LoadResult;
using neomifes::document::TextRange;
using neomifes::encoding::Encoding;
using neomifes::encoding::LineEnding;
using neomifes::platform::currentProcessMemory;
using neomifes::platform::KernelHandle;
using neomifes::platform::PerfClock;
using neomifes::platform::resolveAppDataDir;
using neomifes::render::FoldVisual;
using neomifes::render::MatchVisual;
using neomifes::render::RenderPipeline;
using neomifes::search::expandReplacementTemplate;
using neomifes::search::GrepMatch;
using neomifes::search::GrepService;
using neomifes::search::Match;
using neomifes::search::Query;
using neomifes::search::SearchService;
using neomifes::ui::CommandDescriptor;
using neomifes::ui::CommandPalette;
using neomifes::ui::CommandPaletteConfig;
using neomifes::ui::FindBar;
using neomifes::ui::FindBarConfig;
using neomifes::ui::GotoLineBar;
using neomifes::ui::GotoLineBarConfig;
using neomifes::ui::GrepBar;
using neomifes::ui::GrepBarConfig;
using neomifes::ui::kWindowClassName;
using neomifes::ui::MainWindow;
using neomifes::ui::MainWindowConfig;
using neomifes::ui::OutlinePane;
using neomifes::ui::OutlinePaneConfig;

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

// WI-02: the document::saveFile()-relevant metadata for the currently open
// document - what Ctrl+S should reuse without prompting. Deliberately
// does NOT duplicate the existing currentDocumentPath local (wWinMain) -
// that variable is already threaded through many pre-existing functions
// (handleTagJumpKey/handleOutlineKey/extractCurrentOutline/etc., Phase
// 5c2-7i) for language detection, and renaming it everywhere for this WI
// would be a large, purely-mechanical diff with no functional benefit.
// openAndResetTo() (below) is the single place that keeps this struct and
// currentDocumentPath in sync for every "opened a different file" case;
// handleNewDocumentKey() is the only other writer (Ctrl+N resets both).
struct DocumentFileState {
    Encoding   encoding   = Encoding::Utf8;
    LineEnding lineEnding = LineEnding::Crlf;  // build_plan.md: new document default
    bool       writeBom   = false;
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

// Defensive: FindBar's SetWindowSubclass/DefSubclassProc (Phase 5b3a, first
// comctl32 usage in this codebase) do not strictly require this per
// Microsoft's docs (it is only load-bearing for visual-styles-aware
// controls), but calling it costs nothing and removes any doubt about
// comctl32 being loaded before the first CreateWindowExW(WC_EDITW, ...).
void initCommonControls() noexcept {
    // ICC_TREEVIEW_CLASSES added for OutlinePane's WC_TREEVIEW (Phase 7g) -
    // this codebase's first control outside ICC_STANDARD_CLASSES.
    const INITCOMMONCONTROLSEX icc{.dwSize = sizeof(icc),
                                   .dwICC   = ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES};
    ::InitCommonControlsEx(&icc);
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
    const std::wstring msg = L"loadFile failed for " + path.wstring() +
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
// WI-02: `fileStateOut`/`currentDocumentPathOut` are populated ONLY on a
// successful load (not unconditionally from `args.openPath` regardless of
// outcome, as this function's caller used to do further down in wWinMain -
// see prepareDocument()'s own comment for why that distinction now matters
// for data safety, not just cosmetic language detection).
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

// Decides which Document a launch needs: --open's file (Normal or
// MeasureFrame), a synthesized large document (MeasureFrame without --open),
// or an unused empty one (MeasureStartup/MeasureMemory don't render at all).
// `syntheticLineCountOut` is set only when the synthetic path was taken, for
// FrameProfile reporting. `fileStateOut`/`currentDocumentPathOut` (WI-02)
// are forwarded to loadStartupDocument() untouched otherwise - synthetic/
// empty documents have no real source file, so both stay at their
// freshly-constructed defaults (untitled, UTF-8/CRLF/no-BOM) in those
// cases. Pulled out of wWinMain to keep its cognitive complexity down
// (same rationale as loadStartupDocument() above).
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
// (setTopLine/setCursorVisuals take plain document types), so this glue
// lives here in the app layer rather than in either core or render.
// Phase 4b7a: builds one CursorVisual per SelectionModel cursor (not just
// the primary) so every cursor's caret/selection actually gets drawn.
// `primaryVirtualColumnOffset` (Phase 4b8e, default 0 - every existing call
// site is unaffected) carries the primary cursor's free-cursor virtual
// column count, if any, onto its CursorVisual; every other cursor always
// gets 0 (free cursor mode is single-primary-cursor only, per the approved
// plan).
void syncRenderStateAndInvalidate(HWND hwnd, RenderPipeline& renderPipeline,
                                  const SelectionModel& selection, const Viewport& viewport,
                                  std::uint32_t primaryVirtualColumnOffset = 0) {
    renderPipeline.setTopLine(viewport.topLine());
    // WI-03: horizontal counterpart to setTopLine() above, same "driven
    // every frame from Viewport" wiring.
    renderPipeline.setLeftColumn(viewport.leftColumn());
    std::vector<neomifes::render::CursorVisual> visuals;
    visuals.reserve(selection.cursors().size());
    for (const auto& cursor : selection.cursors()) {
        visuals.push_back(neomifes::render::CursorVisual{
            .position       = cursor.position,
            .selectionRange = TextRange{.start = std::min(cursor.position, cursor.anchor),
                                        .end     = std::max(cursor.position, cursor.anchor)},
            .virtualColumnOffset = cursor.isPrimary ? primaryVirtualColumnOffset : 0,
            .isPrimary           = cursor.isPrimary,
        });
    }
    renderPipeline.setCursorVisuals(std::move(visuals));
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// WI-03: keeps the window's standard horizontal scrollbar (WS_HSCROLL) in
// sync with RenderPipeline/Viewport's own idea of the scroll range - range
// (nMax) from maxVisibleLineLength() (the visible-window approximation, see
// that method's comment), page size (nPage) from visibleColumnCount(), thumb
// position (nPos) from leftColumn(). Called once per successful render()
// (the paint handler below) rather than from every scroll-causing event
// individually - maxVisibleLineLength()/visibleColumnCount() are only
// current AFTER a render pass has walked the (possibly newly scrolled-to)
// visible lines, so re-deriving them any earlier would use stale values.
void syncHorizontalScrollBar(HWND hwnd, const RenderPipeline& renderPipeline,
                             const Viewport& viewport) noexcept {
    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = static_cast<int>(renderPipeline.maxVisibleLineLength());
    si.nPage  = static_cast<UINT>(renderPipeline.visibleColumnCount());
    si.nPos   = static_cast<int>(viewport.leftColumn());
    ::SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
}

// Pushes FoldingModel's current region list into RenderPipeline as
// render::FoldVisual (Phase 7i) - the render:: mirror-type conversion every
// core:: session-state setter (setBookmarkedLines(), setCursorVisuals())
// already follows. Called after every FoldingModel mutation (toggle, or a
// fresh region list from refreshOutlinePane()/refreshFoldingRegions()).
void syncFoldingState(HWND hwnd, RenderPipeline& renderPipeline, const FoldingModel& foldingModel) {
    std::vector<FoldVisual> visuals;
    visuals.reserve(foldingModel.regions().size());
    for (const FoldRegion& region : foldingModel.regions()) {
        visuals.push_back(FoldVisual{
            .headerLine       = region.headerLine,
            .endLineInclusive = region.endLineInclusive,
            .folded           = region.folded,
        });
    }
    renderPipeline.setFoldRegions(std::move(visuals));
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Bundles the Find/Replace feature's session-lifetime state (Phase 5b3b) -
// replaces 3 separate reference parameters (currentQuery didn't exist
// before; currentMatches/currentMatchIndex were threaded individually) that
// had pushed wireNormalMode to 12 parameters. currentQuery is new: it is
// needed so replaceCurrentMatch() can re-run the identical search after a
// document mutation shifts offsets (previously each search's Query was
// discarded immediately after SearchService::findAll()).
struct FindReplaceState {
    Query               currentQuery;
    std::vector<Match>  currentMatches;
    std::size_t          currentMatchIndex = 0;
};

// Grep results pane state (Phase 5c3) - kept separate from FindReplaceState
// since Grep and Find are independent, simultaneously-visible overlays (no
// mutual exclusion exists anywhere in this codebase - see grep_bar.h's class
// comment) with unrelated result shapes (search::GrepMatch vs. search::Match).
// GrepBar itself never sees search::GrepMatch (stays decoupled from
// neomifes::search, same rationale as FindBar - see FindReplaceState's own
// comment above), so this state has to live here.
struct GrepState {
    std::vector<GrepMatch> currentResults;
};

// Rebuilds RenderPipeline's match highlight set from `state.currentMatches`,
// marking `state.currentMatchIndex` as the "active" one (Phase 5b3a).
// Pulled out of runFindQuery()/navigateToMatch() since both need to do this
// identically.
void syncMatchVisuals(const FindReplaceState& state, RenderPipeline& renderPipeline) {
    std::vector<MatchVisual> visuals;
    visuals.reserve(state.currentMatches.size());
    for (std::size_t i = 0; i < state.currentMatches.size(); ++i) {
        visuals.push_back(MatchVisual{.range     = state.currentMatches[i].range,
                                      .isCurrent = (i == state.currentMatchIndex)});
    }
    renderPipeline.setMatchVisuals(std::move(visuals));
}

// Moves the selection/viewport to `state.currentMatches[state.currentMatchIndex]`
// and pushes the resulting state to FindBar/RenderPipeline (Phase 5b3a).
// Shared by runFindQuery() (jump to the first match after a new search) and
// navigateToMatch() (F3/Shift+F3) - both end up wanting exactly this.
void jumpToMatch(HWND hwnd, const FindReplaceState& state, SelectionModel& selectionModel,
                 Viewport& viewport, const Document& document, RenderPipeline& renderPipeline,
                 FindBar& findBar) {
    const Match& match = state.currentMatches[state.currentMatchIndex];
    selectionModel.setCursors(
        {Cursor{.position = match.range.end, .anchor = match.range.start, .isPrimary = true}});
    viewport.ensureVisible(match.range.start, document);
    findBar.setMatchCount(state.currentMatchIndex, state.currentMatches.size());
    syncMatchVisuals(state, renderPipeline);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
}

// Runs SearchService::findAll() and updates `state.currentQuery`/
// `state.currentMatches`/`state.currentMatchIndex` (reset to 0) plus
// RenderPipeline's highlight set - but does NOT move the
// selection/viewport (Phase 5b3b: extracted from runFindQuery()'s previous
// body, which always jumped to match #0. replaceCurrentMatch() needs the
// search-and-update-state half without the jump, since it wants to land on
// "the match nearest the one just replaced", not unconditionally #0).
void refreshMatches(const Query& query, const Document& document, FindReplaceState& state,
                    RenderPipeline& renderPipeline, FindBar& findBar) {
    state.currentQuery      = query;
    state.currentMatches    = SearchService::findAll(document, query);
    state.currentMatchIndex = 0;
    findBar.setMatchCount(state.currentMatchIndex, state.currentMatches.size());
    syncMatchVisuals(state, renderPipeline);
}

// Runs SearchService::findAll() for FindBar's onQueryChanged callback and
// jumps to the first match, if any (Phase 5b3a). An empty/no-match result
// clears all highlighting and shows FindBar's "no results" state.
void runFindQuery(std::u16string_view query, bool caseSensitive, bool wholeWord, bool regex, HWND hwnd,
                  const Document& document, FindReplaceState& state, SelectionModel& selectionModel,
                  Viewport& viewport, RenderPipeline& renderPipeline, FindBar& findBar) {
    refreshMatches(Query{.pattern       = std::u16string(query),
                        .caseSensitive = caseSensitive,
                        .wholeWord     = wholeWord,
                        .regex         = regex},
                  document, state, renderPipeline, findBar);
    if (state.currentMatches.empty()) {
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    jumpToMatch(hwnd, state, selectionModel, viewport, document, renderPipeline, findBar);
}

// F3 (forward=true) / Shift+F3 (forward=false), wrapping around - shared by
// FindBarConfig::onFindNext/onFindPrevious (fired while the find edit has
// focus) and the F3/Shift+F3 branch of handleFindBarKey() below (fired
// while the document editing area has focus instead) - same "one shared
// helper, two call sites" pattern as dispatchMouseDown()/handleClipboardKey().
void navigateToMatch(bool forward, HWND hwnd, FindReplaceState& state, SelectionModel& selectionModel,
                     Viewport& viewport, const Document& document, RenderPipeline& renderPipeline,
                     FindBar& findBar) {
    if (state.currentMatches.empty()) {
        return;
    }
    state.currentMatchIndex = forward
        ? neomifes::ui::nextMatchIndex(state.currentMatchIndex, state.currentMatches.size())
        : neomifes::ui::previousMatchIndex(state.currentMatchIndex, state.currentMatches.size());
    jumpToMatch(hwnd, state, selectionModel, viewport, document, renderPipeline, findBar);
}

// Escape while the find edit has focus (FindBarConfig::onClosed) - hides
// the bar, clears match highlighting, and restores focus to the document
// editing area (FindBar itself does not know where that is).
void closeFindBar(HWND hwnd, FindBar& findBar, FindReplaceState& state, RenderPipeline& renderPipeline) {
    findBar.hide();
    state.currentMatches.clear();
    renderPipeline.setMatchVisuals({});
    ::SetFocus(hwnd);
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Ctrl+F (show) / F3 / Shift+F3 (navigate) while the document editing area
// has focus (not the find edit - see find_bar.h's class comment for why
// these same keys are ALSO handled inside FindBar's own subclass proc when
// the find edit itself has focus). Returns true if the key was one this
// handles, mirroring handleClipboardKey()'s ClipboardKeyResult.handled shape.
bool handleFindBarKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, FindBar& findBar,
                      FindReplaceState& state, SelectionModel& selectionModel, Viewport& viewport,
                      const Document& document, RenderPipeline& renderPipeline) {
    // !shiftDown is redundant today (handleGrepKey() is checked earlier in
    // handleKeyDownEvent()'s dispatch chain and already claims Ctrl+Shift+F
    // via an early return), but makes this condition self-documenting and
    // safe against a future reordering of that chain (Phase 5c3).
    if (ctrlDown && !shiftDown && vkCode == 'F') {
        findBar.show();
        return true;
    }
    if (vkCode == VK_F3) {
        navigateToMatch(!shiftDown, hwnd, state, selectionModel, viewport, document, renderPipeline,
                        findBar);
        return true;
    }
    return false;
}

// Ctrl+Shift+P while the document editing area has focus (Phase 5b3c) -
// mirrors handleFindBarKey()'s single-purpose shape. Not fired while the
// palette's own query edit has focus (same reasoning as handleFindBarKey's
// comment: Win32 routes keyboard input straight to the focused child HWND).
bool handleCommandPaletteKey(UINT vkCode, bool shiftDown, bool ctrlDown, CommandPalette& commandPalette) {
    if (ctrlDown && shiftDown && vkCode == 'P') {
        commandPalette.show();
        return true;
    }
    return false;
}

// Ctrl+Shift+F while the document editing area has focus (Phase 5c3) -
// mirrors handleCommandPaletteKey()'s single-purpose shape. Must be checked
// in handleKeyDownEvent()'s dispatch chain BEFORE handleFindBarKey():
// handleFindBarKey()'s own `ctrlDown && vkCode == 'F'` check does not look
// at shiftDown, so without this ordering Ctrl+Shift+F would already be
// swallowed by the plain Find bar before ever reaching this function.
bool handleGrepKey(UINT vkCode, bool shiftDown, bool ctrlDown, GrepBar& grepBar) {
    if (ctrlDown && shiftDown && vkCode == 'F') {
        grepBar.show();
        return true;
    }
    return false;
}

// Parses the currently open document into an OutlineNode tree (empty if no
// language detected - currentDocumentPath unset, or an unrecognized
// extension). Factored out of refreshOutlinePane() (Phase 7i) so its result
// can seed both the outline panel and core::FoldingModel's fold regions from
// the exact same parse, rather than each computing (and re-parsing) its own.
std::vector<neomifes::syntax::OutlineNode> extractCurrentOutline(
    const Document& document, const std::optional<std::filesystem::path>& currentDocumentPath) {
    const auto language =
        currentDocumentPath ? neomifes::app::detectLanguage(*currentDocumentPath) : std::nullopt;
    if (!language) {
        return {};
    }
    const auto            snapshot = document.snapshot();
    const std::u16string  text = snapshot->extract(TextRange{.start = 0, .end = snapshot->length()});
    return neomifes::syntax::extractOutline(text, *language);
}

// Recomputes and displays the outline for the currently open document
// (Phase 7g) - called from handleOutlineKey() below whenever the panel is
// (re-)shown. Same "harmless empty result, no special-casing" convention as
// buildGrepQueryFromInput() on an empty query. Phase 7i: also refreshes
// FoldingModel's foldable-region list from the same parse (see
// extractCurrentOutline()'s comment) - existing folded state is preserved
// by FoldingModel::setFoldableRegions() matching on headerLine, same
// "stale after edit until next refresh" limitation as BookmarkManager.
void refreshOutlinePane(const Document& document,
                        const std::optional<std::filesystem::path>& currentDocumentPath,
                        neomifes::ui::OutlinePane& outlinePane, FoldingModel& foldingModel) {
    const auto nodes = extractCurrentOutline(document, currentDocumentPath);
    outlinePane.showWith(neomifes::app::buildOutlineItems(nodes));
    foldingModel.setFoldableRegions(neomifes::app::buildFoldRegions(nodes, document));
}

// Ctrl+Shift+O while the document editing area has focus (Phase 7g) -
// unlike handleCommandPaletteKey()/handleGrepKey(), this TOGGLES (a second
// press while visible hides it) rather than only ever showing. An outline
// view is a persistent navigation aid the user dismisses with the same key
// they opened it with, not a one-shot search/command tool - see
// outline_pane.h's class comment.
bool handleOutlineKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, const Document& document,
                      const std::optional<std::filesystem::path>& currentDocumentPath,
                      neomifes::ui::OutlinePane& outlinePane, FoldingModel& foldingModel,
                      RenderPipeline& renderPipeline) {
    if (!ctrlDown || !shiftDown || vkCode != 'O') {
        return false;
    }
    if (outlinePane.isVisible()) {
        outlinePane.hide();
    } else {
        refreshOutlinePane(document, currentDocumentPath, outlinePane, foldingModel);
        syncFoldingState(hwnd, renderPipeline, foldingModel);
    }
    return true;
}

// OutlinePaneConfig::onItemSelected (Phase 7g) - unlike jumpToGotoTarget()/
// openDocumentAt(), no line/column conversion is needed: the targetPos
// OutlinePane echoes back is already a 0-based document::TextPos into the
// SAME open document, not a cross-file jump. The panel is deliberately left
// open afterward (see outline_pane.h's class comment) - this function never
// touches it.
void jumpToOutlinePosition(std::uint64_t targetPos, HWND hwnd, const Document& document,
                           SelectionModel& selectionModel, Viewport& viewport,
                           RenderPipeline& renderPipeline) {
    const auto pos =
        std::min(static_cast<neomifes::document::TextPos>(targetPos), document.length());
    selectionModel.moveAllTo(pos);
    viewport.ensureVisible(pos, document);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
}

// Ctrl+G while the document editing area has focus (Phase 4b8b) - same
// single-purpose shape as handleFindBarKey()/handleCommandPaletteKey().
bool handleGotoLineKey(UINT vkCode, bool ctrlDown, GotoLineBar& gotoLineBar) {
    if (ctrlDown && vkCode == 'G') {
        gotoLineBar.show();
        return true;
    }
    return false;
}

// Ctrl+F2 (toggle a bookmark on the current line) / F2 (jump to next
// bookmark) / Shift+F2 (jump to previous), Phase 4b8c. Mirrors
// handleGotoLineKey()'s single-purpose shape. `bookmarks.next()`/
// `previous()` already wrap around and return the nearest bookmark
// *strictly* after/before the current line, so re-pressing F2 while
// sitting on a bookmarked line correctly cycles to the next one rather than
// staying put.
bool handleBookmarkKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, BookmarkManager& bookmarks,
                       SelectionModel& selectionModel, Viewport& viewport, const Document& document,
                       RenderPipeline& renderPipeline, FoldingModel& foldingModel) {
    if (vkCode != VK_F2) {
        return false;
    }
    const auto currentLine = document.offsetToLine(selectionModel.primaryCursor().position);
    if (ctrlDown) {
        bookmarks.toggle(currentLine);
        renderPipeline.setBookmarkedLines(
            std::vector<neomifes::document::LineNumber>(bookmarks.lines().begin(), bookmarks.lines().end()));
        ::InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    const auto target = shiftDown ? bookmarks.previous(currentLine) : bookmarks.next(currentLine);
    if (target) {
        const auto pos = document.lineToOffset(*target);
        selectionModel.moveAllTo(pos);
        viewport.ensureVisible(pos, document);
        // Phase 7i: a bookmark can land inside content that's since been
        // folded - reveal it rather than leaving the cursor on a hidden line.
        if (foldingModel.revealLine(*target)) {
            syncFoldingState(hwnd, renderPipeline, foldingModel);
        }
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    }
    return true;
}

// WI-02: the "the document just changed to a different file" reset,
// shared by every caller that swaps `document`'s content out from under
// the view (F12 tag-jump, Grep-result-click, and - once wired below -
// Ctrl+O/drag-drop-open). Originally duplicated verbatim at the F12/Grep
// call sites (Phase 5c2-7i); a 3rd/4th caller needing the identical
// sequence is exactly this codebase's own "extract once 3+ callers exist"
// trigger (see visibleLineAtRow()/reservedTopHeightDips()'s own history).
//
// Deliberately does NOT reset dispatcher/bookmarks/anchors/
// freeCursorVirtualColumns - those are openDocumentAt()'s own internal
// responsibility when a file was actually loaded. A "reset to blank"
// caller (Ctrl+N) never calls openDocumentAt() at all (there is no file to
// load) and MUST perform that half itself - see handleNewDocumentKey()'s
// own comment for the concrete data-corruption path (a stale Undo
// splicing the previous file's deleted text into the new blank document)
// that skipping it caused during this WI's design review.
void resetViewAfterDocumentSwap(HWND hwnd, RenderPipeline& renderPipeline, FoldingModel& foldingModel,
                                FindReplaceState& findReplaceState, FindBar& findBar,
                                const std::optional<std::filesystem::path>& currentDocumentPath) {
    findReplaceState.currentMatches.clear();
    findReplaceState.currentMatchIndex = 0;
    findBar.setMatchCount(0, 0);
    renderPipeline.setMatchVisuals({});
    renderPipeline.setBookmarkedLines({});
    foldingModel.setFoldableRegions({});
    syncFoldingState(hwnd, renderPipeline, foldingModel);
    renderPipeline.setLanguage(currentDocumentPath ? neomifes::app::detectLanguage(*currentDocumentPath)
                                                    : std::nullopt);
    ::SetFocus(hwnd);
}

// WI-02: opens `path` into `document` via neomifes::app::openDocumentAt()
// (optionally jumping to targetLine/targetColumn - both already 0-based,
// same convention openDocumentAt() itself documents), and on success
// updates `currentDocumentPath`/`fileState` from the returned
// LoadedFileMeta and runs resetViewAfterDocumentSwap() + a final repaint.
// Returns the LoadError on failure, leaving document/currentDocumentPath/
// fileState completely untouched (matches openDocumentAt()'s own no-
// partial-mutation-on-failure contract) - callers decide whether to
// surface it (F12/Grep-click keep their pre-existing silent no-op below;
// Ctrl+O/drag-drop-open show message_dialogs.h's showOpenErrorDialog()).
std::optional<LoadError> openAndResetTo(
    const std::filesystem::path& path, std::optional<neomifes::document::LineNumber> targetLine,
    std::optional<std::uint64_t> targetColumn, HWND hwnd, Document& document,
    CommandDispatcher& dispatcher, SelectionModel& selectionModel, Viewport& viewport,
    RenderPipeline& renderPipeline, BookmarkManager& bookmarks, FoldingModel& foldingModel,
    FindReplaceState& findReplaceState, FindBar& findBar,
    std::optional<neomifes::document::TextPos>& altCursorAnchor,
    std::optional<neomifes::document::TextPos>& rectangularAnchor,
    std::optional<std::uint32_t>& freeCursorVirtualColumns,
    std::optional<std::filesystem::path>& currentDocumentPath, DocumentFileState& fileState) {
    auto result = neomifes::app::openDocumentAt(path, targetLine, targetColumn, document, dispatcher,
                                                selectionModel, viewport, bookmarks, altCursorAnchor,
                                                rectangularAnchor, freeCursorVirtualColumns);
    auto* const meta = std::get_if<neomifes::app::LoadedFileMeta>(&result);
    if (meta == nullptr) {
        return std::get<LoadError>(result);
    }
    currentDocumentPath  = path;
    fileState.encoding   = meta->encoding;
    fileState.lineEnding = meta->lineEnding;
    fileState.writeBom   = meta->hadBom;
    resetViewAfterDocumentSwap(hwnd, renderPipeline, foldingModel, findReplaceState, findBar,
                               currentDocumentPath);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    return std::nullopt;
}

// WI-02: shared save routine for Ctrl+S/Ctrl+Shift+S (and the "Save" choice
// of confirmDiscardIfDirty()'s unsaved-changes prompt below). `forceSaveAs`
// (or a document that has never been saved anywhere, i.e. `!currentDocumentPath`)
// prompts via file_dialogs.h's showSaveFileDialog() for a destination first;
// otherwise reuses the existing path silently. Returns false on a cancelled
// dialog or a failed document::saveFile() call (in which case
// message_dialogs.h's showSaveErrorDialog() has already reported it to the
// user) - never silently swallows a failure (CLAUDE.md rule 3).
bool performSave(HWND hwnd, Document& document, DocumentFileState& fileState,
                 std::optional<std::filesystem::path>& currentDocumentPath, bool forceSaveAs) {
    std::filesystem::path targetPath;
    if (forceSaveAs || !currentDocumentPath) {
        const auto chosen = neomifes::app::showSaveFileDialog(hwnd, currentDocumentPath);
        if (!chosen) {
            return false;  // dialog cancelled - not an error, just no-op
        }
        targetPath = *chosen;
    } else {
        targetPath = *currentDocumentPath;
    }
    const auto error = neomifes::document::saveFile(document, targetPath, fileState.encoding,
                                                     fileState.lineEnding, fileState.writeBom);
    if (error) {
        neomifes::app::showSaveErrorDialog(hwnd, *error);
        return false;
    }
    currentDocumentPath = targetPath;
    return true;
}

// WI-02: the "may this destructive operation proceed" gate shared by
// Ctrl+O/Ctrl+N/drag-drop-open/WM_CLOSE. An already-clean document is
// always an immediate yes. Otherwise prompts via
// message_dialogs.h's showUnsavedChangesDialog(); the Save choice routes
// through performSave() above so a cancelled Save-As dialog or a failed
// save also blocks the destructive operation - unsaved work is never
// discarded behind a save that didn't actually happen.
bool confirmDiscardIfDirty(HWND hwnd, Document& document, DocumentFileState& fileState,
                           std::optional<std::filesystem::path>& currentDocumentPath) {
    if (!document.isDirty()) {
        return true;
    }
    const std::wstring documentName =
        currentDocumentPath ? currentDocumentPath->filename().wstring() : L"Untitled";
    switch (neomifes::app::showUnsavedChangesDialog(hwnd, documentName)) {
        case neomifes::app::UnsavedChangesChoice::Save:
            return performSave(hwnd, document, fileState, currentDocumentPath,
                               /*forceSaveAs=*/!currentDocumentPath.has_value());
        case neomifes::app::UnsavedChangesChoice::DontSave:
            return true;
        case neomifes::app::UnsavedChangesChoice::Cancel:
        default:
            return false;
    }
}

// F12 (Phase 5c4) - same "act on the cursor's current line" shape as
// handleBookmarkKey()'s F2 above, but here the line's TEXT (not just its
// number) is inspected: if it contains an MSVC-diagnostic-style location
// reference ("path(line)"/"path(line,column)", util::parseTagJumpReference()),
// opens that file via openAndResetTo() (WI-02, wrapping
// neomifes::app::openDocumentAt(), Phase 5c2) and jumps to the referenced
// position. Always returns true once vkCode==VK_F12 is confirmed - F12 is
// unclaimed everywhere else in this dispatch chain, so there is nothing to
// fall through to whether or not a reference was found/opened (same
// silent-no-op contract openDocumentAt() itself guarantees on a
// stale/missing path).
bool handleTagJumpKey(HWND hwnd, UINT vkCode, Document& document, CommandDispatcher& dispatcher,
                      SelectionModel& selectionModel, Viewport& viewport, BookmarkManager& bookmarks,
                      RenderPipeline& renderPipeline, FindBar& findBar,
                      FindReplaceState& findReplaceState, FoldingModel& foldingModel,
                      std::optional<neomifes::document::TextPos>& altCursorAnchor,
                      std::optional<neomifes::document::TextPos>& rectangularAnchor,
                      std::optional<std::uint32_t>& freeCursorVirtualColumns,
                      std::optional<std::filesystem::path>& currentDocumentPath,
                      DocumentFileState& fileState) {
    if (vkCode != VK_F12) {
        return false;
    }
    const auto cursorPos = selectionModel.primaryCursor().position;
    const auto line      = document.offsetToLine(cursorPos);
    const auto lineStart = document.lineToOffset(line);
    const auto lineEnd   = (line + 1 < document.lineCount()) ? document.lineToOffset(line + 1) - 1
                                                             : document.length();
    const std::u16string lineText = document.snapshot()->extract(
        neomifes::document::TextRange{.start = lineStart, .end = lineEnd});

    const auto reference = neomifes::util::parseTagJumpReference(lineText);
    if (!reference) {
        return true;
    }
    const auto resolvedPath =
        neomifes::app::resolveTagJumpPath(reference->path, std::filesystem::current_path());
    const std::optional<std::uint64_t> targetColumn =
        reference->column ? std::optional<std::uint64_t>(*reference->column - 1) : std::nullopt;
    // stale/missing path - openAndResetTo() leaves everything untouched on
    // failure, same silent no-op as before this WI.
    (void)openAndResetTo(resolvedPath, reference->line - 1, targetColumn, hwnd, document, dispatcher,
                         selectionModel, viewport, renderPipeline, bookmarks, foldingModel,
                         findReplaceState, findBar, altCursorAnchor, rectangularAnchor,
                         freeCursorVirtualColumns, currentDocumentPath, fileState);
    return true;
}

// Enter while the replace edit has focus (FindBarConfig::onReplaceCurrent,
// Phase 5b3b) - replaces state.currentMatches[state.currentMatchIndex] with
// `replacementTemplate` expanded against the match's capture groups, then
// re-runs state.currentQuery and jumps to whichever match now occupies the
// same index (clamped, since a replace can only ever remove exactly one
// match, so the count shrinks by at most 1 - see the plan's Context section
// for the out-of-bounds trace).
void replaceCurrentMatch(std::u16string_view replacementTemplate, HWND hwnd, Document& document,
                         CommandDispatcher& dispatcher, FindReplaceState& state,
                         SelectionModel& selectionModel, Viewport& viewport,
                         RenderPipeline& renderPipeline, FindBar& findBar) {
    if (state.currentMatches.empty()) {
        return;
    }
    const std::size_t replacedIndex = state.currentMatchIndex;
    const Match&       match         = state.currentMatches[replacedIndex];
    const std::u16string expanded = expandReplacementTemplate(replacementTemplate, document, match);
    dispatcher.dispatch(std::make_unique<ReplaceRangeCommand>(match.range, expanded));

    refreshMatches(state.currentQuery, document, state, renderPipeline, findBar);
    if (state.currentMatches.empty()) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
        return;
    }
    state.currentMatchIndex = std::min(replacedIndex, state.currentMatches.size() - 1);
    jumpToMatch(hwnd, state, selectionModel, viewport, document, renderPipeline, findBar);
}

// Ctrl+Enter while the replace edit has focus (FindBarConfig::onReplaceAll,
// Phase 5b3b) - replaces every current match atomically as one undo step.
// state.currentMatches is already in ascending document order
// (SearchService::findAll()'s guarantee - search_service.h), matching
// applyEditsWithCumulativeShift()'s ordering requirement
// (cumulative_shift_edit.h) directly, so no re-sort is needed before
// building the PerCursorEdit vector. Each replacement's capture-group
// expansion is resolved against the pre-edit document (expandReplacementTemplate()'s
// contract, replacement.h) before any edit is applied.
//
// Does not re-search afterward: match highlighting is simply cleared, same
// as closeFindBar() - re-matching the just-replaced text against the same
// query would be confusing (looks like the replace silently didn't work)
// rather than informative.
void replaceAllMatches(std::u16string_view replacementTemplate, HWND hwnd, Document& document,
                       CommandDispatcher& dispatcher, const SelectionModel& selectionModel,
                       FindReplaceState& state, RenderPipeline& renderPipeline, FindBar& findBar) {
    if (state.currentMatches.empty()) {
        return;
    }
    std::vector<PerCursorEdit> edits;
    edits.reserve(state.currentMatches.size());
    for (const Match& match : state.currentMatches) {
        edits.push_back(PerCursorEdit{.range        = match.range,
                                      .insertedText = expandReplacementTemplate(replacementTemplate,
                                                                                document, match)});
    }
    const std::vector<Cursor> cursorsBefore(selectionModel.cursors().begin(),
                                            selectionModel.cursors().end());
    dispatcher.dispatch(std::make_unique<ReplaceAllCommand>(std::move(edits), cursorsBefore));

    state.currentMatches.clear();
    state.currentMatchIndex = 0;
    findBar.setMatchCount(0, 0);
    renderPipeline.setMatchVisuals({});
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Convert Tabs to Spaces / Convert Spaces to Tabs command-palette actions
// (Phase 4b8d). Applies to the whole document; tabWidth is fixed at 4 -
// there is no settings system to source a configurable value from (same
// rationale as elsewhere in this file), so a future settings UI would wire
// its value in here rather than this being a design gap. Reuses
// core::ReplaceAllCommand (Phase 5b2) rather than a bespoke command class -
// see indentation_conversion.h's header comment. No-ops (no lines need
// conversion) skip dispatch entirely, same convention as replaceAllMatches()
// above returning early on an empty match set.
void applyIndentationConversion(IndentationConversionTarget target, HWND hwnd, Document& document,
                                CommandDispatcher& dispatcher, const SelectionModel& selectionModel) {
    constexpr int kTabWidth = 4;
    auto edits = computeIndentationConversionEdits(target, kTabWidth, document);
    if (edits.empty()) {
        return;
    }
    const std::vector<Cursor> cursorsBefore(selectionModel.cursors().begin(),
                                            selectionModel.cursors().end());
    dispatcher.dispatch(std::make_unique<ReplaceAllCommand>(std::move(edits), cursorsBefore));
    ::InvalidateRect(hwnd, nullptr, FALSE);
}

// Checks whether a WM_LBUTTONDOWN landed on a foldable gutter row and, if
// so, toggles that region and repaints, returning true so the caller skips
// its ordinary hitTest()/dispatchMouseDown() cursor-placement path entirely
// (Phase 7j). Pulled out of wireNormalMode's onMouseDown lambda to keep
// that function's cognitive complexity down, same rationale as
// dispatchMouseDown() below.
bool tryToggleFoldMarker(HWND hwnd, std::int32_t x, std::int32_t y, RenderPipeline& renderPipeline,
                         FoldingModel& foldingModel) {
    const auto foldHeaderLine = renderPipeline.hitTestFoldMarker(x, y);
    if (!foldHeaderLine) {
        return false;
    }
    foldingModel.toggleFold(*foldHeaderLine);
    syncFoldingState(hwnd, renderPipeline, foldingModel);
    return true;
}

// Checks whether a WM_LBUTTONDOWN landed on the minimap strip and, if so,
// jumps the viewport there and starts drag tracking, returning true so the
// caller skips its ordinary hitTest()/dispatchMouseDown() cursor-placement
// path entirely (Phase 7v) - same "priority-check, consume, and return"
// shape as tryToggleFoldMarker() above. Does not touch SelectionModel: a
// minimap click is a scroll-position operation, not a cursor-placement one
// (matches the common convention other minimap-bearing editors use).
bool tryHandleMinimapClick(HWND hwnd, std::int32_t x, std::int32_t y, RenderPipeline& renderPipeline,
                           Viewport& viewport, const SelectionModel& selectionModel,
                           bool& isDraggingMinimap) {
    const auto targetLine = renderPipeline.hitTestMinimap(x, y);
    if (!targetLine) {
        return false;
    }
    isDraggingMinimap = true;
    viewport.scrollTo(*targetLine);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    return true;
}

// Picks which click interpretation applies to a hit-tested WM_LBUTTONDOWN and
// applies it. Pulled out of wireNormalMode's onMouseDown lambda to keep that
// function's cognitive complexity down (same rationale as
// loadStartupDocument()/prepareDocument() above) - Phase 4b5b's altDown
// branch pushed the inline version over clang-tidy's threshold.
//
// `altCursorAnchor` (Phase 4b6d) is wireNormalMode's session-lifetime state
// tracking the anchor of the cursor a prior plain Alt+click added, so a
// later Alt+Shift+click (and onMouseDrag below, for Alt+drag) can extend
// that specific cursor - SelectionModel::moveAllTo()/moveAll() always apply
// to every cursor uniformly, so this targeted extension needs the caller to
// remember which cursor is "active" across separate mouse events.
//
// `rectangularAnchor` (Phase 4b8a) is the equivalent session-lifetime state
// for Shift+Alt+drag rectangular selection - chosen over the roadmap's
// literal "Alt+drag" spec specifically to avoid colliding with the existing
// altCursorAnchor gesture above (confirmed with the user). It is only ever
// *set* here, on a Shift+Alt+click - never acted upon here, since a click
// alone (no drag) is deliberately left to fall through to the existing
// altCursorAnchor/handleAltClick logic unchanged. If the click does turn
// into a drag, onMouseDrag's rectangularAnchor branch (checked first, see
// below) fully replaces the cursor set via setRectangularSelection(),
// superseding whatever this function did as a side effect - so the
// fallthrough below is harmless rather than a real behavior change.
bool dispatchMouseDown(neomifes::document::TextPos hit, bool shiftDown, bool altDown, int clickCount,
                       SelectionModel& selectionModel, Viewport& viewport, const Document& document,
                       std::optional<neomifes::document::TextPos>& altCursorAnchor,
                       std::optional<neomifes::document::TextPos>& rectangularAnchor) {
    if (altDown) {
        // Alt+Shift+click extends the cursor the last plain Alt+click added
        // (if any); otherwise (including a bare Alt+Shift+click with no
        // prior Alt+click to extend) it falls through to adding a new
        // cursor, same as plain Alt+click. Alt+double/triple-click's
        // meaning is left undefined rather than guessed at - click count is
        // not consulted here at all.
        if (shiftDown) {
            rectangularAnchor = hit;
            if (altCursorAnchor) {
                selectionModel.moveCursorMatching(*altCursorAnchor, hit);
                viewport.ensureVisible(hit, document);
                return true;
            }
        } else {
            // Plain Alt+click is not a rectangular-selection gesture - clear
            // any stale anchor a prior Shift+Alt+click left behind, so a
            // plain Alt+drag that follows isn't mistaken for one.
            rectangularAnchor.reset();
        }
        const bool changed = neomifes::app::handleAltClick(hit, selectionModel, viewport, document);
        altCursorAnchor    = hit;
        return changed;
    }
    // A plain click abandons any in-progress Alt-cursor extension - the next
    // drag should extend the primary selection again, not the old target.
    altCursorAnchor.reset();
    rectangularAnchor.reset();
    if (clickCount >= 3) {
        return neomifes::app::handleTripleClick(hit, selectionModel, viewport, document);
    }
    if (clickCount == 2) {
        return neomifes::app::handleDoubleClick(hit, selectionModel, viewport, document);
    }
    return neomifes::app::handleMouseDown(hit, shiftDown, selectionModel, viewport, document);
}

// Handles WM_LBUTTONDOWN. Pulled out of wireNormalMode's onMouseDown lambda
// for the same cognitive-complexity reason as handleKeyDownEvent() above -
// Phase 7j's tryToggleFoldMarker() check pushed the inline version over
// clang-tidy's threshold.
void handleMouseDownEvent(HWND hwnd, std::int32_t x, std::int32_t y, bool shiftDown, bool altDown,
                          int clickCount, SelectionModel& selectionModel, Viewport& viewport,
                          const Document& document, RenderPipeline& renderPipeline,
                          std::optional<neomifes::document::TextPos>& altCursorAnchor,
                          std::optional<neomifes::document::TextPos>& rectangularAnchor,
                          std::optional<std::uint32_t>& freeCursorVirtualColumns,
                          FoldingModel& foldingModel, bool& isDraggingMinimap) {
    // Every new mouse-down is the start of a fresh gesture - this is the one
    // reliable reset point for this flag (MainWindow exposes no onMouseUp
    // hook; see isDraggingMinimap's declaration comment in wWinMain). Only
    // tryHandleMinimapClick() below sets it back to true.
    isDraggingMinimap = false;
    if (tryToggleFoldMarker(hwnd, x, y, renderPipeline, foldingModel)) {
        return;
    }
    if (tryHandleMinimapClick(hwnd, x, y, renderPipeline, viewport, selectionModel, isDraggingMinimap)) {
        return;
    }
    const auto hit = renderPipeline.hitTest(x, y);
    if (!hit) {
        return;
    }
    // A click is always an "unrelated operation" for free-cursor virtual
    // columns (Phase 4b8e is keyboard-only) - discard silently; the click
    // itself always changes the selection, so the repaint below already
    // clears any stale virtual-offset caret.
    freeCursorVirtualColumns.reset();
    const bool changed = dispatchMouseDown(*hit, shiftDown, altDown, clickCount, selectionModel,
                                          viewport, document, altCursorAnchor, rectangularAnchor);
    if (changed) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    }
}

// Phase 4b8e (フリーカーソル簡略版): Right-arrow past the real end of the
// current line, while Free Cursor Mode is on and there is exactly one
// cursor with no active selection (deliberately narrow scope per the
// approved plan - no mouse support, no multi-cursor, no Shift-extend or
// Ctrl+Right word-jump into virtual space), increments a virtual column
// count instead of the usual "do nothing at end of line/document" behavior.
// No document mutation happens here - the virtual columns are main.cpp
// session state until onChar materializes them (see applyFreeCursorChar()
// below) - so this only ever needs a repaint, never dispatcher.dispatch().
bool handleFreeCursorRightArrow(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown,
                                bool freeCursorModeEnabled,
                                std::optional<std::uint32_t>& freeCursorVirtualColumns,
                                const SelectionModel& selectionModel, const Document& document,
                                RenderPipeline& renderPipeline, const Viewport& viewport) {
    if (!freeCursorModeEnabled || vkCode != VK_RIGHT || shiftDown || ctrlDown ||
        selectionModel.cursors().size() != 1) {
        return false;
    }
    const Cursor& cursor = selectionModel.primaryCursor();
    if (cursor.hasSelection()) {
        return false;
    }
    const auto line = document.offsetToLine(cursor.position);
    const auto lineEndExclusive = (line + 1 < document.lineCount())
                                       ? document.lineToOffset(line + 1) - 1
                                       : document.length();
    if (cursor.position != lineEndExclusive) {
        return false;  // not at the real end of the line yet - normal movement applies
    }
    freeCursorVirtualColumns = freeCursorVirtualColumns.value_or(0) + 1;
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport,
                                 *freeCursorVirtualColumns);
    return true;
}

// Materializes free-cursor virtual columns (Phase 4b8e) once the user types
// a character while the caret sits past the real end of its line: inserts
// `virtualColumns` real spaces followed by `ch` in one edit, via the same
// single-range core::ReplaceRangeCommand dispatch replaceCurrentMatch()
// uses (Phase 5b3b) rather than neomifes::app::handleChar()'s
// insertTextAtEveryCursor() path - this is always exactly one pre-validated,
// selection-less cursor (handleFreeCursorRightArrow()'s guard), not the
// general multi-cursor case. Mirrors handleChar()'s own \r->\n translation
// and C0-control filter (Enter/Tab accepted, everything else below 0x20
// ignored - Backspace/Escape/etc. arrive via WM_KEYDOWN instead) since this
// bypasses handleChar() entirely rather than wrapping it.
void applyFreeCursorChar(wchar_t ch, std::uint32_t virtualColumns, HWND hwnd,
                         CommandDispatcher& dispatcher, SelectionModel& selectionModel,
                         Viewport& viewport, const Document& document,
                         RenderPipeline& renderPipeline) {
    if (ch < 0x20 && ch != u'\r' && ch != u'\t') {
        return;
    }
    auto inserted = static_cast<char16_t>(ch);
    if (ch == u'\r') {
        inserted = u'\n';
    }
    std::u16string text(virtualColumns, u' ');
    text.push_back(inserted);
    const auto pos = selectionModel.primaryCursor().position;
    dispatcher.dispatch(
        std::make_unique<ReplaceRangeCommand>(TextRange{.start = pos, .end = pos}, text));
    viewport.ensureVisible(selectionModel.primaryCursor().position, document);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
}

// Whether a Ctrl+C/X/V keystroke was recognized at all, and (only when it
// was) whether it changed the document/selection.
struct ClipboardKeyResult {
    bool handled = false;
    bool changed = false;
};

// Handles Ctrl+C/X/V (Phase 4b6c). Pulled out of wireNormalMode's onKeyDown
// lambda for the same cognitive-complexity reason as dispatchMouseDown()
// above. Clipboard I/O is a Win32 API concern (src/platform/clipboard.h),
// so this lives here rather than inside neomifes::app::handleKeyDown() -
// editor_input.cpp is deliberately kept free of Win32 calls so it stays
// headlessly testable (see editor_input.h's file header). Applies to every
// cursor (Phase 4b7c) via textToCopy()/handlePaste()/deleteAllSelections().
ClipboardKeyResult handleClipboardKey(HWND hwnd, UINT vkCode, bool ctrlDown,
                                      CommandDispatcher& dispatcher, SelectionModel& selectionModel,
                                      Viewport& viewport, const Document& document) {
    if (!ctrlDown || (vkCode != 'C' && vkCode != 'X' && vkCode != 'V')) {
        return {};
    }
    if (vkCode == 'V') {
        const auto text = neomifes::platform::getClipboardText(hwnd);
        if (!text) {
            return {.handled = true, .changed = false};
        }
        neomifes::app::handlePaste(*text, dispatcher, selectionModel, viewport, document);
        return {.handled = true, .changed = true};
    }
    // Copy or Cut. If the clipboard write fails, don't delete any selection
    // for Cut either - that would destroy text the user never actually got
    // a copy of.
    const auto text = neomifes::app::textToCopy(selectionModel, document);
    if (!text || !neomifes::platform::setClipboardText(hwnd, *text)) {
        return {.handled = true, .changed = false};
    }
    if (vkCode == 'X') {
        const bool changed =
            neomifes::app::deleteAllSelections(dispatcher, selectionModel, viewport, document);
        return {.handled = true, .changed = changed};
    }
    return {.handled = true, .changed = false};
}

// Ctrl+S (save) / Ctrl+Shift+S (save as) (WI-02, build_plan.md §5, 🎉 M1).
// Single function for both - mirrors handleFindBarKey()'s Ctrl+F/F3
// combination - since performSave()'s forceSaveAs parameter is exactly
// `shiftDown`. Always returns true once Ctrl+S is confirmed (return value
// of performSave() itself is intentionally ignored here - no window-title
// "saved" indicator exists yet to update either way, out of scope for
// this WI).
bool handleSaveKey(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown, Document& document,
                   DocumentFileState& fileState,
                   std::optional<std::filesystem::path>& currentDocumentPath) {
    if (!ctrlDown || vkCode != 'S') {
        return false;
    }
    (void)performSave(hwnd, document, fileState, currentDocumentPath, /*forceSaveAs=*/shiftDown);
    return true;
}

// Ctrl+O (WI-02) - confirmDiscardIfDirty() first (so an unsaved edit is
// never silently discarded by opening a different file), then
// file_dialogs.h's showOpenFileDialog() for the destination, then
// openAndResetTo() (Phase 5c2's openDocumentAt(), wrapped) same as F12/
// Grep-result-click. Unlike those two silent-no-op callers, a failed open
// here is user-INITIATED (not a stale background reference), so it is
// surfaced via message_dialogs.h's showOpenErrorDialog().
bool handleOpenKey(HWND hwnd, UINT vkCode, bool ctrlDown, Document& document,
                   CommandDispatcher& dispatcher, SelectionModel& selectionModel, Viewport& viewport,
                   RenderPipeline& renderPipeline, BookmarkManager& bookmarks, FoldingModel& foldingModel,
                   FindReplaceState& findReplaceState, FindBar& findBar,
                   std::optional<neomifes::document::TextPos>& altCursorAnchor,
                   std::optional<neomifes::document::TextPos>& rectangularAnchor,
                   std::optional<std::uint32_t>& freeCursorVirtualColumns,
                   std::optional<std::filesystem::path>& currentDocumentPath,
                   DocumentFileState& fileState) {
    if (!ctrlDown || vkCode != 'O') {
        return false;
    }
    if (!confirmDiscardIfDirty(hwnd, document, fileState, currentDocumentPath)) {
        return true;  // user cancelled the unsaved-changes prompt
    }
    const auto chosen = neomifes::app::showOpenFileDialog(hwnd);
    if (!chosen) {
        return true;  // Open dialog cancelled - nothing to do
    }
    const auto error = openAndResetTo(*chosen, std::nullopt, std::nullopt, hwnd, document, dispatcher,
                                      selectionModel, viewport, renderPipeline, bookmarks, foldingModel,
                                      findReplaceState, findBar, altCursorAnchor, rectangularAnchor,
                                      freeCursorVirtualColumns, currentDocumentPath, fileState);
    if (error) {
        neomifes::app::showOpenErrorDialog(hwnd, *error);
    }
    return true;
}

// Ctrl+N (WI-02) - confirmDiscardIfDirty() first, then resets `document` to
// a fresh blank Document IN PLACE (move-assignment onto the existing
// object, not a locally-scoped replacement - CommandDispatcher and other
// collaborators were bound to this specific Document instance at
// construction and must keep pointing at it). Ctrl+N never calls
// openDocumentAt() (there is no file to load), so it does not get that
// function's internal reset for free - this mirrors it explicitly
// (dispatcher.resetUndoHistory()/bookmarks.clear()/both selection anchors/
// freeCursorVirtualColumns, exactly as document_open.cpp's openDocumentAt()
// does after its own move-assignment). Skipping this was a real
// data-corruption path found during this WI's design review: a stale Undo
// entry from the PREVIOUS document splices its deleted text into the new
// blank document's start on Ctrl+Z (PieceTable::insert() silently clamps
// an out-of-range offset to 0 rather than rejecting it) - see
// resetViewAfterDocumentSwap()'s own comment.
bool handleNewDocumentKey(HWND hwnd, UINT vkCode, bool ctrlDown, Document& document,
                          CommandDispatcher& dispatcher, SelectionModel& selectionModel,
                          Viewport& viewport, RenderPipeline& renderPipeline, BookmarkManager& bookmarks,
                          FoldingModel& foldingModel, FindReplaceState& findReplaceState, FindBar& findBar,
                          std::optional<neomifes::document::TextPos>& altCursorAnchor,
                          std::optional<neomifes::document::TextPos>& rectangularAnchor,
                          std::optional<std::uint32_t>& freeCursorVirtualColumns,
                          std::optional<std::filesystem::path>& currentDocumentPath,
                          DocumentFileState& fileState) {
    if (!ctrlDown || vkCode != 'N') {
        return false;
    }
    if (!confirmDiscardIfDirty(hwnd, document, fileState, currentDocumentPath)) {
        return true;  // user cancelled the unsaved-changes prompt
    }
    document = Document{};
    dispatcher.resetUndoHistory();
    bookmarks.clear();
    altCursorAnchor.reset();
    rectangularAnchor.reset();
    freeCursorVirtualColumns.reset();
    selectionModel.moveAllTo(0);
    viewport.ensureVisible(0, document);
    currentDocumentPath.reset();
    fileState = DocumentFileState{};
    resetViewAfterDocumentSwap(hwnd, renderPipeline, foldingModel, findReplaceState, findBar,
                               currentDocumentPath);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    return true;
}

// Handles WM_KEYDOWN end-to-end: Ctrl+C/X/V first (Phase 4b6c), falling
// through to the regular movement/edit/undo path otherwise. Pulled all the
// way out of wireNormalMode's onKeyDown lambda body (not just the branching
// logic) - a lambda defined inline inside wireNormalMode has its body
// counted toward wireNormalMode's own cognitive complexity even when the
// branching it does is itself delegated to helper functions, so leaving any
// nontrivial control flow in the lambda itself re-creates the problem
// dispatchMouseDown()/handleClipboardKey() were extracted to avoid.
void handleKeyDownEvent(HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown,
                        CommandDispatcher& dispatcher, SelectionModel& selectionModel,
                        Viewport& viewport, Document& document, RenderPipeline& renderPipeline,
                        FindBar& findBar, FindReplaceState& findReplaceState,
                        CommandPalette& commandPalette, GotoLineBar& gotoLineBar, GrepBar& grepBar,
                        OutlinePane& outlinePane, BookmarkManager& bookmarks,
                        FoldingModel& foldingModel, bool freeCursorModeEnabled,
                        std::optional<std::uint32_t>& freeCursorVirtualColumns,
                        std::optional<neomifes::document::TextPos>& altCursorAnchor,
                        std::optional<neomifes::document::TextPos>& rectangularAnchor,
                        std::optional<std::filesystem::path>& currentDocumentPath,
                        DocumentFileState& fileState) {
    if (handleFreeCursorRightArrow(hwnd, vkCode, shiftDown, ctrlDown, freeCursorModeEnabled,
                                   freeCursorVirtualColumns, selectionModel, document, renderPipeline,
                                   viewport)) {
        return;
    }
    // Any other key discards a pending virtual-column count (Phase 4b8e) -
    // "無関係な操作で破棄" rule, same convention as altCursorAnchor/
    // rectangularAnchor above. Forces one repaint here (rather than relying
    // on whichever branch below happens to run) so the caret doesn't stay
    // visually stranded past the real end of the line when the branch that
    // does run turns out to be a no-op (e.g. Ctrl+Z with nothing to undo).
    if (freeCursorVirtualColumns.has_value()) {
        freeCursorVirtualColumns.reset();
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    }
    if (handleCommandPaletteKey(vkCode, shiftDown, ctrlDown, commandPalette)) {
        return;
    }
    if (handleGrepKey(vkCode, shiftDown, ctrlDown, grepBar)) {
        return;
    }
    if (handleOutlineKey(hwnd, vkCode, shiftDown, ctrlDown, document, currentDocumentPath, outlinePane,
                         foldingModel, renderPipeline)) {
        return;
    }
    if (handleGotoLineKey(vkCode, ctrlDown, gotoLineBar)) {
        return;
    }
    if (handleBookmarkKey(hwnd, vkCode, shiftDown, ctrlDown, bookmarks, selectionModel, viewport,
                          document, renderPipeline, foldingModel)) {
        return;
    }
    if (handleTagJumpKey(hwnd, vkCode, document, dispatcher, selectionModel, viewport, bookmarks,
                         renderPipeline, findBar, findReplaceState, foldingModel, altCursorAnchor,
                         rectangularAnchor, freeCursorVirtualColumns, currentDocumentPath, fileState)) {
        return;
    }
    if (handleSaveKey(hwnd, vkCode, shiftDown, ctrlDown, document, fileState, currentDocumentPath)) {
        return;
    }
    if (handleOpenKey(hwnd, vkCode, ctrlDown, document, dispatcher, selectionModel, viewport,
                      renderPipeline, bookmarks, foldingModel, findReplaceState, findBar, altCursorAnchor,
                      rectangularAnchor, freeCursorVirtualColumns, currentDocumentPath, fileState)) {
        return;
    }
    if (handleNewDocumentKey(hwnd, vkCode, ctrlDown, document, dispatcher, selectionModel, viewport,
                             renderPipeline, bookmarks, foldingModel, findReplaceState, findBar,
                             altCursorAnchor, rectangularAnchor, freeCursorVirtualColumns,
                             currentDocumentPath, fileState)) {
        return;
    }
    if (handleFindBarKey(hwnd, vkCode, shiftDown, ctrlDown, findBar, findReplaceState, selectionModel,
                         viewport, document, renderPipeline)) {
        return;
    }
    const auto clipboardResult =
        handleClipboardKey(hwnd, vkCode, ctrlDown, dispatcher, selectionModel, viewport, document);
    if (clipboardResult.handled) {
        if (clipboardResult.changed) {
            syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
        }
        return;
    }
    const bool changed = neomifes::app::handleKeyDown(vkCode, shiftDown, ctrlDown, dispatcher,
                                                      selectionModel, viewport, document, &foldingModel);
    if (changed) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    }
}

// Handles WM_CHAR: free-cursor materialization (Phase 4b8e, applyFreeCursorChar()
// above) takes priority over the regular insert-at-every-cursor path
// (neomifes::app::handleChar()) whenever a virtual-column count is pending.
// Pulled out of wireNormalMode's onChar lambda for the same cognitive-
// complexity reason as handleKeyDownEvent() above.
void handleCharEvent(HWND hwnd, wchar_t ch, CommandDispatcher& dispatcher,
                     SelectionModel& selectionModel, Viewport& viewport, const Document& document,
                     RenderPipeline& renderPipeline,
                     std::optional<std::uint32_t>& freeCursorVirtualColumns) {
    if (freeCursorVirtualColumns) {
        applyFreeCursorChar(ch, *freeCursorVirtualColumns, hwnd, dispatcher, selectionModel, viewport,
                           document, renderPipeline);
        freeCursorVirtualColumns.reset();
        return;
    }
    const bool changed = neomifes::app::handleChar(ch, dispatcher, selectionModel, viewport, document);
    if (changed) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    }
}

// WI-03: standard Win32 scroll-code decode for WM_HSCROLL - nullopt for
// SB_ENDSCROLL and anything else unrecognized (a no-op). Every switch branch
// returns directly (no intermediate "declare then overwrite" variable) so
// clang-analyzer-deadcode.DeadStores has nothing to flag - an initial value
// that's unconditionally overwritten before any read is exactly what that
// check exists to catch, and a straight `switch { case: return X; ... }`
// shape simply never creates one.
[[nodiscard]] std::optional<std::uint32_t> computeHScrollTargetColumn(
    WORD scrollCode, WORD scrollPos, std::uint32_t currentColumn, std::uint32_t pageStep) noexcept {
    switch (scrollCode) {
        case SB_LINELEFT:
            return currentColumn > 0 ? currentColumn - 1 : 0;
        case SB_LINERIGHT:
            return currentColumn + 1;
        case SB_PAGELEFT:
            return currentColumn > pageStep ? currentColumn - pageStep : 0;
        case SB_PAGERIGHT:
            return currentColumn + pageStep;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            return scrollPos;
        default:
            return std::nullopt;
    }
}

// WI-03: handles WM_HSCROLL (this window's first-ever scrollbar). Page step
// uses renderPipeline.visibleColumnCount() (falls back to 1 column if the
// text area hasn't been sized/measured yet, so PageLeft/PageRight always
// make forward progress instead of silently doing nothing). Extracted to a
// standalone function (not left inline in wireNormalMode's cfg.onHScroll
// lambda) - the scroll-code switch's nesting pushed wireNormalMode's own
// cognitive complexity over clang-tidy's threshold, same "pull the lambda
// body out" fix this codebase's other onXxx handlers already needed (see
// handleKeyDownEvent()/handleCharEvent() above).
void handleHScrollEvent(HWND hwnd, WORD scrollCode, WORD scrollPos, Viewport& viewport,
                        const SelectionModel& selectionModel, RenderPipeline& renderPipeline) {
    const std::uint32_t pageStep = std::max<std::uint32_t>(renderPipeline.visibleColumnCount(), 1);
    const auto newColumn =
        computeHScrollTargetColumn(scrollCode, scrollPos, viewport.leftColumn(), pageStep);
    if (!newColumn) {
        return;  // SB_ENDSCROLL etc - nothing to do
    }
    viewport.scrollToColumn(*newColumn);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
}

// Handles WM_SYSKEYDOWN (Phase 4b8g): Shift+Alt+arrows extends/starts a
// keyboard-driven rectangular selection, reusing `rectangularAnchor` - the
// same session state Shift+Alt+drag already established in Phase 4b8a (see
// dispatchMouseDown()'s comment above) - so a keyboard extension started
// this way can be continued by mouse or vice versa. Shift+Alt+I converts the
// current cursor/selection set into one cursor at the end of each spanned
// line (SelectionModel::convertToLineEndCursors()). Returns false for
// anything else (including plain Alt combos with no Shift) so MainWindow
// falls through to DefWindowProcW - see main_window.h's onSysKeyDown comment
// for why that fallthrough is mandatory (Alt+F4 etc.).
//
// Known limitation: unlike ordinary vertical movement (Viewport-independent
// column memory isn't tracked here), stepping through a shorter line with
// Shift+Alt+Up/Down does not remember the column to return to once a longer
// line follows - each step re-derives purely from the immediately preceding
// step's own (possibly already-clamped) column, same simplification the
// approved plan documents.
bool handleSysKeyDownEvent(HWND hwnd, UINT vkCode, bool shiftDown, SelectionModel& selectionModel,
                           Viewport& viewport, const Document& document,
                           RenderPipeline& renderPipeline,
                           std::optional<neomifes::document::TextPos>& rectangularAnchor) {
    if (!shiftDown) {
        return false;
    }
    if (vkCode == 'I') {
        selectionModel.convertToLineEndCursors(document);
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
        return true;
    }
    MovementKind kind{};
    switch (vkCode) {
        case VK_LEFT:  kind = MovementKind::Left;  break;
        case VK_RIGHT: kind = MovementKind::Right; break;
        case VK_UP:    kind = MovementKind::Up;    break;
        case VK_DOWN:  kind = MovementKind::Down;  break;
        default:       return false;
    }
    if (!rectangularAnchor) {
        rectangularAnchor = selectionModel.primaryCursor().position;
    }
    const auto newActive = moveTextPos(kind, document, selectionModel.primaryCursor().position);
    selectionModel.setRectangularSelection(*rectangularAnchor, newActive, document);
    viewport.ensureVisible(newActive, document);
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    return true;
}

// Builds the FindBarConfig callbacks (Phase 5b3a) - pulled out of
// wireNormalMode's onDeferredInit lambda for the same cognitive-complexity
// reason documented above handleKeyDownEvent(). All captured references
// outlive the returned FindBarConfig (they are wWinMain-scope locals; the
// config itself is only used immediately, inside findBar.create()).
FindBarConfig buildFindBarConfig(HWND hwnd, Document& document, CommandDispatcher& dispatcher,
                                 SelectionModel& selectionModel, Viewport& viewport,
                                 RenderPipeline& renderPipeline, FindBar& findBar,
                                 FindReplaceState& findReplaceState, SearchHistory& searchHistory) {
    FindBarConfig config{};
    config.onQueryChanged = [hwnd, &document, &findReplaceState, &selectionModel, &viewport,
                             &renderPipeline, &findBar](std::u16string_view query, bool caseSensitive,
                                                        bool wholeWord, bool regex) {
        runFindQuery(query, caseSensitive, wholeWord, regex, hwnd, document, findReplaceState,
                    selectionModel, viewport, renderPipeline, findBar);
    };
    // Recording happens here (Enter/F3 while the find edit itself has
    // focus), not inside navigateToMatch() - this is the one call site that
    // covers "the user typed a query and asked to act on it" for every
    // realistic flow (Ctrl+F -> type -> Enter). Subsequent F3 presses after
    // focus has moved to the document (handleFindBarKey()) or via the
    // command palette's Find Next/Previous re-record the SAME
    // already-recorded query - record()'s dedupe makes that a harmless
    // no-op move-to-front rather than a second entry, so those other call
    // sites don't also need searchHistory threaded through them (Phase 5c5).
    config.onHistoryOlder = [&findBar, &searchHistory](std::u16string_view currentText) {
        if (const auto older = searchHistory.older(currentText)) {
            findBar.setQueryText(*older);
        }
    };
    config.onHistoryNewer = [&findBar, &searchHistory](std::u16string_view currentText) {
        if (const auto newer = searchHistory.newer(currentText)) {
            findBar.setQueryText(*newer);
        }
    };
    config.onFindNext = [hwnd, &findReplaceState, &selectionModel, &viewport, &document,
                         &renderPipeline, &findBar, &searchHistory]() {
        searchHistory.record(findReplaceState.currentQuery.pattern);
        navigateToMatch(true, hwnd, findReplaceState, selectionModel, viewport, document,
                        renderPipeline, findBar);
    };
    config.onFindPrevious = [hwnd, &findReplaceState, &selectionModel, &viewport, &document,
                             &renderPipeline, &findBar, &searchHistory]() {
        searchHistory.record(findReplaceState.currentQuery.pattern);
        navigateToMatch(false, hwnd, findReplaceState, selectionModel, viewport, document,
                        renderPipeline, findBar);
    };
    config.onClosed = [hwnd, &findBar, &findReplaceState, &renderPipeline]() {
        closeFindBar(hwnd, findBar, findReplaceState, renderPipeline);
    };
    config.onReplaceCurrent = [hwnd, &document, &dispatcher, &findReplaceState, &selectionModel,
                               &viewport, &renderPipeline, &findBar](std::u16string_view replacementText) {
        replaceCurrentMatch(replacementText, hwnd, document, dispatcher, findReplaceState,
                           selectionModel, viewport, renderPipeline, findBar);
    };
    config.onReplaceAll = [hwnd, &document, &dispatcher, &selectionModel, &findReplaceState,
                           &renderPipeline, &findBar](std::u16string_view replacementText) {
        replaceAllMatches(replacementText, hwnd, document, dispatcher, selectionModel, findReplaceState,
                         renderPipeline, findBar);
    };
    return config;
}

// Builds the command palette's static registry (Phase 5b3c, extended in
// Phase 4b8d/4b8e) - 9 entries, each re-exposing an already-implemented
// keybinding or document-wide action through the palette (Find/Find+Replace/
// Find Next/Find Previous/Undo/Redo/Convert Tabs to Spaces/Convert Spaces to
// Tabs/Toggle Free Cursor Mode). None of the last 3 has a dedicated
// keybinding - the palette is their only entry point, same as any editor's
// "no default shortcut, command palette only" commands. Deliberately does
// not invent commands for features this project hasn't built yet (File
// Open/Save has no runtime UI - see this file's header comment), matching
// CLAUDE.md rule 3 (no speculative implementation). Pulled out of
// wireNormalMode's onDeferredInit lambda for the same cognitive-complexity
// reason documented above handleKeyDownEvent().
std::vector<CommandDescriptor> buildCommandRegistry(
    HWND hwnd, FindBar& findBar, CommandDispatcher& dispatcher, FindReplaceState& findReplaceState,
    SelectionModel& selectionModel, Viewport& viewport, Document& document,
    RenderPipeline& renderPipeline, FoldingModel& foldingModel, bool& freeCursorModeEnabled,
    std::optional<std::uint32_t>& freeCursorVirtualColumns) {
    std::vector<CommandDescriptor> commands;
    commands.push_back(CommandDescriptor{.id              = u"find.show",
                                         .title           = u"Find",
                                         .keybindingLabel = u"Ctrl+F",
                                         .action          = [&findBar]() { findBar.show(); }});
    commands.push_back(
        CommandDescriptor{.id              = u"find.replace",
                          .title           = u"Find and Replace",
                          .keybindingLabel = u"Ctrl+H",
                          .action          = [&findBar]() { findBar.showWithReplace(); }});
    commands.push_back(CommandDescriptor{
        .id = u"find.next", .title = u"Find Next", .keybindingLabel = u"F3",
        .action = [hwnd, &findReplaceState, &selectionModel, &viewport, &document, &renderPipeline,
                   &findBar]() {
            navigateToMatch(true, hwnd, findReplaceState, selectionModel, viewport, document,
                            renderPipeline, findBar);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"find.previous", .title = u"Find Previous", .keybindingLabel = u"Shift+F3",
        .action = [hwnd, &findReplaceState, &selectionModel, &viewport, &document, &renderPipeline,
                   &findBar]() {
            navigateToMatch(false, hwnd, findReplaceState, selectionModel, viewport, document,
                            renderPipeline, findBar);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.undo", .title = u"Undo", .keybindingLabel = u"Ctrl+Z",
        .action = [hwnd, &dispatcher, &selectionModel, &viewport, &renderPipeline]() {
            if (dispatcher.undo()) {
                syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
            }
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.redo", .title = u"Redo", .keybindingLabel = u"Ctrl+Y",
        .action = [hwnd, &dispatcher, &selectionModel, &viewport, &renderPipeline]() {
            if (dispatcher.redo()) {
                syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
            }
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.convertTabsToSpaces", .title = u"Convert Tabs to Spaces", .keybindingLabel = u"",
        .action = [hwnd, &document, &dispatcher, &selectionModel]() {
            applyIndentationConversion(IndentationConversionTarget::TabsToSpaces, hwnd, document,
                                       dispatcher, selectionModel);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.convertSpacesToTabs", .title = u"Convert Spaces to Tabs", .keybindingLabel = u"",
        .action = [hwnd, &document, &dispatcher, &selectionModel]() {
            applyIndentationConversion(IndentationConversionTarget::SpacesToTabs, hwnd, document,
                                       dispatcher, selectionModel);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"view.toggleFoldAtCursor", .title = u"Fold/Unfold at Cursor", .keybindingLabel = u"",
        // Phase 7i: v1 requires the primary cursor to sit exactly on a fold
        // header line - no-op otherwise (see the Phase 7i plan's Context
        // point 6 for why gutter-click toggling is deferred to a later
        // sub-phase; this command is the only way to toggle a fold for now).
        .action = [hwnd, &document, &selectionModel, &renderPipeline, &foldingModel]() {
            const auto line = document.offsetToLine(selectionModel.primaryCursor().position);
            if (!foldingModel.isFoldHeader(line)) {
                return;
            }
            foldingModel.toggleFold(line);
            syncFoldingState(hwnd, renderPipeline, foldingModel);
        }});
    commands.push_back(CommandDescriptor{
        .id = u"edit.toggleFreeCursorMode", .title = u"Toggle Free Cursor Mode",
        .keybindingLabel = u"",
        .action = [hwnd, &renderPipeline, &selectionModel, &viewport, &freeCursorModeEnabled,
                   &freeCursorVirtualColumns]() {
            freeCursorModeEnabled = !freeCursorModeEnabled;
            // Turning the mode off (or back on) mid-way through a pending
            // virtual-column count would otherwise leave the caret rendered
            // past the real end of the line with nothing left able to
            // materialize or reset it - same "discard on unrelated action"
            // rule as handleKeyDownEvent()'s reset above.
            if (freeCursorVirtualColumns) {
                freeCursorVirtualColumns.reset();
                syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
            }
        }});
    return commands;
}

// Parses and applies a GotoLineBar submission (Phase 4b8b). Both `target.line`
// and `target.column` are 1-based (Ctrl+G's user-facing convention, per
// goto_line_parser.h); converted to this project's 0-based LineNumber/column
// here at the single point of use. An out-of-range line clamps to the last
// line (same "never throw on a stale/bad user input" convention as
// SelectionModel's own clamping); a column beyond the target line's actual
// length clamps to that line's end.
void jumpToGotoTarget(const neomifes::ui::GotoTarget& target, HWND hwnd, Document& document,
                      SelectionModel& selectionModel, Viewport& viewport,
                      RenderPipeline& renderPipeline, FoldingModel& foldingModel) {
    const auto lastLine  = document.lineCount() > 0 ? document.lineCount() - 1 : 0;
    const auto line      = std::min(target.line - 1, lastLine);
    const auto lineStart = document.lineToOffset(line);
    const auto lineEnd =
        (line + 1 < document.lineCount()) ? document.lineToOffset(line + 1) - 1 : document.length();
    const auto column = target.column.value_or(1) - 1;
    const auto pos     = std::min(lineStart + column, lineEnd);

    selectionModel.moveAllTo(pos);
    viewport.ensureVisible(pos, document);
    // Phase 7i: Ctrl+G can target a line that's since been folded - reveal it
    // (same document, so unlike the cross-file jumps this can genuinely
    // still be a real, meaningful line to unfold rather than clear).
    if (foldingModel.revealLine(line)) {
        syncFoldingState(hwnd, renderPipeline, foldingModel);
    }
    syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
}

// Builds the GotoLineBarConfig callbacks (Phase 4b8b) - same extraction
// rationale as buildFindBarConfig()/buildCommandRegistry() above.
GotoLineBarConfig buildGotoLineBarConfig(HWND hwnd, Document& document, SelectionModel& selectionModel,
                                         Viewport& viewport, RenderPipeline& renderPipeline,
                                         GotoLineBar& gotoLineBar, FoldingModel& foldingModel) {
    GotoLineBarConfig config{};
    config.onSubmit = [hwnd, &document, &selectionModel, &viewport, &renderPipeline, &foldingModel,
                       &gotoLineBar](std::u16string_view input) {
        const auto target = neomifes::ui::parseGotoLineInput(input);
        if (target) {
            jumpToGotoTarget(*target, hwnd, document, selectionModel, viewport, renderPipeline,
                            foldingModel);
        }
        gotoLineBar.hide();
        ::SetFocus(hwnd);
    };
    config.onClosed = [hwnd, &gotoLineBar]() {
        gotoLineBar.hide();
        ::SetFocus(hwnd);
    };
    return config;
}

// GrepBarConfig::onRunQuery (Phase 5c3) - builds a search::GrepQuery from
// GrepBar's raw text fields and runs it synchronously. See grep_bar.h's
// class comment for why this is Enter-triggered rather than live/debounced:
// a directory-wide Grep is far more expensive than Find bar's single-document
// incremental search, and this codebase has no async infrastructure to keep
// the UI responsive while one runs.
void runGrepQuery(std::u16string_view queryText, std::u16string_view folderText, GrepState& grepState,
                  GrepBar& grepBar, SearchHistory& searchHistory) {
    // Enter is GrepBar's only trigger (no debounce/live-refresh - see
    // grep_bar.h's class comment), so this single call site covers every
    // way a Grep query actually runs (Phase 5c5).
    searchHistory.record(queryText);
    const auto query = neomifes::app::buildGrepQueryFromInput(queryText, folderText);
    grepState.currentResults = query ? GrepService::findAll(*query) : std::vector<GrepMatch>{};

    std::vector<std::u16string> rows;
    rows.reserve(grepState.currentResults.size());
    for (const auto& match : grepState.currentResults) {
        rows.push_back(neomifes::app::formatGrepResultRow(match));
    }
    grepBar.setResults(rows);
}

// GrepBarConfig::onResultActivated (Phase 5c3) - opens the file a Grep
// result points to via neomifes::app::openDocumentAt() (Phase 5c2), then
// performs the reset sequence that function's header comment explicitly
// leaves to the caller: RenderPipeline's cached match/bookmark visuals and
// FindBar's match count are separate from what openDocumentAt() itself
// already resets internally (undo history, BookmarkManager, both selection
// anchors, free-cursor virtual columns - see document_open.h). Mirrors
// replaceAllMatches()'s reset sequence above.
void jumpToGrepResult(std::size_t resultIndex, HWND hwnd, GrepState& grepState, Document& document,
                      CommandDispatcher& dispatcher, SelectionModel& selectionModel, Viewport& viewport,
                      BookmarkManager& bookmarks, RenderPipeline& renderPipeline, FindBar& findBar,
                      FindReplaceState& findReplaceState, FoldingModel& foldingModel,
                      std::optional<neomifes::document::TextPos>& altCursorAnchor,
                      std::optional<neomifes::document::TextPos>& rectangularAnchor,
                      std::optional<std::uint32_t>& freeCursorVirtualColumns,
                      std::optional<std::filesystem::path>& currentDocumentPath,
                      DocumentFileState& fileState) {
    if (resultIndex >= grepState.currentResults.size()) {
        return;
    }
    const GrepMatch& match = grepState.currentResults[resultIndex];
    // Stale result (file moved/deleted since the Grep ran) - openAndResetTo()
    // leaves everything untouched on failure, same silent no-op contract as
    // before this WI. No error-toast UI exists yet to surface this.
    (void)openAndResetTo(match.path, match.line, match.columnRange.start, hwnd, document, dispatcher,
                         selectionModel, viewport, renderPipeline, bookmarks, foldingModel,
                         findReplaceState, findBar, altCursorAnchor, rectangularAnchor,
                         freeCursorVirtualColumns, currentDocumentPath, fileState);
}

// Builds the GrepBarConfig callbacks (Phase 5c3) - same extraction rationale
// as buildFindBarConfig()/buildGotoLineBarConfig() above.
GrepBarConfig buildGrepBarConfig(HWND hwnd, Document& document, CommandDispatcher& dispatcher,
                                 SelectionModel& selectionModel, Viewport& viewport,
                                 BookmarkManager& bookmarks, RenderPipeline& renderPipeline,
                                 FindBar& findBar, FindReplaceState& findReplaceState, GrepBar& grepBar,
                                 GrepState& grepState, SearchHistory& searchHistory,
                                 FoldingModel& foldingModel,
                                 std::optional<neomifes::document::TextPos>& altCursorAnchor,
                                 std::optional<neomifes::document::TextPos>& rectangularAnchor,
                                 std::optional<std::uint32_t>& freeCursorVirtualColumns,
                                 std::optional<std::filesystem::path>& currentDocumentPath,
                                 DocumentFileState& fileState) {
    GrepBarConfig config{};
    config.onRunQuery = [&grepState, &grepBar, &searchHistory](std::u16string_view queryText,
                                                                std::u16string_view folderText) {
        runGrepQuery(queryText, folderText, grepState, grepBar, searchHistory);
    };
    config.onHistoryOlder = [&grepBar, &searchHistory](std::u16string_view currentText) {
        if (const auto older = searchHistory.older(currentText)) {
            grepBar.setQueryText(*older);
        }
    };
    config.onHistoryNewer = [&grepBar, &searchHistory](std::u16string_view currentText) {
        if (const auto newer = searchHistory.newer(currentText)) {
            grepBar.setQueryText(*newer);
        }
    };
    config.onResultActivated = [hwnd, &grepState, &document, &dispatcher, &selectionModel, &viewport,
                                &bookmarks, &renderPipeline, &findBar, &findReplaceState, &foldingModel,
                                &altCursorAnchor, &rectangularAnchor, &freeCursorVirtualColumns,
                                &currentDocumentPath, &fileState](std::size_t resultIndex) {
        jumpToGrepResult(resultIndex, hwnd, grepState, document, dispatcher, selectionModel, viewport,
                        bookmarks, renderPipeline, findBar, findReplaceState, foldingModel,
                        altCursorAnchor, rectangularAnchor, freeCursorVirtualColumns, currentDocumentPath,
                        fileState);
    };
    config.onClosed = [hwnd, &grepBar]() {
        grepBar.hide();
        ::SetFocus(hwnd);
    };
    return config;
}

// Builds OutlinePaneConfig, creates the panel, and - if that succeeds -
// primes its initial position/size (Phase 7g bug fix, discovered via
// EnumChildWindows during visual verification): onDeferredInit runs via a
// POSTED message, strictly after the WM_SIZE that fires as part of
// MainWindow::create()'s own ShowWindow() call, so by the time it runs that
// one-off WM_SIZE has already come and gone and cfg.onResize (hence
// outlinePane.onParentResized()) will not fire again until the user manually
// resizes the window. Without the explicit priming call here, the panel
// stays stuck at its CreateWindowExW-time placeholder rect (0,0,10,10) the
// first time it's shown. (The four other overlays created in
// wireNormalMode()'s onDeferredInit below have this same latent gap; fixing
// them is out of scope here - see the Phase 7g completion notes.) Pulled out
// to its own function - same cognitive-complexity reason as
// handleKeyDownEvent()/handleCharEvent() above - rather than left inline in
// onDeferredInit's already-long lambda body.
void createAndPositionOutlinePane(HWND hwnd, HINSTANCE hInstance, Document& document,
                                  SelectionModel& selectionModel, Viewport& viewport,
                                  RenderPipeline& renderPipeline, OutlinePane& outlinePane) {
    OutlinePaneConfig config{};
    config.onItemSelected = [hwnd, &document, &selectionModel, &viewport,
                             &renderPipeline](std::uint64_t targetPos) {
        jumpToOutlinePosition(targetPos, hwnd, document, selectionModel, viewport, renderPipeline);
    };
    config.onClosed = [hwnd]() { ::SetFocus(hwnd); };
    if (!outlinePane.create(hwnd, hInstance, config)) {
        return;
    }
    RECT clientRect{};
    ::GetClientRect(hwnd, &clientRect);
    const auto dpiScale = static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0F;
    outlinePane.onParentResized(static_cast<std::uint32_t>(clientRect.right),
                                static_cast<std::uint32_t>(clientRect.bottom), dpiScale);
}

// WI-02: cfg.onDropFiles body, pulled out of wireNormalMode() for the same
// cognitive-complexity reason as handleMouseDownEvent()/handleKeyDownEvent()
// above. Multi-file drops open only the first path (no multi-tab yet, see
// build_plan.md's explicit WI-02 scope cut - multi-tab is WI-05).
void handleDropFilesEvent(HWND hwnd, std::vector<std::wstring> paths, Document& document,
                          CommandDispatcher& dispatcher, SelectionModel& selectionModel,
                          Viewport& viewport, RenderPipeline& renderPipeline, BookmarkManager& bookmarks,
                          FoldingModel& foldingModel, FindReplaceState& findReplaceState, FindBar& findBar,
                          std::optional<neomifes::document::TextPos>& altCursorAnchor,
                          std::optional<neomifes::document::TextPos>& rectangularAnchor,
                          std::optional<std::uint32_t>& freeCursorVirtualColumns,
                          std::optional<std::filesystem::path>& currentDocumentPath,
                          DocumentFileState& fileState) {
    if (paths.empty()) {
        return;
    }
    if (!confirmDiscardIfDirty(hwnd, document, fileState, currentDocumentPath)) {
        return;
    }
    const auto error = openAndResetTo(paths.front(), std::nullopt, std::nullopt, hwnd, document,
                                      dispatcher, selectionModel, viewport, renderPipeline, bookmarks,
                                      foldingModel, findReplaceState, findBar, altCursorAnchor,
                                      rectangularAnchor, freeCursorVirtualColumns, currentDocumentPath,
                                      fileState);
    if (error) {
        neomifes::app::showOpenErrorDialog(hwnd, *error);
    }
}

// Real launches only - deferred so it never affects firstPaintNs timing
// (ADR-009). If attach() fails, the window simply keeps the GDI placeholder
// forever; there is no retry policy. Same non-fatal treatment for
// findBar.create() (Phase 5b3a) - a Find bar that fails to create simply
// isn't available this session, no retry policy either.
void wireNormalMode(MainWindowConfig& cfg, MainWindow& window, RenderPipeline& renderPipeline,
                    Document& document, CommandDispatcher& dispatcher, SelectionModel& selectionModel,
                    Viewport& viewport, std::optional<neomifes::document::TextPos>& altCursorAnchor,
                    std::optional<neomifes::document::TextPos>& rectangularAnchor, HINSTANCE hInstance,
                    FindBar& findBar, FindReplaceState& findReplaceState, CommandPalette& commandPalette,
                    GotoLineBar& gotoLineBar, GrepBar& grepBar, GrepState& grepState,
                    SearchHistory& searchHistory, OutlinePane& outlinePane, BookmarkManager& bookmarks,
                    FoldingModel& foldingModel, bool& freeCursorModeEnabled,
                    std::optional<std::uint32_t>& freeCursorVirtualColumns,
                    std::optional<std::filesystem::path>& currentDocumentPath, DocumentFileState& fileState,
                    bool& isDraggingMinimap) {
    cfg.onDeferredInit = [&window, &renderPipeline, &document, &dispatcher, hInstance, &findBar,
                          &selectionModel, &viewport, &findReplaceState, &commandPalette, &gotoLineBar,
                          &grepBar, &grepState, &searchHistory, &outlinePane, &bookmarks, &foldingModel,
                          &altCursorAnchor, &rectangularAnchor, &freeCursorModeEnabled,
                          &freeCursorVirtualColumns, &currentDocumentPath, &fileState](HWND hwnd) {
        const auto attached = renderPipeline.attach(hwnd);
        if (!attached) {
            debugLogRenderError("RenderPipeline::attach", attached.error());
            return;
        }
        renderPipeline.setDocument(&document);
        window.setPaintHandler([&renderPipeline, &viewport](HWND paintHwnd) {
            const auto rendered = renderPipeline.render();
            if (!rendered) {
                debugLogRenderError("RenderPipeline::render", rendered.error());
                return;
            }
            // WI-03: kept fresh every successful frame rather than only on
            // WM_SIZE - m_charWidthDips (which visibleColumnCount() depends
            // on) isn't measured until the FIRST render() call completes, so
            // relying on resize() alone would leave this at 0 until the user
            // manually resized the window. See syncHorizontalScrollBar()'s
            // comment for why the scrollbar sync itself belongs here too.
            viewport.setVisibleColumnCount(renderPipeline.visibleColumnCount());
            syncHorizontalScrollBar(paintHwnd, renderPipeline, viewport);
        });
        const FindBarConfig findBarConfig =
            buildFindBarConfig(hwnd, document, dispatcher, selectionModel, viewport, renderPipeline,
                               findBar, findReplaceState, searchHistory);
        [[maybe_unused]] const bool findBarCreated = findBar.create(hwnd, hInstance, findBarConfig);

        // Same non-fatal treatment as findBar.create() above - a palette
        // that fails to create simply isn't available this session.
        CommandPaletteConfig commandPaletteConfig{};
        commandPaletteConfig.onClosed = [hwnd]() { ::SetFocus(hwnd); };
        auto commands = buildCommandRegistry(hwnd, findBar, dispatcher, findReplaceState, selectionModel,
                                             viewport, document, renderPipeline, foldingModel,
                                             freeCursorModeEnabled, freeCursorVirtualColumns);
        [[maybe_unused]] const bool commandPaletteCreated =
            commandPalette.create(hwnd, hInstance, commandPaletteConfig, std::move(commands));

        // Same non-fatal treatment as findBar.create() above.
        const GotoLineBarConfig gotoLineBarConfig = buildGotoLineBarConfig(
            hwnd, document, selectionModel, viewport, renderPipeline, gotoLineBar, foldingModel);
        [[maybe_unused]] const bool gotoLineBarCreated =
            gotoLineBar.create(hwnd, hInstance, gotoLineBarConfig);

        // Same non-fatal treatment as findBar.create() above.
        const GrepBarConfig grepBarConfig =
            buildGrepBarConfig(hwnd, document, dispatcher, selectionModel, viewport, bookmarks,
                              renderPipeline, findBar, findReplaceState, grepBar, grepState,
                              searchHistory, foldingModel, altCursorAnchor, rectangularAnchor,
                              freeCursorVirtualColumns, currentDocumentPath, fileState);
        [[maybe_unused]] const bool grepBarCreated = grepBar.create(hwnd, hInstance, grepBarConfig);

        // Same non-fatal treatment as findBar.create() above.
        createAndPositionOutlinePane(hwnd, hInstance, document, selectionModel, viewport, renderPipeline,
                                     outlinePane);
        // Phase 7i: seeds FoldingModel's region list once at startup (mirrors
        // renderPipeline.setLanguage()'s own startup timing in wWinMain) so
        // "Fold/Unfold at Cursor" and the gutter markers work immediately,
        // without requiring the user to first open the outline panel
        // (refreshOutlinePane() re-seeds this later from the same parse
        // pattern whenever the panel is opened - see that function's comment).
        foldingModel.setFoldableRegions(
            neomifes::app::buildFoldRegions(extractCurrentOutline(document, currentDocumentPath), document));
        syncFoldingState(hwnd, renderPipeline, foldingModel);
        // Phase 7h: pushes the startup cursor state (position 0, isPrimary)
        // into RenderPipeline before the first paint - without this,
        // m_cursorVisuals stays empty (its default) until the user's first
        // cursor-moving action, which left both the caret and (once added,
        // Phase 7h) the Breadcrumb strip invisible on a freshly opened file.
        // Supersedes the bare InvalidateRect() this replaced - this already
        // invalidates internally.
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    };
    cfg.onResize = [&renderPipeline, &findBar, &commandPalette, &gotoLineBar, &grepBar, &outlinePane](
                       HWND, std::uint32_t w, std::uint32_t h, float dpiScale) {
        if (renderPipeline.isAttached()) {
            const auto resized = renderPipeline.resize(w, h, dpiScale);
            if (!resized) {
                debugLogRenderError("RenderPipeline::resize", resized.error());
            }
        }
        findBar.onParentResized(w, dpiScale);
        commandPalette.onParentResized(w, dpiScale);
        gotoLineBar.onParentResized(w, dpiScale);
        grepBar.onParentResized(w, dpiScale);
        outlinePane.onParentResized(w, h, dpiScale);
    };
    cfg.onCommand = [&findBar, &commandPalette, &grepBar](HWND, WPARAM wParam, LPARAM lParam) {
        findBar.handleCommand(wParam, lParam);
        commandPalette.handleCommand(wParam, lParam);
        grepBar.handleCommand(wParam, lParam);
    };
    // Phase 7g: OutlinePane's WC_TREEVIEW is this codebase's first control
    // that notifies via WM_NOTIFY rather than WM_COMMAND - see
    // MainWindowConfig::onNotify's doc comment (main_window.h).
    cfg.onNotify = [&outlinePane](HWND, WPARAM wParam, LPARAM lParam) {
        return outlinePane.handleNotify(wParam, lParam);
    };
    // Phase 7c: SyntaxWorker's background-thread parse completion signal.
    // MainWindow forwards every WM_APP+ message it doesn't itself interpret
    // here unexamined (neomifes::ui never learns what kMsgSyntaxTokensReady
    // means - see MainWindowConfig::onAppMessage's doc comment); main.cpp
    // is the layer that already depends on both ui:: and render:: so it's
    // the only place that can safely compare against the constant and
    // reconstruct the payload's real type.
    cfg.onAppMessage = [&renderPipeline](HWND hwnd, UINT msg, WPARAM, LPARAM lParam) {
        if (msg != neomifes::render::kMsgSyntaxTokensReady) {
            return;
        }
        const std::unique_ptr<std::vector<neomifes::syntax::Token>> tokens(
            reinterpret_cast<std::vector<neomifes::syntax::Token>*>(lParam));
        renderPipeline.applyAsyncSyntaxTokens(std::move(*tokens));
        ::InvalidateRect(hwnd, nullptr, FALSE);
    };
    // WI-02: WM_CLOSE veto - both go through confirmDiscardIfDirty() so an
    // unsaved edit is never silently discarded by closing the window or
    // dropping a different file onto it.
    cfg.onClose = [&document, &fileState, &currentDocumentPath](HWND hwnd) {
        return confirmDiscardIfDirty(hwnd, document, fileState, currentDocumentPath);
    };
    cfg.onDropFiles = [&document, &dispatcher, &selectionModel, &viewport, &renderPipeline, &bookmarks,
                       &foldingModel, &findReplaceState, &findBar, &altCursorAnchor, &rectangularAnchor,
                       &freeCursorVirtualColumns, &currentDocumentPath,
                       &fileState](HWND hwnd, std::vector<std::wstring> paths) {
        handleDropFilesEvent(hwnd, std::move(paths), document, dispatcher, selectionModel, viewport,
                             renderPipeline, bookmarks, foldingModel, findReplaceState, findBar,
                             altCursorAnchor, rectangularAnchor, freeCursorVirtualColumns,
                             currentDocumentPath, fileState);
    };
    cfg.onKeyDown = [&dispatcher, &selectionModel, &viewport, &document, &renderPipeline, &findBar,
                     &findReplaceState, &commandPalette, &gotoLineBar, &grepBar, &outlinePane,
                     &bookmarks, &foldingModel, &freeCursorModeEnabled, &freeCursorVirtualColumns,
                     &altCursorAnchor, &rectangularAnchor, &currentDocumentPath,
                     &fileState](HWND hwnd, UINT vkCode, bool shiftDown, bool ctrlDown) {
        handleKeyDownEvent(hwnd, vkCode, shiftDown, ctrlDown, dispatcher, selectionModel, viewport,
                          document, renderPipeline, findBar, findReplaceState, commandPalette,
                          gotoLineBar, grepBar, outlinePane, bookmarks, foldingModel,
                          freeCursorModeEnabled, freeCursorVirtualColumns, altCursorAnchor,
                          rectangularAnchor, currentDocumentPath, fileState);
    };
    cfg.onSysKeyDown = [&selectionModel, &viewport, &document, &renderPipeline, &rectangularAnchor](
                           HWND hwnd, UINT vkCode, bool shiftDown) {
        return handleSysKeyDownEvent(hwnd, vkCode, shiftDown, selectionModel, viewport, document,
                                     renderPipeline, rectangularAnchor);
    };
    cfg.onChar = [&dispatcher, &selectionModel, &viewport, &document, &renderPipeline,
                 &freeCursorVirtualColumns](HWND hwnd, wchar_t ch) {
        handleCharEvent(hwnd, ch, dispatcher, selectionModel, viewport, document, renderPipeline,
                       freeCursorVirtualColumns);
    };
    cfg.onMouseWheel = [&viewport, &selectionModel, &renderPipeline, &document](HWND hwnd, short wheelDelta) {
        viewport.scrollTo(
            neomifes::app::applyMouseWheelScroll(wheelDelta, viewport.topLine(), document.lineCount()));
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    };
    // WI-03: this window's first-ever scrollbar (WS_HSCROLL, added by
    // MainWindow::create() only because this handler is set - see
    // MainWindowConfig::onHScroll's comment). Body lives in
    // handleHScrollEvent() (not inline here) - see that function's comment.
    cfg.onHScroll = [&viewport, &selectionModel, &renderPipeline](HWND hwnd, WORD scrollCode, WORD scrollPos) {
        handleHScrollEvent(hwnd, scrollCode, scrollPos, viewport, selectionModel, renderPipeline);
    };
    cfg.onMouseDown = [&selectionModel, &viewport, &document, &renderPipeline, &altCursorAnchor,
                       &rectangularAnchor, &freeCursorVirtualColumns, &foldingModel, &isDraggingMinimap](
                          HWND hwnd, std::int32_t x, std::int32_t y, bool shiftDown, bool altDown,
                          int clickCount) {
        handleMouseDownEvent(hwnd, x, y, shiftDown, altDown, clickCount, selectionModel, viewport,
                             document, renderPipeline, altCursorAnchor, rectangularAnchor,
                             freeCursorVirtualColumns, foldingModel, isDraggingMinimap);
    };
    cfg.onMouseDrag = [&selectionModel, &viewport, &document, &renderPipeline, &altCursorAnchor,
                       &rectangularAnchor, &freeCursorVirtualColumns, &isDraggingMinimap](
                          HWND hwnd, std::int32_t x, std::int32_t y) {
        // Highest priority: a minimap drag never falls through to
        // rectangularAnchor/altCursorAnchor/ordinary text-drag handling
        // below - it tracks by Y alone (Phase 7v, see minimapLineAtY()'s
        // comment on why X is ignored once a drag has started).
        if (isDraggingMinimap) {
            if (const auto targetLine = renderPipeline.minimapLineAtY(y)) {
                viewport.scrollTo(*targetLine);
                syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
            }
            return;
        }
        const auto hit = renderPipeline.hitTest(x, y);
        if (!hit) {
            return;
        }
        freeCursorVirtualColumns.reset();
        // Checked in this priority order: a rectangular-selection drag
        // (Phase 4b8a, Shift+Alt+drag) takes precedence over a plain
        // Alt+drag cursor extension (Phase 4b6d), which takes precedence
        // over the default drag-extends-primary-selection behavior (Phase
        // 4b3). At most one of rectangularAnchor/altCursorAnchor is ever
        // meaningfully set at a time - see dispatchMouseDown()'s comment for
        // why a Shift+Alt+click that turns into a drag safely supersedes
        // whatever the down-click itself did.
        bool changed = false;
        if (rectangularAnchor) {
            selectionModel.setRectangularSelection(*rectangularAnchor, *hit, document);
            // The rectangle just replaced the entire cursor set, so any
            // altCursorAnchor left over from an earlier plain Alt+click no
            // longer identifies a real cursor - clear it so the next
            // unrelated Shift+Alt+click doesn't silently no-op.
            altCursorAnchor.reset();
            viewport.ensureVisible(*hit, document);
            changed = true;
        } else if (altCursorAnchor) {
            selectionModel.moveCursorMatching(*altCursorAnchor, *hit);
            viewport.ensureVisible(*hit, document);
            changed = true;
        } else {
            changed =
                neomifes::app::handleMouseDown(*hit, /*shiftDown=*/true, selectionModel, viewport, document);
        }
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
    // Phase 5b3a: defensive, see initCommonControls()'s comment. Cheap and
    // harmless even on modes that never create a FindBar (Measure*).
    initCommonControls();

    // Declared before window/renderPipeline so it outlives both (reverse
    // destruction order) - RenderPipeline::setDocument() below hands out a
    // non-owning pointer that must not dangle while the message loop runs.
    //
    // WI-02: currentDocumentPath/fileState are declared here (not further
    // down where currentDocumentPath used to live alone) because
    // prepareDocument() populates all three together from the same
    // loadFile() call - see loadStartupDocument()'s comment on why
    // currentDocumentPath must only be set on a SUCCESSFUL load (a stale
    // path pointing at an empty `document` would make a later Ctrl+S wipe
    // the original file, not just mis-color syntax highlighting the way an
    // incorrect path did before saveFile() existed).
    std::uint64_t                        syntheticLineCountUsed = 0;
    std::optional<std::filesystem::path> currentDocumentPath;
    DocumentFileState                    fileState;
    Document document =
        prepareDocument(args, syntheticLineCountUsed, fileState, currentDocumentPath);
    FrameProfile  frameProfile{};

    // Editor Core state (Phase 4b1) - Normal-mode-only in practice (only
    // wireNormalMode's hooks ever touch these), but declared unconditionally
    // like `document` above since CommandDispatcher must be constructed
    // with a valid Document& regardless of launch mode.
    SelectionModel    selectionModel{0};
    CommandDispatcher dispatcher{document, selectionModel};
    Viewport          viewport;
    // Phase 4b6d: anchor of the cursor a plain Alt+click most recently
    // added, so a later Alt+Shift+click or Alt+drag can extend that one
    // cursor specifically. Reset to nullopt by any non-Alt click.
    std::optional<neomifes::document::TextPos> altCursorAnchor;
    // Phase 4b8a: anchor of an in-progress Shift+Alt+drag rectangular
    // selection. Kept as a separate optional (not folded into
    // altCursorAnchor) since the two gestures are deliberately independent -
    // see dispatchMouseDown()'s comment.
    std::optional<neomifes::document::TextPos> rectangularAnchor;
    // Phase 7v: true while a minimap click-and-drag is in progress. Reset to
    // false at the top of every handleMouseDownEvent() call (the only
    // reliable reset point - MainWindow exposes no onMouseUp hook, see
    // handleMouseDownEvent()'s comment) and set back to true only by
    // tryHandleMinimapClick() when the down-click itself lands on the strip.
    bool isDraggingMinimap = false;
    // Find bar state (Phase 5b3a, bundled into FindReplaceState in Phase
    // 5b3b) - lives here (not inside FindBar itself) so FindBar can stay
    // decoupled from neomifes::search, same rationale as core::ReplaceAllCommand
    // staying decoupled from neomifes::search in Phase 5b2 (see
    // docs/history/TIMELINE.md's Phase 5b3a entry).
    FindBar           findBar;
    FindReplaceState findReplaceState;
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
    // findReplaceState above).
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
    // Line bookmarks (Phase 4b8c, Ctrl+F2/F2/Shift+F2) - headless, no Win32
    // overlay of its own; RenderPipeline::setBookmarkedLines() is pushed
    // from handleBookmarkKey() whenever the set changes.
    BookmarkManager bookmarks;
    // Foldable symbol regions (Phase 7i) - headless, same "no Win32 overlay
    // of its own" shape as bookmarks above; RenderPipeline::setFoldRegions()
    // is pushed from syncFoldingState() whenever the region list or a
    // folded flag changes.
    FoldingModel foldingModel;
    // Free cursor mode (Phase 4b8e, simplified - see approved plan). Both
    // are session-lifetime UI state, not document state: freeCursorModeEnabled
    // is toggled via the command palette ("Toggle Free Cursor Mode"), and
    // freeCursorVirtualColumns tracks how many columns past the real end of
    // the primary cursor's line it is currently drawn at, materializing into
    // real spaces the moment a character is typed (applyFreeCursorChar()).
    bool                          freeCursorModeEnabled = false;
    std::optional<std::uint32_t> freeCursorVirtualColumns;
    // Phase 7b: which file (if any) `document` was loaded from - Document
    // itself never tracks this (see syntax_language.h's file comment), and
    // RenderPipeline::setLanguage() needs it to decide which language (if
    // any) to color the current document as. WI-02: now declared/populated
    // together with `document`/`fileState` near the top of wWinMain (see
    // that declaration's comment) rather than here, so it stays accurate
    // for the new Ctrl+S capability too.

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
        wireNormalMode(cfg, window, renderPipeline, document, dispatcher, selectionModel, viewport,
                       altCursorAnchor, rectangularAnchor, hInstance, findBar, findReplaceState,
                       commandPalette, gotoLineBar, grepBar, grepState, searchHistory, outlinePane,
                       bookmarks, foldingModel, freeCursorModeEnabled, freeCursorVirtualColumns,
                       currentDocumentPath, fileState, isDraggingMinimap);
        // Phase 7b/7d: reflect the startup document's language before the
        // first paint - attach() itself happens later inside onDeferredInit,
        // but setLanguage() only touches plain member state, so it's safe to
        // call before RenderPipeline is attached.
        renderPipeline.setLanguage(currentDocumentPath ? neomifes::app::detectLanguage(*currentDocumentPath)
                                                        : std::nullopt);
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
