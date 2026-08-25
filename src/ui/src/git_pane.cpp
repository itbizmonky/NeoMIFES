#include "neomifes/ui/git_pane.h"

#include <commctrl.h>

#include <string>
#include <vector>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

// Continues the child-control ID block list csv_grid_pane.cpp's own comment
// documents (..., outline_pane 5001, tab_bar 6001, status_bar 7001,
// json_tree_pane 9001, csv_grid_pane 10001..10007, json_path_bar 11001) -
// 12001 is the next free block.
constexpr int      kListId     = 12001;
constexpr UINT_PTR kSubclassId = 1;

// Layout constants in DIPs (96-DPI baseline), same convention as
// outline_pane.cpp's own kPanelWidthDips/kFontSizeDips.
constexpr float kPanelWidthDips  = 260.0F;
constexpr float kFontSizeDips    = 14.0F;
constexpr float kStatusColumnWidthDips = 44.0F;

}  // namespace

bool GitPane::create(HWND parent, HINSTANCE hInstance, const GitPaneConfig& config) {
    m_config = config;

    // Plain LVS_REPORT (real items) - see this class's header comment for
    // why virtual mode is unnecessary here. LVS_SINGLESEL matches this
    // pane's single-click-to-open interaction (no multi-select use case).
    constexpr DWORD kListStyle = WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;

    HWND list = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", kListStyle, 0, 0, 10, 10, parent,
                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kListId)), hInstance, nullptr);
    if (list == nullptr) {
        return false;
    }
    m_hwndList.reset(list);

    // LVS_EX_FULLROWSELECT is required, not cosmetic - see this class's
    // header comment (WI-16c/WI-16f's own real-machine dogfooding lesson:
    // without it, NM_CLICK hit-testing only resolves iItem inside subitem
    // 0's own column bounds).
    ::SendMessageW(m_hwndList.get(), LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES,
                  LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    if (::SetWindowSubclass(m_hwndList.get(), &GitPane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    LVCOLUMNW statusColumn{};
    statusColumn.mask    = LVCF_TEXT | LVCF_WIDTH;
    std::wstring statusHeader;
    statusColumn.pszText = statusHeader.data();
    statusColumn.cx      = static_cast<int>(kStatusColumnWidthDips);
    ::SendMessageW(m_hwndList.get(), LVM_INSERTCOLUMNW, 0, reinterpret_cast<LPARAM>(&statusColumn));

    LVCOLUMNW pathColumn{};
    pathColumn.mask    = LVCF_TEXT | LVCF_WIDTH;
    std::wstring pathHeader(L"パス");
    pathColumn.pszText = pathHeader.data();
    pathColumn.cx      = static_cast<int>(kPanelWidthDips - kStatusColumnWidthDips);
    ::SendMessageW(m_hwndList.get(), LVM_INSERTCOLUMNW, 1, reinterpret_cast<LPARAM>(&pathColumn));

    ensureFont(1.0F);
    return true;
}

void GitPane::showWith(std::vector<GitPaneItem> items) noexcept {
    if (!m_hwndList) {
        return;
    }
    m_items = std::move(items);
    populateList();
    ::ShowWindow(m_hwndList.get(), SW_SHOW);
    ::SetFocus(m_hwndList.get());
}

void GitPane::hide() noexcept {
    if (!m_hwndList) {
        return;
    }
    ::ShowWindow(m_hwndList.get(), SW_HIDE);
}

bool GitPane::isVisible() const noexcept {
    return static_cast<bool>(m_hwndList) && ::IsWindowVisible(m_hwndList.get()) != FALSE;
}

void GitPane::onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight, float dpiScale) noexcept {
    if (!m_hwndList) {
        return;
    }
    ensureFont(dpiScale);

    const auto widthPx = static_cast<int>(kPanelWidthDips * dpiScale);
    const int  startX  = static_cast<int>(parentWidth) - widthPx;

    ::SetWindowPos(m_hwndList.get(), nullptr, startX, 0, widthPx, static_cast<int>(parentHeight),
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT GitPane::handleNotify(WPARAM /*wParam*/, LPARAM lParam) noexcept {
    const auto* header = reinterpret_cast<const NMHDR*>(lParam);
    if (header == nullptr || !m_hwndList || header->hwndFrom != m_hwndList.get()) {
        return 0;
    }
    if (header->code == NM_CLICK) {
        handleClick(lParam);
    }
    return 0;
}

void GitPane::handleClick(LPARAM lParam) noexcept {
    const auto* click = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
    if (click->iItem < 0 || static_cast<std::size_t>(click->iItem) >= m_items.size() || !m_config.onFileActivated) {
        return;
    }
    const GitPaneItem& item = m_items[static_cast<std::size_t>(click->iItem)];
    if (item.absolutePath.empty()) {
        // A placeholder row (git_pane_bridge.h's "not a repository"/"no
        // changes" text) - see this class's header comment.
        return;
    }
    m_config.onFileActivated(item.absolutePath);
}

void GitPane::ensureFont(float dpiScale) noexcept {
    const auto fontHeightPx = -static_cast<int>(kFontSizeDips * dpiScale);
    // NOLINTNEXTLINE(misc-redundant-expression)
    constexpr int kPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    HFONT font = ::CreateFontW(fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, kPitchAndFamily,
                               L"Segoe UI");
    if (font == nullptr) {
        return;
    }
    m_font.reset(reinterpret_cast<HGDIOBJ>(font));
    if (m_hwndList) {
        ::SendMessageW(m_hwndList.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void GitPane::populateList() noexcept {
    if (!m_hwndList) {
        return;
    }
    ::SendMessageW(m_hwndList.get(), LVM_DELETEALLITEMS, 0, 0);

    for (std::size_t i = 0; i < m_items.size(); ++i) {
        std::wstring glyphW(neomifes::util::toWstringView(m_items[i].statusGlyph));
        LVITEMW      item{};
        item.mask    = LVIF_TEXT;
        item.iItem   = static_cast<int>(i);
        item.iSubItem = 0;
        item.pszText = glyphW.data();
        ::SendMessageW(m_hwndList.get(), LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));

        std::wstring pathW(neomifes::util::toWstringView(m_items[i].displayPath));
        LVITEMW      subItem{};
        subItem.mask     = LVIF_TEXT;
        subItem.iItem    = static_cast<int>(i);
        subItem.iSubItem = 1;
        subItem.pszText  = pathW.data();
        ::SendMessageW(m_hwndList.get(), LVM_SETITEMTEXTW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&subItem));
    }
}

LRESULT GitPane::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_KEYDOWN:
            if (static_cast<UINT>(wParam) == VK_ESCAPE) {
                hide();
                if (m_config.onClosed) {
                    m_config.onClosed();
                }
                return 0;
            }
            break;
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, &GitPane::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK GitPane::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<GitPane*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleSubclassMessage(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
