#include "neomifes/ui/main_window.h"

#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM/GET_Y_LPARAM (WM_LBUTTONDOWN, Phase 4b2)

#include <utility>

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
    m_onCommand      = config.onCommand;
    m_onAppMessage   = config.onAppMessage;

    // CreateWindowExW blocks briefly for WM_CREATE. Startup profiling markers
    // that need to happen "before window creation" must run beforehand.
    m_hwnd = ::CreateWindowExW(
        0,
        kWindowClassName,
        L"NeoMIFES",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        config.initialWidth, config.initialHeight,
        nullptr, nullptr, hInstance, this);

    if (m_hwnd == nullptr) {
        return false;
    }
    m_currentDpi = ::GetDpiForWindow(m_hwnd);

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
        case WM_COMMAND:
            handleCommand(wParam, lParam);
            return 0;
        case WM_ERASEBKGND:
            // We paint the full client rect in WM_PAINT; suppress default erase to
            // avoid flicker.
            return 1;
        case WM_CLOSE:
            ::DestroyWindow(m_hwnd);
            return 0;
        case WM_DESTROY:
            m_hwnd = nullptr;
            ::PostQuitMessage(0);
            return 0;
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

void MainWindow::handleCommand(WPARAM wParam, LPARAM lParam) noexcept {
    if (m_onCommand) {
        m_onCommand(m_hwnd, wParam, lParam);
    }
}

}  // namespace neomifes::ui
