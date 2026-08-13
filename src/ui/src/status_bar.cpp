#include "neomifes/ui/status_bar.h"

#include <commctrl.h>

#include <array>

namespace neomifes::ui {

namespace {

// Next in the per-widget control-ID block sequence this codebase already
// uses (command_palette.cpp: 2001-2002, goto_line_bar.cpp: 3001,
// grep_bar.cpp: 4001-4003, outline_pane.cpp: 5001, tab_bar.cpp: 6001) - see
// command_ids.h's own range-separation comment, which already reserves this
// exact value for this exact purpose.
constexpr int kStatusBarId = 7001;

// Fixed DIP widths for the first 5 parts (left-to-right); the 6th (language)
// fills whatever remains via SB_SETPARTS' -1 terminator convention.
// Untuned initial values, same "pick something reasonable, adjust later if
// dogfooding shows a problem" spirit as TabBar::kHeightDips.
constexpr float kPositionWidthDips  = 90.0F;
constexpr float kSelectionWidthDips = 110.0F;
constexpr float kEncodingWidthDips  = 90.0F;
constexpr float kLineEndingWidthDips = 70.0F;
constexpr float kOverwriteWidthDips  = 50.0F;

}  // namespace

bool StatusBar::create(HWND parent, HINSTANCE hInstance) {
    HWND status = ::CreateWindowExW(0, STATUSCLASSNAME, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
                                    reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kStatusBarId)), hInstance,
                                    nullptr);
    if (status == nullptr) {
        return false;
    }
    m_hwndStatus.reset(status);
    return true;
}

void StatusBar::setParts(const StatusBarParts& parts) noexcept {
    if (!m_hwndStatus) {
        return;
    }
    ::SendMessageW(m_hwndStatus.get(), SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(parts.position.c_str()));
    ::SendMessageW(m_hwndStatus.get(), SB_SETTEXTW, 1,
                   reinterpret_cast<LPARAM>(parts.selectionCount.c_str()));
    ::SendMessageW(m_hwndStatus.get(), SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(parts.encoding.c_str()));
    ::SendMessageW(m_hwndStatus.get(), SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(parts.lineEnding.c_str()));
    ::SendMessageW(m_hwndStatus.get(), SB_SETTEXTW, 4,
                   reinterpret_cast<LPARAM>(parts.overwriteMode.c_str()));
    ::SendMessageW(m_hwndStatus.get(), SB_SETTEXTW, 5, reinterpret_cast<LPARAM>(parts.language.c_str()));
}

void StatusBar::onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight,
                                float dpiScale) noexcept {
    if (!m_hwndStatus) {
        return;
    }
    const auto heightPx = static_cast<int>(kHeightDips * dpiScale);
    const auto widthPx  = static_cast<int>(parentWidth);
    const auto y         = static_cast<int>(parentHeight) - heightPx;
    // Explicit SetWindowPos (not the more common "send WM_SIZE and let the
    // control self-size" idiom) - same reasoning TabBar::onParentResized()
    // gives: this codebase's controls are always repositioned imperatively
    // from a resize callback, never left to react to messages they were
    // never sent (this window's own WM_SIZE is not forwarded to children).
    ::SetWindowPos(m_hwndStatus.get(), nullptr, 0, y, widthPx, heightPx, SWP_NOZORDER | SWP_NOACTIVATE);

    // SB_SETPARTS' array holds each part's right edge as an absolute X
    // pixel offset from the control's own left edge; the last entry (-1)
    // means "extends to the control's right edge" - both are Win32's own
    // API contract, not a magic value invented here.
    const int p1 = static_cast<int>(kPositionWidthDips * dpiScale);
    const int p2 = p1 + static_cast<int>(kSelectionWidthDips * dpiScale);
    const int p3 = p2 + static_cast<int>(kEncodingWidthDips * dpiScale);
    const int p4 = p3 + static_cast<int>(kLineEndingWidthDips * dpiScale);
    const int p5 = p4 + static_cast<int>(kOverwriteWidthDips * dpiScale);
    const std::array<int, 6> partEdges = {p1, p2, p3, p4, p5, -1};
    ::SendMessageW(m_hwndStatus.get(), SB_SETPARTS, static_cast<WPARAM>(partEdges.size()),
                   reinterpret_cast<LPARAM>(partEdges.data()));
}

}  // namespace neomifes::ui
