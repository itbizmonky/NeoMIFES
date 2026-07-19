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

#include "neomifes/app/editor_input.h"
#include "neomifes/core/command_dispatcher.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/replace_all_command.h"
#include "neomifes/core/selection_model.h"
#include "neomifes/core/viewport.h"
#include "neomifes/document/document.h"
#include "neomifes/document/file_loader.h"
#include "neomifes/platform/clipboard.h"
#include "neomifes/platform/handle_guard.h"
#include "neomifes/platform/perf_clock.h"
#include "neomifes/platform/process_metrics.h"
#include "neomifes/render/render_pipeline.h"
#include "neomifes/search/replacement.h"
#include "neomifes/search/search_service.h"
#include "neomifes/ui/command_descriptor.h"
#include "neomifes/ui/command_palette.h"
#include "neomifes/ui/find_bar.h"
#include "neomifes/ui/find_navigation.h"
#include "neomifes/ui/main_window.h"

#include "frame_profile.h"
#include "startup_profile.h"

namespace {

using neomifes::app::FrameProfile;
using neomifes::app::StartupProfile;
using neomifes::core::CommandDispatcher;
using neomifes::core::Cursor;
using neomifes::core::PerCursorEdit;
using neomifes::core::ReplaceAllCommand;
using neomifes::core::ReplaceRangeCommand;
using neomifes::core::SelectionModel;
using neomifes::core::Viewport;
using neomifes::document::Document;
using neomifes::document::LoadError;
using neomifes::document::LoadResult;
using neomifes::document::TextRange;
using neomifes::platform::currentProcessMemory;
using neomifes::platform::KernelHandle;
using neomifes::platform::PerfClock;
using neomifes::render::MatchVisual;
using neomifes::render::RenderPipeline;
using neomifes::search::expandReplacementTemplate;
using neomifes::search::Match;
using neomifes::search::Query;
using neomifes::search::SearchService;
using neomifes::ui::CommandDescriptor;
using neomifes::ui::CommandPalette;
using neomifes::ui::CommandPaletteConfig;
using neomifes::ui::FindBar;
using neomifes::ui::FindBarConfig;
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

// Defensive: FindBar's SetWindowSubclass/DefSubclassProc (Phase 5b3a, first
// comctl32 usage in this codebase) do not strictly require this per
// Microsoft's docs (it is only load-bearing for visual-styles-aware
// controls), but calling it costs nothing and removes any doubt about
// comctl32 being loaded before the first CreateWindowExW(WC_EDITW, ...).
void initCommonControls() noexcept {
    const INITCOMMONCONTROLSEX icc{.dwSize = sizeof(icc), .dwICC = ICC_STANDARD_CLASSES};
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
// (setTopLine/setCursorVisuals take plain document types), so this glue
// lives here in the app layer rather than in either core or render.
// Phase 4b7a: builds one CursorVisual per SelectionModel cursor (not just
// the primary) so every cursor's caret/selection actually gets drawn.
void syncRenderStateAndInvalidate(HWND hwnd, RenderPipeline& renderPipeline,
                                  const SelectionModel& selection, const Viewport& viewport) {
    renderPipeline.setTopLine(viewport.topLine());
    std::vector<neomifes::render::CursorVisual> visuals;
    visuals.reserve(selection.cursors().size());
    for (const auto& cursor : selection.cursors()) {
        visuals.push_back(neomifes::render::CursorVisual{
            .position       = cursor.position,
            .selectionRange = TextRange{.start = std::min(cursor.position, cursor.anchor),
                                        .end     = std::max(cursor.position, cursor.anchor)},
        });
    }
    renderPipeline.setCursorVisuals(std::move(visuals));
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
    if (ctrlDown && vkCode == 'F') {
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
                        Viewport& viewport, const Document& document, RenderPipeline& renderPipeline,
                        FindBar& findBar, FindReplaceState& findReplaceState,
                        CommandPalette& commandPalette) {
    if (handleCommandPaletteKey(vkCode, shiftDown, ctrlDown, commandPalette)) {
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
                                                      selectionModel, viewport, document);
    if (changed) {
        syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
    }
}

// Builds the FindBarConfig callbacks (Phase 5b3a) - pulled out of
// wireNormalMode's onDeferredInit lambda for the same cognitive-complexity
// reason documented above handleKeyDownEvent(). All captured references
// outlive the returned FindBarConfig (they are wWinMain-scope locals; the
// config itself is only used immediately, inside findBar.create()).
FindBarConfig buildFindBarConfig(HWND hwnd, Document& document, CommandDispatcher& dispatcher,
                                 SelectionModel& selectionModel, Viewport& viewport,
                                 RenderPipeline& renderPipeline, FindBar& findBar,
                                 FindReplaceState& findReplaceState) {
    FindBarConfig config{};
    config.onQueryChanged = [hwnd, &document, &findReplaceState, &selectionModel, &viewport,
                             &renderPipeline, &findBar](std::u16string_view query, bool caseSensitive,
                                                        bool wholeWord, bool regex) {
        runFindQuery(query, caseSensitive, wholeWord, regex, hwnd, document, findReplaceState,
                    selectionModel, viewport, renderPipeline, findBar);
    };
    config.onFindNext = [hwnd, &findReplaceState, &selectionModel, &viewport, &document,
                         &renderPipeline, &findBar]() {
        navigateToMatch(true, hwnd, findReplaceState, selectionModel, viewport, document,
                        renderPipeline, findBar);
    };
    config.onFindPrevious = [hwnd, &findReplaceState, &selectionModel, &viewport, &document,
                             &renderPipeline, &findBar]() {
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

// Builds the command palette's static registry (Phase 5b3c) - 6 entries,
// each re-exposing an already-implemented keybinding through the palette
// (Find/Find+Replace/Find Next/Find Previous/Undo/Redo). Deliberately does
// not invent commands for features this project hasn't built yet (File
// Open/Save has no runtime UI - see this file's header comment), matching
// CLAUDE.md rule 3 (no speculative implementation). Pulled out of
// wireNormalMode's onDeferredInit lambda for the same cognitive-complexity
// reason documented above handleKeyDownEvent().
std::vector<CommandDescriptor> buildCommandRegistry(HWND hwnd, FindBar& findBar,
                                                     CommandDispatcher& dispatcher,
                                                     FindReplaceState& findReplaceState,
                                                     SelectionModel& selectionModel, Viewport& viewport,
                                                     Document& document, RenderPipeline& renderPipeline) {
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
    return commands;
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
                    FindBar& findBar, FindReplaceState& findReplaceState, CommandPalette& commandPalette) {
    cfg.onDeferredInit = [&window, &renderPipeline, &document, &dispatcher, hInstance, &findBar,
                          &selectionModel, &viewport, &findReplaceState, &commandPalette](HWND hwnd) {
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
        const FindBarConfig findBarConfig =
            buildFindBarConfig(hwnd, document, dispatcher, selectionModel, viewport, renderPipeline,
                               findBar, findReplaceState);
        [[maybe_unused]] const bool findBarCreated = findBar.create(hwnd, hInstance, findBarConfig);

        // Same non-fatal treatment as findBar.create() above - a palette
        // that fails to create simply isn't available this session.
        CommandPaletteConfig commandPaletteConfig{};
        commandPaletteConfig.onClosed = [hwnd]() { ::SetFocus(hwnd); };
        auto commands = buildCommandRegistry(hwnd, findBar, dispatcher, findReplaceState, selectionModel,
                                             viewport, document, renderPipeline);
        [[maybe_unused]] const bool commandPaletteCreated =
            commandPalette.create(hwnd, hInstance, commandPaletteConfig, std::move(commands));
        ::InvalidateRect(hwnd, nullptr, FALSE);
    };
    cfg.onResize = [&renderPipeline, &findBar, &commandPalette](HWND, std::uint32_t w, std::uint32_t h,
                                                                float dpiScale) {
        if (renderPipeline.isAttached()) {
            const auto resized = renderPipeline.resize(w, h, dpiScale);
            if (!resized) {
                debugLogRenderError("RenderPipeline::resize", resized.error());
            }
        }
        findBar.onParentResized(w, dpiScale);
        commandPalette.onParentResized(w, dpiScale);
    };
    cfg.onCommand = [&findBar, &commandPalette](HWND, WPARAM wParam, LPARAM lParam) {
        findBar.handleCommand(wParam, lParam);
        commandPalette.handleCommand(wParam, lParam);
    };
    cfg.onKeyDown = [&dispatcher, &selectionModel, &viewport, &document, &renderPipeline, &findBar,
                     &findReplaceState, &commandPalette](HWND hwnd, UINT vkCode, bool shiftDown,
                                                         bool ctrlDown) {
        handleKeyDownEvent(hwnd, vkCode, shiftDown, ctrlDown, dispatcher, selectionModel, viewport,
                          document, renderPipeline, findBar, findReplaceState, commandPalette);
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
    cfg.onMouseDown = [&selectionModel, &viewport, &document, &renderPipeline, &altCursorAnchor,
                       &rectangularAnchor](HWND hwnd, std::int32_t x, std::int32_t y, bool shiftDown,
                                          bool altDown, int clickCount) {
        const auto hit = renderPipeline.hitTest(x, y);
        if (!hit) {
            return;
        }
        const bool changed = dispatchMouseDown(*hit, shiftDown, altDown, clickCount, selectionModel,
                                              viewport, document, altCursorAnchor, rectangularAnchor);
        if (changed) {
            syncRenderStateAndInvalidate(hwnd, renderPipeline, selectionModel, viewport);
        }
    };
    cfg.onMouseDrag = [&selectionModel, &viewport, &document, &renderPipeline, &altCursorAnchor,
                       &rectangularAnchor](HWND hwnd, std::int32_t x, std::int32_t y) {
        const auto hit = renderPipeline.hitTest(x, y);
        if (!hit) {
            return;
        }
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
    // Phase 4b6d: anchor of the cursor a plain Alt+click most recently
    // added, so a later Alt+Shift+click or Alt+drag can extend that one
    // cursor specifically. Reset to nullopt by any non-Alt click.
    std::optional<neomifes::document::TextPos> altCursorAnchor;
    // Phase 4b8a: anchor of an in-progress Shift+Alt+drag rectangular
    // selection. Kept as a separate optional (not folded into
    // altCursorAnchor) since the two gestures are deliberately independent -
    // see dispatchMouseDown()'s comment.
    std::optional<neomifes::document::TextPos> rectangularAnchor;
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
                       commandPalette);
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
