#include "neomifes/ui/main_window.h"

#include <windows.h>
#include <imm.h>       // ImmGetContext/ImmGetCompositionStringW/ImmSetCandidateWindow (WM_IME_*, WI-06)
#include <oleacc.h>    // LresultFromObject (WM_GETOBJECT, text_surface_no_screen_reader_exposure.md)
#include <shellapi.h>  // DragAcceptFiles/DragQueryFileW/DragFinish (WM_DROPFILES, WI-02)
#include <windowsx.h>  // GET_X_LPARAM/GET_Y_LPARAM (WM_LBUTTONDOWN, Phase 4b2)

#include <utility>
#include <vector>

#include "neomifes/platform/ime_context.h"
#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

// Internal-only message used to defer renderer device creation until after
// the first WM_PAINT completes (ADR-009) - never posted by anything outside
// MainWindow itself.
constexpr UINT kMsgDeferredInit = WM_APP + 1;

// Registration is one-shot per process. RegisterClassExW returns 0 if the class
// is already registered under the same HINSTANCE, so we swallow that case.
bool ensureWindowClass(HINSTANCE hInstance) noexcept {
    static bool sRegistered = false;
    if (sRegistered) {
        return true;
    }
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &MainWindow::wndProcTrampoline;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    // Explicitly no background brush - we own painting entirely (avoids flicker).
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kWindowClassName;

    const ATOM atom = ::RegisterClassExW(&wc);
    if (atom == 0) {
        const DWORD err = ::GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
    }
    sRegistered = true;
    return true;
}

}  // namespace

MainWindow::~MainWindow() {
    if (m_hwnd != nullptr) {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool MainWindow::create(HINSTANCE hInstance, const MainWindowConfig& config) {
    if (!ensureWindowClass(hInstance)) {
        return false;
    }
    m_onFirstPaint   = config.onFirstPaint;
    m_onDeferredInit = config.onDeferredInit;
    m_onResize       = config.onResize;
    m_onKeyDown      = config.onKeyDown;
    m_onSysKeyDown   = config.onSysKeyDown;
    m_onChar         = config.onChar;
    m_onMouseWheel   = config.onMouseWheel;
    m_onMouseDown    = config.onMouseDown;
    m_onMouseDrag    = config.onMouseDrag;
    m_onHScroll      = config.onHScroll;
    m_onCommand      = config.onCommand;
    m_onAppMessage   = config.onAppMessage;
    m_onNotify       = config.onNotify;
    m_onClose        = config.onClose;
    m_onDropFiles    = config.onDropFiles;
    m_onContextMenu  = config.onContextMenu;
    m_onTimer        = config.onTimer;
    m_onFocusLost    = config.onFocusLost;
    m_onImeStartComposition = config.onImeStartComposition;
    m_onImeComposition      = config.onImeComposition;
    m_onImeResult           = config.onImeResult;
    m_onImeEndComposition   = config.onImeEndComposition;
    m_onDestroyed           = config.onDestroyed;
    m_onCopyData            = config.onCopyData;

    // WI-03: WS_HSCROLL only added when a handler is actually configured -
    // must be decided before CreateWindowExW (unlike DragAcceptFiles below,
    // a scrollbar style bit can't be toggled on after the window exists
    // without a separate SetWindowLongPtrW dance this class has no other
    // reason to need).
    //
    // WI-07 step 0 (native_overlay_widgets_invisible.md hypothesis test):
    // WS_CLIPCHILDREN was never set, so every WM_PAINT this window handles
    // paints over the full client rect INCLUDING the area occupied by
    // child HWNDs (FindBar/TabBar/etc.) - without WS_CLIPCHILDREN, Windows
    // does not exclude child-window regions from the parent's paint clip
    // region, so our own D2D full-frame present can legally end up
    // compositing on top of what the child just drew, one message-loop
    // iteration later. Cheapest untested hypothesis for the P0 invisible-
    // widget bug; testing in isolation before touching anything else.
    const DWORD windowStyle =
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | (config.onHScroll ? WS_HSCROLL : 0);

    // CreateWindowExW blocks briefly for WM_CREATE. Startup profiling markers
    // that need to happen "before window creation" must run beforehand.
    m_hwnd = ::CreateWindowExW(
        0,
        kWindowClassName,
        L"NeoMIFES",
        windowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT,
        config.initialWidth, config.initialHeight,
        nullptr, config.menuBar, hInstance, this);

    if (m_hwnd == nullptr) {
        return false;
    }
    m_currentDpi = ::GetDpiForWindow(m_hwnd);
    if (config.onDropFiles) {
        ::DragAcceptFiles(m_hwnd, TRUE);
    }

    // Fire the "window created" hook here - after CreateWindowExW has
    // returned (WM_NCCREATE / WM_CREATE done) but before ShowWindow queues
    // the first WM_PAINT. This preserves the temporal ordering
    // windowCreatedNs <= firstPaintNs that --measure-startup relies on.
    if (config.onWindowCreated) {
        config.onWindowCreated(m_hwnd);
    }

    if (config.showOnCreate) {
        ::ShowWindow(m_hwnd, SW_SHOWNORMAL);
        ::UpdateWindow(m_hwnd);  // Force synchronous WM_PAINT so first-paint timing is deterministic.
    }
    return true;
}

void MainWindow::requestClose() noexcept {
    if (m_hwnd != nullptr) {
        ::PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }
}

void MainWindow::setPaintHandler(std::function<void(HWND)> handler) noexcept {
    m_onPaint = std::move(handler);
}

bool MainWindow::startAutoSaveTimer(UINT intervalMs) noexcept {
    if (m_hwnd == nullptr) {
        return false;
    }
    return ::SetTimer(m_hwnd, kAutoSaveTimerId, intervalMs, nullptr) != 0;
}

LRESULT CALLBACK MainWindow::wndProcTrampoline(HWND hwnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam) noexcept {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self     = static_cast<MainWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr) {
        return self->wndProc(msg, wParam, lParam);
    }
    return ::DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::wndProc(UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_PAINT:
            handlePaint();
            return 0;
        case WM_SIZE:
            handleSize(lParam);
            return 0;
        case WM_DPICHANGED:
            return handleDpiChanged(wParam, lParam);
        case kMsgDeferredInit:
            handleDeferredInit();
            return 0;
        case WM_KEYDOWN:
            handleKeyDown(wParam);
            return 0;
        case WM_SYSKEYDOWN:
            // Unconsumed (including "no handler configured") MUST fall
            // through to DefWindowProcW - this is what keeps Alt+F4/Alt+Tab/
            // the system menu/F10 working (Phase 4b8g).
            if (handleSysKeyDown(wParam)) {
                return 0;
            }
            return ::DefWindowProcW(m_hwnd, msg, wParam, lParam);
        case WM_CHAR:
            handleChar(wParam);
            return 0;
        case WM_MOUSEWHEEL:
            handleMouseWheel(wParam);
            return 0;
        case WM_LBUTTONDOWN:
            handleMouseDown(wParam, lParam);
            return 0;
        case WM_MOUSEMOVE:
            handleMouseMove(lParam);
            return 0;
        case WM_LBUTTONUP:
            handleMouseUp();
            return 0;
        case WM_HSCROLL:
            handleHScroll(wParam);
            return 0;
        case WM_COMMAND:
            handleCommand(wParam, lParam);
            return 0;
        case WM_NOTIFY:
            return handleNotify(wParam, lParam);
        case WM_ERASEBKGND:
            // We paint the full client rect in WM_PAINT; suppress default erase to
            // avoid flicker.
            return 1;
        case WM_CLOSE:
            if (handleClose()) {
                ::DestroyWindow(m_hwnd);
            }
            return 0;
        case WM_DROPFILES:
            handleDropFiles(wParam);
            return 0;
        case WM_CONTEXTMENU:
            handleContextMenu(wParam, lParam);
            return 0;
        case WM_TIMER:
            handleTimer(wParam);
            return 0;
        case WM_KILLFOCUS:
            handleFocusLost();
            return 0;
        case WM_GETOBJECT:
            return handleGetObject(wParam, lParam);
        // WI-06: never forwarded to DefWindowProcW (unlike every case above
        // except WM_ERASEBKGND) - this class owns composition drawing/
        // candidate-window positioning entirely, so there is no OS default
        // behavior worth preserving. WM_IME_STARTCOMPOSITION unconditionally
        // suppresses the OS's own default composition UI regardless of
        // whether onImeStartComposition is configured (see that field's doc
        // comment). Critically, letting WM_IME_COMPOSITION reach
        // DefWindowProcW would make Windows auto-generate one WM_CHAR per
        // GCS_RESULTSTR code unit, defeating the "commit as one Undo step"
        // contract handleImeComposition()'s onImeResult callback exists to
        // provide instead.
        case WM_IME_STARTCOMPOSITION:
            handleImeStartComposition();
            return 0;
        case WM_IME_COMPOSITION:
            handleImeComposition(lParam);
            return 0;
        case WM_IME_ENDCOMPOSITION:
            handleImeEndComposition();
            return 0;
        case WM_DESTROY: {
            // WI-20a: capture before resetting m_hwnd to nullptr - the
            // configured hook (if any) still needs to know WHICH window just
            // went away (SessionManager::onWindowDestroyed() looks it up by
            // this value). Not `const HWND` - HWND is a pointer typedef, so
            // `const HWND` would const-qualify the pointer itself rather
            // than what it points to (misc-misplaced-const); harmless
            // either way since this local is never reassigned, but
            // clang-tidy flags it as a warning-turned-error under this
            // project's build config.
            HWND destroyedHwnd = m_hwnd;
            m_hwnd             = nullptr;
            if (m_onDestroyed) {
                m_onDestroyed(destroyedHwnd);
            } else {
                ::PostQuitMessage(0);
            }
            return 0;
        }
        case WM_COPYDATA: {
            // WI-20b: decode the raw COPYDATASTRUCT here (this class owns
            // Win32-primitive decoding, see this field's own doc comment)
            // and COPY the payload out before calling the hook - lParam's
            // COPYDATASTRUCT and the memory cds->lpData points to are only
            // guaranteed valid for the duration of this SendMessageW call.
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
            if (cds != nullptr && m_onCopyData) {
                const std::wstring_view payload(reinterpret_cast<const wchar_t*>(cds->lpData),
                                                cds->cbData / sizeof(wchar_t));
                m_onCopyData(m_hwnd, cds->dwData, payload);
            }
            return TRUE;
        }
        default:
            // App-defined messages (Phase 7c) - kMsgDeferredInit above is
            // MainWindow's own, everything else >= WM_APP is opaque to this
            // class (see MainWindowConfig::onAppMessage's doc comment).
            // DefWindowProcW is still called either way; a custom WM_APP+
            // message has no default behavior worth skipping.
            if (msg >= WM_APP && m_onAppMessage) {
                m_onAppMessage(m_hwnd, msg, wParam, lParam);
            }
            return ::DefWindowProcW(m_hwnd, msg, wParam, lParam);
    }
}

void MainWindow::handlePaint() noexcept {
    if (m_onPaint) {
        // The renderer owns presentation entirely (D2D/DXGI Present, not
        // GDI); just validate the update region so Windows doesn't keep
        // reposting WM_PAINT for it.
        m_onPaint(m_hwnd);
        ::ValidateRect(m_hwnd, nullptr);
    } else {
        PAINTSTRUCT ps{};
        HDC dc = ::BeginPaint(m_hwnd, &ps);
        if (dc != nullptr) {
            // GDI placeholder fill, active until a renderer attaches via
            // setPaintHandler() (see ADR-009 / onDeferredInit).
            HBRUSH bg = ::CreateSolidBrush(RGB(30, 30, 30));  // dark placeholder
            ::FillRect(dc, &ps.rcPaint, bg);
            ::DeleteObject(bg);
            ::EndPaint(m_hwnd, &ps);
        }
    }

    if (!m_firstPaintFired) {
        m_firstPaintFired = true;
        if (m_onFirstPaint) {
            m_onFirstPaint(m_hwnd);
        }
        if (m_onDeferredInit) {
            // Posted, not called synchronously - runs one message-loop hop
            // later so it can never affect this WM_PAINT's timing (see
            // MainWindowConfig::onDeferredInit doc comment / ADR-009).
            ::PostMessageW(m_hwnd, kMsgDeferredInit, 0, 0);
        }
    }
}

void MainWindow::handleSize(LPARAM lParam) noexcept {
    if (!m_onResize) {
        return;
    }
    const auto width  = static_cast<std::uint32_t>(LOWORD(lParam));
    const auto height = static_cast<std::uint32_t>(HIWORD(lParam));
    m_onResize(m_hwnd, width, height, static_cast<float>(m_currentDpi) / 96.0F);
}

LRESULT MainWindow::handleDpiChanged(WPARAM wParam, LPARAM lParam) noexcept {
    m_currentDpi = HIWORD(wParam);
    // The suggested new window rect triggers a synchronous WM_SIZE via
    // SetWindowPos, so handleSize() runs with m_currentDpi already updated -
    // no separate resize notification is needed here.
    const auto* suggestedRect = reinterpret_cast<const RECT*>(lParam);
    ::SetWindowPos(m_hwnd, nullptr, suggestedRect->left, suggestedRect->top,
                   suggestedRect->right - suggestedRect->left,
                   suggestedRect->bottom - suggestedRect->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    return 0;
}

void MainWindow::handleDeferredInit() noexcept {
    if (m_onDeferredInit) {
        m_onDeferredInit(m_hwnd);
    }
}

void MainWindow::handleKeyDown(WPARAM wParam) noexcept {
    if (!m_onKeyDown) {
        return;
    }
    const bool shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrlDown  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    m_onKeyDown(m_hwnd, static_cast<UINT>(wParam), shiftDown, ctrlDown);
}

bool MainWindow::handleSysKeyDown(WPARAM wParam) noexcept {
    if (!m_onSysKeyDown) {
        return false;
    }
    const bool shiftDown = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    return m_onSysKeyDown(m_hwnd, static_cast<UINT>(wParam), shiftDown);
}

void MainWindow::handleChar(WPARAM wParam) noexcept {
    if (m_onChar) {
        m_onChar(m_hwnd, static_cast<wchar_t>(wParam));
    }
}

void MainWindow::handleMouseWheel(WPARAM wParam) noexcept {
    if (m_onMouseWheel) {
        m_onMouseWheel(m_hwnd, static_cast<short>(HIWORD(wParam)));
    }
}

void MainWindow::handleMouseDown(WPARAM wParam, LPARAM lParam) noexcept {
    // SetCapture unconditionally (even with no onMouseDown/onMouseDrag
    // configured) - a plain click that never turns into a drag just gets an
    // immediate WM_LBUTTONUP, which releases capture harmlessly. This is
    // the standard Win32 pattern for drag operations (Phase 4b3): it keeps
    // WM_MOUSEMOVE/WM_LBUTTONUP delivered to this window even if the cursor
    // leaves the client area mid-drag.
    ::SetCapture(m_hwnd);
    m_isDragging = true;

    // Click-count tracking (Phase 4b4): WM_LBUTTONDBLCLK (needs CS_DBLCLKS,
    // not set on this window class) has no notion of a third click, so
    // every WM_LBUTTONDOWN is run through the pure nextClickState() helper
    // instead, using the same thresholds Windows itself uses for
    // double-clicks.
    const auto x = static_cast<std::int32_t>(GET_X_LPARAM(lParam));
    const auto y = static_cast<std::int32_t>(GET_Y_LPARAM(lParam));
    m_clickState = nextClickState(m_clickState, ClickPoint{.x = x, .y = y},
                                  static_cast<std::uint32_t>(::GetMessageTime()),
                                  ::GetDoubleClickTime(),
                                  ::GetSystemMetrics(SM_CXDOUBLECLK) / 2,
                                  ::GetSystemMetrics(SM_CYDOUBLECLK) / 2);

    if (!m_onMouseDown) {
        return;
    }
    const bool shiftDown = (wParam & MK_SHIFT) != 0;
    // Alt has no MK_* bit in mouse message wParams (unlike Shift/Ctrl), so it
    // must be queried separately (Phase 4b5b).
    const bool altDown = (::GetKeyState(VK_MENU) & 0x8000) != 0;
    m_onMouseDown(m_hwnd, x, y, shiftDown, altDown, m_clickState.count);
}

void MainWindow::handleMouseMove(LPARAM lParam) noexcept {
    if (!m_isDragging || !m_onMouseDrag) {
        return;
    }
    const auto x = static_cast<std::int32_t>(GET_X_LPARAM(lParam));
    const auto y = static_cast<std::int32_t>(GET_Y_LPARAM(lParam));
    m_onMouseDrag(m_hwnd, x, y);
}

void MainWindow::handleMouseUp() noexcept {
    if (!m_isDragging) {
        return;
    }
    m_isDragging = false;
    ::ReleaseCapture();
}

void MainWindow::handleHScroll(WPARAM wParam) noexcept {
    if (m_onHScroll) {
        m_onHScroll(m_hwnd, LOWORD(wParam), HIWORD(wParam));
    }
}

void MainWindow::handleCommand(WPARAM wParam, LPARAM lParam) noexcept {
    if (m_onCommand) {
        m_onCommand(m_hwnd, wParam, lParam);
    }
}

LRESULT MainWindow::handleNotify(WPARAM wParam, LPARAM lParam) noexcept {
    if (m_onNotify) {
        return m_onNotify(m_hwnd, wParam, lParam);
    }
    return 0;
}

bool MainWindow::handleClose() noexcept {
    return !m_onClose || m_onClose(m_hwnd);
}

void MainWindow::handleDropFiles(WPARAM wParam) noexcept {
    auto* const hDrop = reinterpret_cast<HDROP>(wParam);
    if (m_onDropFiles) {
        const UINT count = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> paths;
        paths.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            const UINT len = ::DragQueryFileW(hDrop, i, nullptr, 0);
            std::wstring path(len, L'\0');
            ::DragQueryFileW(hDrop, i, path.data(), len + 1);
            paths.push_back(std::move(path));
        }
        m_onDropFiles(m_hwnd, std::move(paths));
    }
    ::DragFinish(hDrop);
}

void MainWindow::handleContextMenu(WPARAM wParam, LPARAM lParam) noexcept {
    if (!m_onContextMenu) {
        return;
    }
    auto x = static_cast<std::int32_t>(GET_X_LPARAM(lParam));
    auto y = static_cast<std::int32_t>(GET_Y_LPARAM(lParam));
    if (x == -1 && y == -1) {
        // Keyboard-triggered (Shift+F10/VK_APPS) - MSDN: WM_CONTEXTMENU's
        // lParam carries no meaningful position in that case. Falls back to
        // the current cursor position (still somewhere on-screen, unlike
        // leaving x/y at -1) rather than computing a caret-relative
        // position - see MainWindowConfig::onContextMenu's doc comment for
        // why that's out of scope.
        POINT cursor{};
        if (::GetCursorPos(&cursor)) {
            x = cursor.x;
            y = cursor.y;
        } else {
            x = 0;
            y = 0;
        }
    }
    // WI-18a: wParam is WM_CONTEXTMENU's own "handle to the window in which
    // the user right-clicked" (MSDN) - previously discarded entirely, which
    // meant a right-click that bubbled up here from TabBar/StatusBar (an
    // unhandled WM_CONTEXTMENU on a child control reaches its parent by
    // Win32's own default behavior) was indistinguishable from one on this
    // window's own client area, so the SAME edit-menu callback fired for
    // both. See MainWindowConfig::onContextMenu's doc comment for how
    // callers now use this to tell the two apart.
    m_onContextMenu(reinterpret_cast<HWND>(wParam), m_hwnd, x, y);
}

void MainWindow::handleTimer(WPARAM wParam) noexcept {
    if (m_onTimer) {
        m_onTimer(m_hwnd, static_cast<UINT_PTR>(wParam));
    }
}

void MainWindow::handleFocusLost() noexcept {
    if (m_onFocusLost) {
        m_onFocusLost(m_hwnd);
    }
}

// text_surface_no_screen_reader_exposure.md's minimal live-region tier.
// Only OBJID_CLIENT is ours to answer - every other id (OBJID_WINDOW,
// OBJID_TITLEBAR, OBJID_SYSMENU, etc.) must keep reaching DefWindowProcW so
// the window chrome's own default accessible objects keep working
// unmodified; this handler only replaces what the client area itself would
// otherwise expose.
LRESULT MainWindow::handleGetObject(WPARAM wParam, LPARAM lParam) noexcept {
    if (static_cast<long>(lParam) != OBJID_CLIENT) {
        return ::DefWindowProcW(m_hwnd, WM_GETOBJECT, wParam, lParam);
    }
    if (!m_accessible) {
        m_accessible = TextSurfaceAccessible::create(m_hwnd);
        if (!m_accessible) {
            return ::DefWindowProcW(m_hwnd, WM_GETOBJECT, wParam, lParam);
        }
        // Seed with whatever the cursor's current line already is - this
        // query can arrive well after startup (an AT can attach mid-
        // session), and without this the first read-out would be whatever
        // empty string this member starts as, not the real current line.
        m_accessible->setCurrentLineText(m_currentLineText);
    }
    return ::LresultFromObject(IID_IAccessible, wParam, m_accessible.Get());
}

void MainWindow::announceCurrentLineIfChanged(const void* sessionToken, std::uint64_t lineNumber,
                                              std::wstring_view lineText) {
    const bool changed =
        sessionToken != m_lastAnnouncedSessionToken || lineNumber != m_lastAnnouncedLine;
    m_lastAnnouncedSessionToken = sessionToken;
    m_lastAnnouncedLine         = lineNumber;
    // Kept fresh every call regardless of `changed` - see handleGetObject()'s
    // comment on why a late-created m_accessible still needs this.
    m_currentLineText.assign(lineText);
    if (!m_accessible) {
        return;  // No AT has queried WM_GETOBJECT yet - nothing to notify.
    }
    if (!changed) {
        return;
    }
    m_accessible->setCurrentLineText(m_currentLineText);
    ::NotifyWinEvent(EVENT_OBJECT_LIVEREGIONCHANGED, m_hwnd, OBJID_CLIENT, CHILDID_SELF);
}

void MainWindow::handleImeStartComposition() noexcept {
    if (m_onImeStartComposition) {
        m_onImeStartComposition(m_hwnd);
    }
}

namespace {

// Contiguous run of ATTR_TARGET_CONVERTED/ATTR_TARGET_NOTCONVERTED within a
// GCS_COMPATTR byte array (one byte per GCS_COMPSTR character) - the "clause
// currently being edited" build_plan.md's WI-06 DoD calls out for
// highlighting. nullopt if no such run exists (e.g. before the IME has
// picked a conversion target yet).
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> findImeTargetClause(
    const std::vector<BYTE>& attrs) noexcept {
    std::size_t start = 0;
    while (start < attrs.size() && attrs[start] != ATTR_TARGET_CONVERTED &&
           attrs[start] != ATTR_TARGET_NOTCONVERTED) {
        ++start;
    }
    if (start >= attrs.size()) {
        return std::nullopt;
    }
    std::size_t end = start;
    while (end < attrs.size() &&
           (attrs[end] == ATTR_TARGET_CONVERTED || attrs[end] == ATTR_TARGET_NOTCONVERTED)) {
        ++end;
    }
    return std::make_pair(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(end));
}

}  // namespace

void MainWindow::handleImeComposition(LPARAM lParam) noexcept {
    const platform::ImeContext ime(m_hwnd);
    if (!ime) {
        return;
    }
    const auto flags = static_cast<DWORD>(lParam);

    // GCS_RESULTSTR and GCS_COMPSTR are independent bits and can both be set
    // on the same message (e.g. the IME commits one clause while starting a
    // new composition) - each is handled unconditionally on its own bit.
    if ((flags & GCS_RESULTSTR) != 0 && m_onImeResult) {
        const LONG byteLen = ::ImmGetCompositionStringW(ime.get(), GCS_RESULTSTR, nullptr, 0);
        if (byteLen > 0) {
            std::wstring text(static_cast<std::size_t>(byteLen) / sizeof(wchar_t), L'\0');
            ::ImmGetCompositionStringW(ime.get(), GCS_RESULTSTR, text.data(), static_cast<DWORD>(byteLen));
            m_onImeResult(m_hwnd, std::u16string(util::fromWstringView(text)));
        }
    }

    if ((flags & GCS_COMPSTR) != 0 && m_onImeComposition) {
        const LONG strByteLen = ::ImmGetCompositionStringW(ime.get(), GCS_COMPSTR, nullptr, 0);
        std::wstring text;
        if (strByteLen > 0) {
            text.resize(static_cast<std::size_t>(strByteLen) / sizeof(wchar_t));
            ::ImmGetCompositionStringW(ime.get(), GCS_COMPSTR, text.data(), static_cast<DWORD>(strByteLen));
        }

        std::optional<std::pair<std::uint32_t, std::uint32_t>> targetClause;
        const LONG attrByteLen = ::ImmGetCompositionStringW(ime.get(), GCS_COMPATTR, nullptr, 0);
        if (attrByteLen > 0) {
            std::vector<BYTE> attrs(static_cast<std::size_t>(attrByteLen));
            ::ImmGetCompositionStringW(ime.get(), GCS_COMPATTR, attrs.data(), static_cast<DWORD>(attrByteLen));
            targetClause = findImeTargetClause(attrs);
        }

        m_onImeComposition(m_hwnd, std::u16string(util::fromWstringView(text)), targetClause);
    }
}

void MainWindow::handleImeEndComposition() noexcept {
    if (m_onImeEndComposition) {
        m_onImeEndComposition(m_hwnd);
    }
}

void MainWindow::setImeCandidatePosition(POINT clientPx) noexcept {
    const platform::ImeContext ime(m_hwnd);
    if (!ime) {
        return;
    }
    CANDIDATEFORM form{};
    form.dwIndex      = 0;
    form.dwStyle      = CFS_CANDIDATEPOS;
    form.ptCurrentPos = clientPx;
    ::ImmSetCandidateWindow(ime.get(), &form);
}

void MainWindow::setTitle(std::wstring_view title) noexcept {
    if (m_hwnd == nullptr) {
        return;
    }
    ::SetWindowTextW(m_hwnd, std::wstring(title).c_str());
}

}  // namespace neomifes::ui
