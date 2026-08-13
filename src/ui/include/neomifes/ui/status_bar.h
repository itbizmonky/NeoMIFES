#pragma once

// StatusBar - the bottom status strip's STATUSCLASSNAME child control
// (WI-07 step4). Modeled on ui::TabBar (tab_bar.h): Win32-mechanics-only,
// knows nothing about neomifes::app::EditorSession/Document - it deals only
// in a caller-supplied StatusBarParts (each field already formatted text,
// see status_bar_format.h for the app-layer functions that derive them).
//
// Unlike TabBar (WC_TABCONTROL, notifies via WM_NOTIFY, docked at the TOP),
// a native status bar (msctls_statusbar32) notifies via WM_COMMAND
// (SBN_SIMPLEMODECHANGE etc., none of which this class needs - no
// MainWindowConfig::onNotify/onCommand wiring exists here) and docks along
// the BOTTOM edge - the counterpart to TabBar's top dock, both reserving
// space RenderPipeline itself must NOT draw into (see
// render_pipeline.h's m_tabBarHeightDips/m_statusBarHeightDips comments).
//
// v1 scope cut (WI-07 step4): read-only display of all 6 parts. Clicking
// the encoding/line-ending parts to change them is WI-07 step6, not this
// step - see status_bar_format.h's own header comment for why formatting
// and click-handling are deliberately split.

#include <windows.h>

#include <string>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

// One frame's worth of already-formatted status bar text, one string per
// part in left-to-right display order. Kept as a plain struct (not
// individual setText() calls) so StatusBar::setParts() can do a single
// SB_SETTEXTW sweep without the caller worrying about part-index ordering.
struct StatusBarParts {
    std::wstring position;       // "行:桁", e.g. L"12:5"
    std::wstring selectionCount; // selected UTF-16 code-unit count, empty if no selection
    std::wstring encoding;       // e.g. L"UTF-8"
    std::wstring lineEnding;     // e.g. L"CRLF"
    std::wstring overwriteMode;  // L"INS" or L"OVR"
    std::wstring language;       // e.g. L"C++", empty if undetected
};

class StatusBar {
public:
    StatusBar()  = default;
    ~StatusBar() = default;

    StatusBar(const StatusBar&)            = delete;
    StatusBar& operator=(const StatusBar&) = delete;
    StatusBar(StatusBar&&)                 = delete;
    StatusBar& operator=(StatusBar&&)      = delete;

    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance);

    // Rewrites all 6 parts (SB_SETTEXTW). Called from the paint handler
    // every frame something was invalidated, same "no dirty-check guard at
    // this DoD's scale" convention TabBar::setTabs() follows. No-op if
    // create() hasn't succeeded.
    void setParts(const StatusBarParts& parts) noexcept;

    // Docked full-width along the BOTTOM edge - needs both parent
    // dimensions (unlike TabBar::onParentResized(), pinned to y=0 and so
    // only needing width). Also recomputes SB_SETPARTS' part boundaries
    // against the new width.
    void onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight, float dpiScale) noexcept;

    // Fixed DIP height this control always occupies - see
    // render_pipeline.h's setStatusBarHeightDips() for how this space is
    // reserved, same pattern as ui::TabBar::heightDips().
    [[nodiscard]] static constexpr float heightDips() noexcept { return kHeightDips; }

private:
    static constexpr float kHeightDips = 24.0F;  // untuned initial value, matches TabBar's own precedent

    neomifes::platform::WindowHandle m_hwndStatus;
};

}  // namespace neomifes::ui
