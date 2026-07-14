#pragma once

// MainWindow - Phase 1 skeleton.
// Only registers the window class, creates the top-level window, and paints a
// solid background. Direct2D / DirectWrite integration lands in Phase 3.

#include <windows.h>

#include <functional>

namespace neomifes::ui {

struct MainWindowConfig {
    int  initialWidth       = 1200;
    int  initialHeight      = 800;
    bool showOnCreate       = true;
    // Optional: invoked once, on the UI thread, right after the first WM_PAINT
    // completes. Used by --measure-startup to sample the "first-paint" timestamp.
    std::function<void(HWND)> onFirstPaint;
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

    // Win32 WNDPROC entry point. Public because it is registered as the
    // window class's lpfnWndProc from a free helper in main_window.cpp; do
    // not call it from application code.
    static LRESULT CALLBACK wndProcTrampoline(HWND, UINT, WPARAM, LPARAM) noexcept;

private:
    LRESULT wndProc(UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    HWND                       m_hwnd            = nullptr;
    std::function<void(HWND)>  m_onFirstPaint;
    bool                       m_firstPaintFired = false;
};

}  // namespace neomifes::ui
