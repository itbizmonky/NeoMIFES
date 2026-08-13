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

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "neomifes/ui/click_tracking.h"

namespace neomifes::ui {

// Shared with the single-instance check in src/app/main.cpp (FindWindowW), so
// it lives here rather than duplicated as a file-local constant. See the
// identical C-array justification on kSingleInstanceMutexName in main.cpp.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays, hicpp-avoid-c-arrays, modernize-avoid-c-arrays)
inline constexpr wchar_t kWindowClassName[] = L"NeoMIFES.MainWindow";

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
    void handleImeStartComposition() noexcept;
    // Extracts GCS_COMPSTR/GCS_COMPATTR (-> m_onImeComposition) and/or
    // GCS_RESULTSTR (-> m_onImeResult) from the live composition, per
    // whichever bits WM_IME_COMPOSITION's lParam actually carries this call
    // (see .cpp for why both can't be assumed to always coexist).
    void handleImeComposition(LPARAM lParam) noexcept;
    void handleImeEndComposition() noexcept;

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
    std::function<void(HWND)>                                         m_onImeStartComposition;
    std::function<void(HWND, std::u16string, std::optional<std::pair<std::uint32_t, std::uint32_t>>)>
        m_onImeComposition;
    std::function<void(HWND, std::u16string)>                         m_onImeResult;
    std::function<void(HWND)>                                         m_onImeEndComposition;
    bool                       m_firstPaintFired = false;
    bool                       m_isDragging      = false;
    UINT                       m_currentDpi      = 96;
    ClickTrackerState          m_clickState;
};

}  // namespace neomifes::ui
