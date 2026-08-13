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
// v1 scope cut (WI-07 step4): read-only display of all 6 parts. WI-07
// step6 added onPartClicked below so the caller (app layer) can present a
// choice UI for the encoding/line-ending parts - see this header's
// handleNotify()/StatusBarConfig comments.

#include <windows.h>

#include <functional>
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

// WI-07 step6: `partIndex` matches StatusBarParts' field order (0=position,
// 1=selectionCount, 2=encoding, 3=lineEnding, 4=overwriteMode,
// 5=language) - the same index setParts()/SB_SETTEXTW already use, so the
// caller doesn't need a second numbering scheme. `screenPt` is already
// converted to screen coordinates (handleNotify() owns the status bar's
// HWND and does the ClientToScreen() conversion) - ready to pass straight
// into TrackPopupMenu(), which the app layer needs anyway for WI-07 step9's
// right-click context menu.
struct StatusBarConfig {
    std::function<void(std::size_t partIndex, POINT screenPt)> onPartClicked;
};

class StatusBar {
public:
    StatusBar()  = default;
    ~StatusBar() = default;

    StatusBar(const StatusBar&)            = delete;
    StatusBar& operator=(const StatusBar&) = delete;
    StatusBar(StatusBar&&)                 = delete;
    StatusBar& operator=(StatusBar&&)      = delete;

    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const StatusBarConfig& config);

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

    // WI-07 step6: routes NM_CLICK (WM_NOTIFY, msctls_statusbar32 has
    // supported this since Common Controls 4.71 - verified against the
    // real control, not assumed, per this WI's own plan) into
    // m_config.onPartClicked. No-op (including for any other WM_NOTIFY
    // code, or a notification from a different control) if this isn't a
    // click on THIS status bar. Same "caller forwards cfg.onNotify's raw
    // WPARAM/LPARAM, this decides whether it's for us" shape as
    // TabBar::handleNotify()/OutlinePane::handleNotify().
    void handleNotify(WPARAM wParam, LPARAM lParam) noexcept;

    // Fixed DIP height this control always occupies - see
    // render_pipeline.h's setStatusBarHeightDips() for how this space is
    // reserved, same pattern as ui::TabBar::heightDips().
    [[nodiscard]] static constexpr float heightDips() noexcept { return kHeightDips; }

private:
    static constexpr float kHeightDips = 24.0F;  // untuned initial value, matches TabBar's own precedent

    neomifes::platform::WindowHandle m_hwndStatus;
    StatusBarConfig                  m_config;
};

}  // namespace neomifes::ui
