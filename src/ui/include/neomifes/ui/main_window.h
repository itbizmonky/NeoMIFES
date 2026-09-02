#pragma once

// MainWindow - Win32 window shell.
// Registers the window class, creates the top-level window, and paints a
// solid background by default (GDI). Phase 3 (Rendering Engine) attaches via
// setPaintHandler()/onDeferredInit/onResize rather than this class linking
// neomifes::render directly - composition happens in src/app/main.cpp, the
// layer that already depends on both ui and render (see ADR-009).
//
// WI-06 (IME): all raw Imm32 API calls (ImmGetContext/ImmGetCompositionStringW/
// ImmSetCandidateWindow/ImmReleaseContext) live inside this class - both the
// WM_IME_* message decoding AND the imperative setImeCandidatePosition()
// the app layer calls. This keeps imm32.lib linked in exactly one place
// (src/ui/CMakeLists.txt) rather than splitting Imm32 usage between here and
// src/app/normal_mode_wiring.cpp (which would need a second link site, since
// it compiles directly into the NeoMIFES.exe target, not neomifes_ui).

#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/ui/click_tracking.h"
#include "neomifes/ui/text_surface_accessible.h"

namespace neomifes::ui {

// Shared with the single-instance check in src/app/main.cpp (FindWindowW), so
// it lives here rather than duplicated as a file-local constant. See the
// identical C-array justification on kSingleInstanceMutexName in main.cpp.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays)
inline constexpr wchar_t kWindowClassName[] = L"NeoMIFES.MainWindow";

// Pure, dependency-free title formatting (WI-07 step8) - header-only so it
// stays unit-testable without a live HWND, same rationale as
// ui::formatTabBaseLabel() (tab_bar.h). `filename` is the active session's
// file name only (nullopt for an untitled/unsaved document, same convention
// formatTabBaseLabel() uses - callers derive it via
// std::filesystem::path::filename() before calling this). `isDirty` appends
// a trailing "*" before the " - NeoMIFES" suffix, mirroring TabBarItem's own
// dirty-marker convention (a trailing glyph, not a prefix).
[[nodiscard]] inline std::wstring formatWindowTitle(const std::optional<std::wstring>& filename,
                                                     bool isDirty) {
    std::wstring title = filename.value_or(L"Untitled");
    if (isDirty) {
        title += L'*';
    }
    title += L" - NeoMIFES";
    return title;
}

struct MainWindowConfig {
    int  initialWidth       = 1200;
    int  initialHeight      = 800;
    bool showOnCreate       = true;
    // Optional: passed straight through as CreateWindowExW's hMenu (WI-07
    // step3). nullptr (the default) means "no menu", Win32's own default for
    // an overlapped window. Once handed here, MainWindow does NOT take
    // ownership beyond the standard Win32 rule that DestroyWindow implicitly
    // destroys a window's still-attached menu - callers must not also call
    // DestroyMenu (see neomifes::app::buildMenuBar()'s own comment, the
    // typical producer of this field).
    HMENU menuBar           = nullptr;
    // Optional: invoked on the UI thread AFTER CreateWindowExW returns but
    // BEFORE ShowWindow/UpdateWindow. Callers use this to sample the
    // "window created" timestamp without racing against the first WM_PAINT
    // (which UpdateWindow dispatches synchronously).
    std::function<void(HWND)> onWindowCreated;
    // Optional: invoked once, on the UI thread, right after the first WM_PAINT
    // completes. Used by --measure-startup to sample the "first-paint" timestamp.
    std::function<void(HWND)> onFirstPaint;
    // Optional: invoked once, posted (not called synchronously) via an
    // internal WM_APP message right after the first WM_PAINT completes - so
    // it never blocks the CreateWindowExW/UpdateWindow path or affects
    // onFirstPaint's timing. Intended for renderer device creation (ADR-009).
    std::function<void(HWND)> onDeferredInit;
    // Optional: invoked from WM_SIZE (including the WM_SIZE that
    // WM_DPICHANGED's SetWindowPos triggers) with the new client-area pixel
    // size and current DPI scale (96 DPI == 1.0f).
    std::function<void(HWND, std::uint32_t width, std::uint32_t height, float dpiScale)> onResize;
    // Optional: invoked from WM_KEYDOWN with the raw virtual-key code and
    // the live Shift/Ctrl modifier state (Phase 4b1). Not fired for
    // character input - see onChar.
    std::function<void(HWND, UINT vkCode, bool shiftDown, bool ctrlDown)> onKeyDown;
    // Optional: invoked from WM_SYSKEYDOWN (Phase 4b8g - Shift+Alt+arrows/
    // Shift+Alt+I for keyboard rectangular-selection extension). Win32 posts
    // WM_SYSKEYDOWN instead of WM_KEYDOWN whenever Alt is held down (or F10),
    // so Alt itself is implied and not passed separately; only the live
    // Shift state is. Returns whether the key was consumed - the caller MUST
    // return false for anything it doesn't specifically recognize, so
    // wndProc falls through to DefWindowProcW and system behavior (Alt+F4,
    // Alt+Tab, the system menu, etc.) keeps working. No handler configured
    // is equivalent to always returning false.
    std::function<bool(HWND, UINT vkCode, bool shiftDown)> onSysKeyDown;
    // Optional: invoked from WM_CHAR with the translated UTF-16 code unit
    // (surrogate halves arrive as two separate calls). Phase 4b1.
    std::function<void(HWND, wchar_t ch)> onChar;
    // Optional: invoked from WM_MOUSEWHEEL with the raw wheel delta
    // (positive = away from the user, a multiple of WHEEL_DELTA). Phase 4b1.
    std::function<void(HWND, short wheelDelta)> onMouseWheel;
    // Optional: invoked from WM_LBUTTONDOWN with the client-area pixel
    // coordinate, the live Shift modifier state (Phase 4b2, read from the
    // message's own wParam per mouse-message convention, not GetKeyState),
    // the live Alt modifier state (Phase 4b5b - unlike Shift/Ctrl, mouse
    // message wParams have no MK_ALT bit, so this one IS read via
    // GetKeyState(VK_MENU) rather than the message itself), and the click
    // count (1/2/3, capped - Phase 4b4, tracked via click_tracking.h's
    // nextClickState() rather than WM_LBUTTONDBLCLK, which has no notion of
    // a third click).
    std::function<void(HWND, std::int32_t x, std::int32_t y, bool shiftDown, bool altDown,
                       int clickCount)>
        onMouseDown;
    // Optional: invoked from WM_MOUSEMOVE with the client-area pixel
    // coordinate, but only while a drag is in progress (between
    // WM_LBUTTONDOWN's SetCapture and the matching WM_LBUTTONUP). No
    // shiftDown parameter - a drag always extends the selection from
    // whatever anchor onMouseDown established (Phase 4b3).
    std::function<void(HWND, std::int32_t x, std::int32_t y)> onMouseDrag;
    // Optional: invoked from WM_HSCROLL (WI-03 - this codebase's first
    // scrollbar of any kind; there is no WM_VSCROLL/custom scrollbar
    // anywhere yet). MainWindow decodes only what a standard-window
    // scrollbar's wParam carries (LOWORD == scroll code, e.g. SB_LINELEFT/
    // SB_LINERIGHT/SB_PAGELEFT/SB_PAGERIGHT/SB_THUMBTRACK/SB_THUMBPOSITION;
    // HIWORD == thumb position, only meaningful for the two SB_THUMB* codes)
    // - same "decode the raw Win32 primitive, hand the app layer typed
    // values" convention onMouseDown/onKeyDown follow. lParam (the scrollbar
    // control's HWND) is not passed through: this hooks the WINDOW's own
    // standard scrollbar (WS_HSCROLL), not a child scrollbar control, so
    // lParam is always NULL and carries no information. Only registered
    // (WS_HSCROLL added to the window style) when this is actually set -
    // see create()'s implementation, same "only opt in when configured"
    // convention onDropFiles's DragAcceptFiles(TRUE) follows.
    std::function<void(HWND, WORD scrollCode, WORD scrollPos)> onHScroll;
    // Optional: invoked from WM_COMMAND (Phase 5b3a). Win32 directs child-
    // control notifications - e.g. EN_CHANGE from the Find bar's WC_EDIT -
    // to the PARENT HWND, never to the child itself, so this is the only
    // place such notifications can be observed. wParam/lParam are passed
    // through unexamined; the caller decodes LOWORD(wParam)/HIWORD(wParam)
    // per the control that sent the notification.
    std::function<void(HWND, WPARAM, LPARAM)> onCommand;
    // Optional: invoked from wndProc's default case for any message >=
    // WM_APP that this class doesn't itself interpret (Phase 7c -
    // SyntaxWorker's PostMessageW-based parse-completion signal is the
    // first user of this). wParam/lParam are passed through unexamined,
    // same "caller decodes" contract as onCommand above. MainWindow
    // deliberately never learns what any specific WM_APP+N value means -
    // this keeps neomifes::ui independent of neomifes::render/syntax::,
    // matching CLAUDE.md's layer rule (Rendering Engine sits BELOW UI
    // Shell, so ui:: must not depend on it). DefWindowProcW is still called
    // afterward regardless - a custom WM_APP+ message has no default
    // window behavior to preserve, unlike WM_SYSKEYDOWN's conditional
    // fall-through above.
    std::function<void(HWND, UINT msg, WPARAM, LPARAM)> onAppMessage;
    // Optional: invoked from WM_NOTIFY (Phase 7g - OutlinePane's WC_TREEVIEW
    // is this codebase's first control that notifies via WM_NOTIFY rather
    // than WM_COMMAND; every earlier child control - WC_EDIT/WC_LISTBOX -
    // notified via onCommand above instead). wParam/lParam are passed
    // through unexamined, same "caller decodes" contract as onCommand/
    // onAppMessage - MainWindow never learns what NMHDR::code means, keeping
    // neomifes::ui independent of any specific control's notification
    // payload types. The return value becomes wndProc's WM_NOTIFY result;
    // 0 (no handler configured) is a safe default for TreeView, which does
    // not require a specific non-zero reply.
    std::function<LRESULT(HWND, WPARAM, LPARAM)> onNotify;
    // Optional: invoked from WM_CLOSE (WI-02). Returns whether the window
    // may actually close (true = proceed to DestroyWindow, false = leave
    // the window open) - used for the unsaved-changes confirmation. NOTE
    // THE INVERTED DEFAULT vs onSysKeyDown above: no handler configured
    // here is equivalent to always returning TRUE (closable), the opposite
    // of onSysKeyDown's "no handler = false/unconsumed" - onSysKeyDown's
    // false lets DefWindowProcW preserve system key behavior when nothing
    // claims a key, whereas WM_CLOSE has no such fallback to defer to; the
    // pre-WI-02 behavior (unconditional DestroyWindow) is the correct
    // no-handler default to preserve.
    std::function<bool(HWND)> onClose;
    // Optional: invoked from WM_DROPFILES (WI-02) with every dropped file's
    // full path, in drop order. MainWindow itself decodes the raw HDROP via
    // DragQueryFileW/DragFinish before calling out (same "decode the
    // primitive Win32 payload, hand the app layer clean typed values"
    // convention onMouseDown/onKeyDown already follow - GET_X_LPARAM/
    // GetKeyState respectively - unlike onCommand/onAppMessage/onNotify's
    // deliberate opacity, which exists specifically to avoid neomifes::ui
    // depending on render::/syntax:: payload types; HDROP decoding needs no
    // such cross-layer knowledge). Only registered (DragAcceptFiles(TRUE))
    // when this is actually set - see create()'s implementation.
    std::function<void(HWND, std::vector<std::wstring>)> onDropFiles;
    // Optional: invoked from WM_CONTEXTMENU (WI-07 step9) with the SCREEN
    // coordinate to show a popup menu at (TrackPopupMenu's own coordinate
    // space - unlike onMouseDown/onMouseDrag's client-area pixels, since the
    // caller hands this straight to TrackPopupMenu without an intervening
    // ClientToScreen() call). WM_CONTEXTMENU's own lParam is already in
    // screen coordinates for a mouse-triggered right-click; for a keyboard-
    // triggered one (Shift+F10/VK_APPS, lParam == (-1,-1)) MainWindow
    // substitutes the current cursor position via GetCursorPos() instead of
    // computing a caret-relative position - out of scope for this step (see
    // command_dispatch.h's own comment on what WI-07 step9 covers). Always
    // returns 0 from wndProc (no DefWindowProcW fallback) - same "this class
    // owns the behavior entirely" reasoning as onImeStartComposition below,
    // except unconditional even when unconfigured (no default Win32 popup
    // exists for a Direct2D-painted client area with no WC_EDIT focus to
    // fall back to).
    //
    // WI-18a: the first HWND parameter is WM_CONTEXTMENU's own wParam - the
    // window the user actually right-clicked, which is this window itself
    // for a click on its own client area, but a DIFFERENT hwnd (e.g.
    // TabBar's/StatusBar's) when an unhandled WM_CONTEXTMENU on a child
    // control bubbles up here by Win32's own default behavior. Previously
    // this callback only received the main window's own hwnd (the second
    // parameter, still passed for convenience/consistency with the other
    // hooks here) and had no way to tell a genuine main-window right-click
    // apart from a bubbled one - every right-click anywhere in the window
    // produced the identical popup menu regardless of where it actually
    // landed. Callers should compare the first parameter against whichever
    // child HWNDs they care about distinguishing (see
    // normal_mode_wiring.cpp's cfg.onContextMenu).
    std::function<void(HWND source, HWND hwnd, std::int32_t xScreen, std::int32_t yScreen)> onContextMenu;
    // Optional: invoked from WM_TIMER (WI-11 - this codebase's first Win32
    // timer of any kind) with the raw timer id (WM_TIMER's wParam - a
    // trivial decode, same "hand the app layer typed values" convention
    // every other hook here follows). Only ever fired for the timer
    // startAutoSaveTimer() below starts (kAutoSaveTimerId) - no filtering
    // is done here, the caller compares timerId itself if it ever needs to
    // distinguish multiple timers in the future.
    std::function<void(HWND, UINT_PTR timerId)> onTimer;
    // Optional: invoked from WM_KILLFOCUS (WI-11 - autosave-on-focus-loss).
    // No payload - signals only that this window just lost keyboard focus.
    std::function<void(HWND)> onFocusLost;
    // Optional: invoked from WM_IME_STARTCOMPOSITION (WI-06). No payload -
    // this only signals "a composition session began". MainWindow always
    // returns 0 for this message regardless of whether a handler is
    // configured, unconditionally suppressing the OS's own default IME
    // composition UI (unlike onSysKeyDown's conditional fall-through above -
    // there is no DefWindowProcW behavior worth preserving here, since this
    // window draws its own composition text - see RenderPipeline's
    // ImeComposition).
    std::function<void(HWND)> onImeStartComposition;
    // Optional: invoked from WM_IME_COMPOSITION whenever GCS_COMPSTR is
    // present (WI-06) - i.e. the unconfirmed composition string changed.
    // MainWindow decodes the raw ImmGetCompositionStringW payload itself
    // (same "decode the primitive Win32 payload, hand the app layer clean
    // typed values" convention onMouseDown/onDropFiles already follow) and
    // hands up the current composition text plus, if GCS_COMPATTR marks a
    // contiguous ATTR_TARGET_CONVERTED/ATTR_TARGET_NOTCONVERTED run, the
    // [start,end) code-unit range within that text identifying the
    // currently-being-edited clause (nullopt if no such run exists).
    std::function<void(HWND, std::u16string text,
                       std::optional<std::pair<std::uint32_t, std::uint32_t>> targetClauseRange)>
        onImeComposition;
    // Optional: invoked from WM_IME_COMPOSITION whenever GCS_RESULTSTR is
    // present (WI-06) - i.e. the IME just committed text. This is the ONLY
    // path committed IME text takes: MainWindow never forwards
    // WM_IME_COMPOSITION to DefWindowProcW (see wndProc()), so the OS's
    // usual auto-generated WM_CHAR-per-code-unit sequence for GCS_RESULTSTR
    // never happens - the caller must insert `resultText` itself, as a
    // single atomic edit (this is what makes "one Undo step" achievable).
    std::function<void(HWND, std::u16string resultText)> onImeResult;
    // Optional: invoked from WM_IME_ENDCOMPOSITION (WI-06). No payload -
    // signals the composition session ended (committed or cancelled); the
    // caller should clear any composition-overlay render state.
    std::function<void(HWND)> onImeEndComposition;
    // Optional: invoked from WM_DESTROY (WI-20a), AFTER m_hwnd has already
    // been reset to nullptr, with the HWND that was just destroyed. When
    // configured, this hook takes FULL responsibility for deciding whether/
    // when to call ::PostQuitMessage() - MainWindow itself does NOT call it
    // in that case (multi-window support needs "only quit once every window
    // is gone", which MainWindow itself has no way to know - only whoever
    // owns the collection of windows, SessionManager, does). No handler
    // configured preserves the pre-WI-20a default: MainWindow calls
    // ::PostQuitMessage(0) itself, unconditionally, exactly as before - so
    // measurement-mode launches (wireMeasureStartupOrMemoryMode()/
    // wireMeasureFrameMode(), main.cpp) and any other single-MainWindow
    // caller are completely unaffected by this change.
    std::function<void(HWND)> onDestroyed;
    // Optional: invoked from WM_COPYDATA (WI-20b) - a second NeoMIFES.exe
    // launch, detecting this one is already running, forwards its own
    // --open path (if any) here via SendMessageW before exiting (see
    // app::claimSingleInstance()). `dwData` is COPYDATASTRUCT::dwData
    // (app::kCopyDataOpenPathId identifies this payload shape - the caller
    // should ignore any other value, same "unrecognized input is a silent
    // no-op" convention this codebase already applies elsewhere); `payload`
    // is the UTF-16 path string (empty means "open a new blank window").
    // MainWindow decodes the raw COPYDATASTRUCT itself (same "decode the
    // primitive Win32 payload, hand the app layer clean typed values"
    // convention onMouseDown/onDropFiles already follow) and COPIES the
    // string out of cds->lpData before calling this - Win32 only
    // guarantees that memory is valid for the duration of the SendMessageW
    // call, so the callback must not retain a pointer/view into it.
    std::function<void(HWND, ULONG_PTR dwData, std::wstring_view payload)> onCopyData;
};

class MainWindow {
public:
    MainWindow()  = default;
    ~MainWindow();

    MainWindow(const MainWindow&)            = delete;
    MainWindow& operator=(const MainWindow&) = delete;
    MainWindow(MainWindow&&)                 = delete;
    MainWindow& operator=(MainWindow&&)      = delete;

    // Registers the window class (idempotent) and creates the HWND.
    // Returns false on failure; call ::GetLastError for details.
    [[nodiscard]] bool create(HINSTANCE hInstance, const MainWindowConfig& config);

    [[nodiscard]] HWND hwnd() const noexcept { return m_hwnd; }

    // Convenience: schedules WM_CLOSE so the message loop exits gracefully.
    void requestClose() noexcept;

    // Swaps the WM_PAINT handler at runtime - e.g. once RenderPipeline::attach()
    // succeeds and can take over from the GDI placeholder. Safe to call from
    // any hook (all run on the UI thread). An empty handler restores the GDI
    // fallback fill.
    void setPaintHandler(std::function<void(HWND)> handler) noexcept;

    // Win32 WNDPROC entry point. Public because it is registered as the
    // window class's lpfnWndProc from a free helper in main_window.cpp; do
    // not call it from application code.
    static LRESULT CALLBACK wndProcTrampoline(HWND, UINT, WPARAM, LPARAM) noexcept;

    // Imperative call (WI-06), not a hook - matches ui::TabBar::setTabs()'s
    // "app layer calls imperatively into a ui:: class" precedent, same layer
    // as that call. Positions the IME candidate window at `clientPx`
    // (client-area DEVICE PIXELS, not DIPs - the caller, which already knows
    // the caret's DIP position via RenderPipeline, must multiply by DPI
    // scale itself). All raw Imm32 calls live inside this class (see
    // ime_context.h) so imm32.lib needs linking nowhere else - see this
    // class's header comment for why that single-owner design was chosen.
    // No-op if no composition is in progress (ImmSetCandidateWindow is
    // harmless to call outside one, but callers should prefer not to).
    void setImeCandidatePosition(POINT clientPx) noexcept;

    // Imperative call (WI-07 step8), same "app layer calls imperatively into
    // a ui:: class" pattern as setImeCandidatePosition()/ui::TabBar::setTabs()
    // above. Sets the OS title bar text via ::SetWindowTextW - no diffing
    // against the previous title (same "rebuild every frame, no dirty-check
    // guard" convention normal_mode_wiring.cpp's paint handler already uses
    // for tabBar.setTabs()/statusBar.setParts(), see build_plan.md's WI-07
    // notes). No-op if the window hasn't been created yet.
    void setTitle(std::wstring_view title) noexcept;

    // WI-11: fixed id for the one periodic timer this class ever starts
    // (autosave) - exposed as a public constant (same "other layers
    // compare against it" role as kMsgSyntaxTokensReady, render_pipeline.h)
    // so MainWindowConfig::onTimer's caller can recognize it, even though
    // today it's the only timer that could possibly fire.
    static constexpr UINT_PTR kAutoSaveTimerId = 1;

    // Imperative call (WI-11), same "app layer calls imperatively into a
    // ui:: class" pattern as setTitle()/setImeCandidatePosition() above.
    // Starts a recurring WM_TIMER at `intervalMs` via ::SetTimer(m_hwnd,
    // kAutoSaveTimerId, intervalMs, nullptr) - passing a null TIMERPROC
    // routes WM_TIMER through the normal message queue to this window's own
    // wndProc (Win32's documented behavior), matching every other message
    // this class already handles via a callback hook rather than a
    // per-message procedure. No corresponding stop method: DestroyWindow
    // implicitly kills every timer owned by the window (Win32's own
    // contract), so no explicit cleanup is needed for this class's
    // lifetime. Returns false (no-op) if the window hasn't been created
    // yet or ::SetTimer itself fails; callers should simply not call this
    // at all when autosave is configured disabled (interval == 0, see
    // core::Settings::autoSaveIntervalSeconds's own sentinel convention) -
    // this is not itself responsible for interpreting that sentinel.
    bool startAutoSaveTimer(UINT intervalMs) noexcept;

    // Imperative call (text_surface_no_screen_reader_exposure.md's minimal
    // live-region tier, 2026-09-02), same "app layer calls imperatively
    // into a ui:: class" pattern as setTitle()/setImeCandidatePosition()
    // above. Meant to be called once per WM_PAINT (handlePaintEvent()
    // already computes the active session's cursor line every frame for
    // buildStatusBarParts()) - `sessionToken` is an opaque identity key for
    // the active EditorSession (its address is fine; MainWindow, the ui
    // layer, must not depend on neomifes::core/document types per ADR-009,
    // so it never sees an EditorSession& directly), mirroring
    // csvGridPanePendingSessionToken's existing "raw EditorSession* as an
    // opaque token" pattern in normal_mode_wiring.cpp. Only touches the
    // accessible object and fires EVENT_OBJECT_LIVEREGIONCHANGED when
    // either `sessionToken` or `lineNumber` differs from the previous call
    // - calling this unconditionally every frame would otherwise announce
    // the same unchanged line dozens of times a second.
    void announceCurrentLineIfChanged(const void* sessionToken, std::uint64_t lineNumber,
                                      std::wstring_view lineText);

private:
    LRESULT wndProc(UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    void handlePaint() noexcept;
    void handleSize(LPARAM lParam) noexcept;
    LRESULT handleDpiChanged(WPARAM wParam, LPARAM lParam) noexcept;
    void handleDeferredInit() noexcept;
    void handleKeyDown(WPARAM wParam) noexcept;
    [[nodiscard]] bool handleSysKeyDown(WPARAM wParam) noexcept;
    void handleChar(WPARAM wParam) noexcept;
    void handleMouseWheel(WPARAM wParam) noexcept;
    void handleMouseDown(WPARAM wParam, LPARAM lParam) noexcept;
    void handleMouseMove(LPARAM lParam) noexcept;
    void handleMouseUp() noexcept;
    void handleHScroll(WPARAM wParam) noexcept;
    void handleCommand(WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT handleNotify(WPARAM wParam, LPARAM lParam) noexcept;
    [[nodiscard]] bool handleClose() noexcept;
    void handleDropFiles(WPARAM wParam) noexcept;
    // Decodes WM_CONTEXTMENU's wParam (the originating HWND, WI-18a) and
    // lParam (screen coordinates, or (-1,-1) for a keyboard-triggered
    // invocation - see MainWindowConfig::onContextMenu's doc comment for the
    // GetCursorPos() fallback in that case).
    void handleContextMenu(WPARAM wParam, LPARAM lParam) noexcept;
    // Decodes WM_TIMER's wParam (the timer id) - WI-11.
    void handleTimer(WPARAM wParam) noexcept;
    // WM_KILLFOCUS carries no payload to decode - WI-11.
    void handleFocusLost() noexcept;
    void handleImeStartComposition() noexcept;
    // Extracts GCS_COMPSTR/GCS_COMPATTR (-> m_onImeComposition) and/or
    // GCS_RESULTSTR (-> m_onImeResult) from the live composition, per
    // whichever bits WM_IME_COMPOSITION's lParam actually carries this call
    // (see .cpp for why both can't be assumed to always coexist).
    void handleImeComposition(LPARAM lParam) noexcept;
    void handleImeEndComposition() noexcept;
    // WM_GETOBJECT (text_surface_no_screen_reader_exposure.md) - lazily
    // creates m_accessible on the first OBJID_CLIENT query (no assistive
    // technology running this session means this never runs at all) and
    // seeds it with whatever announceCurrentLineIfChanged() has already
    // cached, so the very first query returns real content rather than an
    // empty name.
    LRESULT handleGetObject(WPARAM wParam, LPARAM lParam) noexcept;

    HWND                       m_hwnd            = nullptr;
    std::function<void(HWND)>  m_onFirstPaint;
    std::function<void(HWND)>  m_onDeferredInit;
    std::function<void(HWND, std::uint32_t, std::uint32_t, float)> m_onResize;
    std::function<void(HWND)>  m_onPaint;          // set via setPaintHandler(); empty == GDI fallback
    std::function<void(HWND, UINT, bool, bool)> m_onKeyDown;
    std::function<bool(HWND, UINT, bool)>       m_onSysKeyDown;
    std::function<void(HWND, wchar_t)>          m_onChar;
    std::function<void(HWND, short)>            m_onMouseWheel;
    std::function<void(HWND, std::int32_t, std::int32_t, bool, bool, int)> m_onMouseDown;
    std::function<void(HWND, std::int32_t, std::int32_t)>            m_onMouseDrag;
    std::function<void(HWND, WORD, WORD)>                             m_onHScroll;
    std::function<void(HWND, WPARAM, LPARAM)>                         m_onCommand;
    std::function<void(HWND, UINT, WPARAM, LPARAM)>                   m_onAppMessage;
    std::function<LRESULT(HWND, WPARAM, LPARAM)>                      m_onNotify;
    std::function<bool(HWND)>                                         m_onClose;
    std::function<void(HWND, std::vector<std::wstring>)>              m_onDropFiles;
    std::function<void(HWND, HWND, std::int32_t, std::int32_t)>       m_onContextMenu;
    std::function<void(HWND, UINT_PTR)>                               m_onTimer;
    std::function<void(HWND)>                                         m_onFocusLost;
    std::function<void(HWND)>                                         m_onImeStartComposition;
    std::function<void(HWND, std::u16string, std::optional<std::pair<std::uint32_t, std::uint32_t>>)>
        m_onImeComposition;
    std::function<void(HWND, std::u16string)>                         m_onImeResult;
    std::function<void(HWND)>                                         m_onImeEndComposition;
    std::function<void(HWND)>                                         m_onDestroyed;
    std::function<void(HWND, ULONG_PTR, std::wstring_view)>           m_onCopyData;
    bool                       m_firstPaintFired = false;
    bool                       m_isDragging      = false;
    UINT                       m_currentDpi      = 96;
    ClickTrackerState          m_clickState;

    // text_surface_no_screen_reader_exposure.md's minimal live-region tier.
    // m_accessible starts null (lazy, see handleGetObject()'s comment).
    // m_currentLineText is kept fresh on EVERY announceCurrentLineIfChanged()
    // call regardless of the change check below, precisely so a late-created
    // m_accessible can be seeded correctly. m_lastAnnouncedSessionToken
    // starts at a value no real EditorSession address can equal only by
    // convention (nullptr is also a legal token in principle, but
    // Workspace always has an active session by the time WM_PAINT can
    // fire, so the very first call's token is never actually nullptr in
    // practice) - the point is only that the first call is always treated
    // as "changed".
    Microsoft::WRL::ComPtr<TextSurfaceAccessible> m_accessible;
    std::wstring                                  m_currentLineText;
    const void*                                   m_lastAnnouncedSessionToken = nullptr;
    std::uint64_t                                 m_lastAnnouncedLine         = UINT64_MAX;
};

}  // namespace neomifes::ui
