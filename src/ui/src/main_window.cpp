#include "neomifes/ui/main_window.h"

#include <windows.h>

namespace neomifes::ui {

namespace {

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
    m_onFirstPaint = config.onFirstPaint;

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
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = ::BeginPaint(m_hwnd, &ps);
            if (dc != nullptr) {
                // Phase 1: plain background fill via GDI. Phase 3 replaces this with
                // a Direct2D device context bound to a DXGI swap chain (see
                // detailed_design.md sec.4).
                HBRUSH bg = ::CreateSolidBrush(RGB(30, 30, 30));  // dark placeholder
                ::FillRect(dc, &ps.rcPaint, bg);
                ::DeleteObject(bg);
                ::EndPaint(m_hwnd, &ps);
            }
            if (!m_firstPaintFired) {
                m_firstPaintFired = true;
                if (m_onFirstPaint) {
                    m_onFirstPaint(m_hwnd);
                }
            }
            return 0;
        }
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
            return ::DefWindowProcW(m_hwnd, msg, wParam, lParam);
    }
}

}  // namespace neomifes::ui
