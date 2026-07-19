#pragma once

// GotoLineBar - Ctrl+G's single WC_EDIT overlay (Phase 4b8b). Simpler than
// FindBar: no debounce timer, no listbox, no toggle buttons - a single
// control that submits its raw text on Enter and lets the caller
// (src/app/main.cpp) parse it via goto_line_parser.h and move the cursor.
// Win32-mechanics-only, same "knows nothing about core::/document::"
// separation as FindBar/CommandPalette.

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

struct GotoLineBarConfig {
    // Enter - the edit's current raw text, unparsed (same "push the current
    // text" shape as FindBarConfig::onQueryChanged - the caller parses via
    // parseGotoLineInput()). Not fired while an IME composition is in
    // progress.
    std::function<void(std::u16string_view input)> onSubmit;
    // Escape. The caller is responsible for restoring focus to the document
    // editing area (GotoLineBar does not know where that is), same contract
    // as FindBarConfig::onClosed.
    std::function<void()> onClosed;
};

class GotoLineBar {
public:
    GotoLineBar()  = default;
    ~GotoLineBar() = default;

    GotoLineBar(const GotoLineBar&)            = delete;
    GotoLineBar& operator=(const GotoLineBar&) = delete;
    GotoLineBar(GotoLineBar&&)                 = delete;
    GotoLineBar& operator=(GotoLineBar&&)      = delete;

    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const GotoLineBarConfig& config);

    // Clears the edit, shows it, focuses it. Re-invoking while already open
    // (Ctrl+G pressed twice) resets to this same state, matching
    // FindBar::show()'s "always land somewhere well-defined" convention.
    void show() noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    void onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR subclassId, DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    [[nodiscard]] bool handleSubclassKeyDown(UINT vkCode) noexcept;
    void ensureFont(float dpiScale) noexcept;
    [[nodiscard]] std::u16string readEditText() const;

    neomifes::platform::WindowHandle    m_hwndEdit;
    neomifes::platform::GdiObjectHandle m_font;
    // Same "CJK IME一級市民" guard as FindBar/CommandPalette - Enter/Escape
    // must not be intercepted mid-composition.
    bool m_composing = false;
    GotoLineBarConfig m_config;
};

}  // namespace neomifes::ui
