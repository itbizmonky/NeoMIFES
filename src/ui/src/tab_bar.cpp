#include "neomifes/ui/tab_bar.h"

#include <commctrl.h>

#include <algorithm>
#include <string>
#include <vector>

namespace neomifes::ui {

namespace {

// Next in the per-widget control-ID block sequence this codebase already
// uses (command_palette.cpp: 2001-2002, goto_line_bar.cpp: 3001,
// grep_bar.cpp: 4001-4003, outline_pane.cpp: 5001).
constexpr int kTabControlId = 6001;

}  // namespace

bool TabBar::create(HWND parent, HINSTANCE hInstance, const TabBarConfig& config) {
    m_config = config;

    // WS_VISIBLE set immediately (unlike OutlinePane, which starts hidden
    // and is toggled by show()/hide()) - the tab strip is always shown, see
    // this class's header comment.
    HWND tab = ::CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | TCS_TABS | TCS_SINGLELINE,
                                 0, 0, 10, 10, parent,
                                 reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kTabControlId)), hInstance,
                                 nullptr);
    if (tab == nullptr) {
        return false;
    }
    m_hwndTab.reset(tab);
    return true;
}

void TabBar::setTabs(std::vector<TabBarItem> items, std::size_t activeIndex) noexcept {
    if (!m_hwndTab || items.empty()) {
        return;
    }
    ::SendMessageW(m_hwndTab.get(), TCM_DELETEALLITEMS, 0, 0);

    for (std::size_t i = 0; i < items.size(); ++i) {
        // The dirty marker (Phase 4b8c's bookmark gutter used the same
        // U+25CF glyph) is appended here, not by the caller - TabBarItem's
        // own comment explains why this stays a ui:: rendering concern.
        // Built fresh each iteration and kept alive until TCM_INSERTITEMW
        // returns (pszText is read synchronously by that call, same
        // build-then-send ordering OutlinePane::populateTree() uses for
        // TVINSERTSTRUCTW::pszText).
        std::wstring text = items[i].isDirty ? items[i].label + L" ●" : items[i].label;
        TCITEMW tcItem{};
        tcItem.mask    = TCIF_TEXT;
        tcItem.pszText = text.data();
        ::SendMessageW(m_hwndTab.get(), TCM_INSERTITEMW, static_cast<WPARAM>(i),
                       reinterpret_cast<LPARAM>(&tcItem));
    }
    const std::size_t clampedActive = std::min(activeIndex, items.size() - 1);
    ::SendMessageW(m_hwndTab.get(), TCM_SETCURSEL, static_cast<WPARAM>(clampedActive), 0);
}

void TabBar::onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept {
    if (!m_hwndTab) {
        return;
    }
    const auto heightPx = static_cast<int>(kHeightDips * dpiScale);
    ::SetWindowPos(m_hwndTab.get(), nullptr, 0, 0, static_cast<int>(parentWidth), heightPx,
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT TabBar::handleNotify(WPARAM /*wParam*/, LPARAM lParam) noexcept {
    const auto* header = reinterpret_cast<const NMHDR*>(lParam);
    if (header == nullptr || header->code != TCN_SELCHANGE || !m_hwndTab ||
        header->hwndFrom != m_hwndTab.get()) {
        return 0;
    }
    const auto selected = ::SendMessageW(m_hwndTab.get(), TCM_GETCURSEL, 0, 0);
    if (selected < 0) {
        return 0;  // no tabs / nothing selected - shouldn't happen in practice, safe no-op regardless
    }
    if (m_config.onTabSelected) {
        m_config.onTabSelected(static_cast<std::size_t>(selected));
    }
    return 0;
}

}  // namespace neomifes::ui
