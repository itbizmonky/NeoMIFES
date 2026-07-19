#pragma once

// MainWindow - Win32 window shell.
// Registers the window class, creates the top-level window, and paints a
// solid background by default (GDI). Phase 3 (Rendering Engine) attaches via
// setPaintHandler()/onDeferredInit/onResize rather than this class linking
// neomifes::render directly - composition happens in src/app/main.cpp, the
// layer that already depends on both ui and render (see ADR-009).

#include <windows.h>

#include <cstdint>
#include <functional>

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
    // Optional: invoked from WM_COMMAND (Phase 5b3a). Win32 directs child-
    // control notifications - e.g. EN_CHANGE from the Find bar's WC_EDIT -
    // to the PARENT HWND, never to the child itself, so this is the only
    // place such notifications can be observed. wParam/lParam are passed
    // through unexamined; the caller decodes LOWORD(wParam)/HIWORD(wParam)
    // per the control that sent the notification.
    std::function<void(HWND, WPARAM, LPARAM)> onCommand;
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

private:
    LRESULT wndProc(UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    void handlePaint() noexcept;
    void handleSize(LPARAM lParam) noexcept;
    LRESULT handleDpiChanged(WPARAM wParam, LPARAM lParam) noexcept;
    void handleDeferredInit() noexcept;
    void handleKeyDown(WPARAM wParam) noexcept;
    void handleChar(WPARAM wParam) noexcept;
    void handleMouseWheel(WPARAM wParam) noexcept;
    void handleMouseDown(WPARAM wParam, LPARAM lParam) noexcept;
    void handleMouseMove(LPARAM lParam) noexcept;
    void handleMouseUp() noexcept;
    void handleCommand(WPARAM wParam, LPARAM lParam) noexcept;

    HWND                       m_hwnd            = nullptr;
    std::function<void(HWND)>  m_onFirstPaint;
    std::function<void(HWND)>  m_onDeferredInit;
    std::function<void(HWND, std::uint32_t, std::uint32_t, float)> m_onResize;
    std::function<void(HWND)>  m_onPaint;          // set via setPaintHandler(); empty == GDI fallback
    std::function<void(HWND, UINT, bool, bool)> m_onKeyDown;
    std::function<void(HWND, wchar_t)>          m_onChar;
    std::function<void(HWND, short)>            m_onMouseWheel;
    std::function<void(HWND, std::int32_t, std::int32_t, bool, bool, int)> m_onMouseDown;
    std::function<void(HWND, std::int32_t, std::int32_t)>            m_onMouseDrag;
    std::function<void(HWND, WPARAM, LPARAM)>                         m_onCommand;
    bool                       m_firstPaintFired = false;
    bool                       m_isDragging      = false;
    UINT                       m_currentDpi      = 96;
    ClickTrackerState          m_clickState;
};

}  // namespace neomifes::ui
