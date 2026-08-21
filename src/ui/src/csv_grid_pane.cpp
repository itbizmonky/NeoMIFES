#include "neomifes/ui/csv_grid_pane.h"

#include <commctrl.h>

#include <cwchar>
#include <string>
#include <vector>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

// Continues the child-control ID block list json_tree_pane.cpp's own kTreeId
// comment documents (..., outline_pane 5001, tab_bar 6001, status_bar 7001,
// json_tree_pane 9001) - 10001 is the next free block.
constexpr int      kListId     = 10001;
constexpr UINT_PTR kSubclassId = 1;

constexpr float kFontSizeDips = 14.0F;

// Fixed DIP widths - same "pick something reasonable, adjust later if
// dogfooding shows a problem" spirit as status_bar.cpp's kXxxWidthDips.
// Data columns are wider than the row-number column since CSV values are
// typically longer than a row ordinal; the user can drag either wider
// (LVS_REPORT's standard column-resize behavior, no extra code needed).
constexpr float kRowNumberColumnWidthDips = 50.0F;
constexpr float kDataColumnWidthDips      = 120.0F;

}  // namespace

bool CsvGridPane::create(HWND parent, HINSTANCE hInstance, const CsvGridPaneConfig& config) {
    m_config = config;

    HWND list = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0, 0, 10, 10, parent,
                                  reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kListId)), hInstance, nullptr);
    if (list == nullptr) {
        return false;
    }
    m_hwndList.reset(list);

    if (::SetWindowSubclass(m_hwndList.get(), &CsvGridPane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    // Verified alongside LVS_OWNERDATA in this WI's standalone probe (see
    // this class's header comment) - grid lines make column boundaries
    // legible for a spreadsheet-like view, double-buffering avoids the
    // flicker LVS_OWNERDATA's per-cell repaint would otherwise cause.
    ::SendMessageW(m_hwndList.get(), LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER,
                  LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    ensureFont(1.0F);
    return true;
}

void CsvGridPane::showWith(std::vector<std::u16string> columnLabels, std::size_t dataRowCount) noexcept {
    if (!m_hwndList) {
        return;
    }

    // Re-invoking while already visible refreshes in place - drop every
    // existing column (including the "#" one from a previous showWith())
    // before rebuilding, same "delete then reinsert" idiom
    // populateTree()'s TVM_DELETEITEM call uses for OutlinePane/JsonTreePane.
    while (::SendMessageW(m_hwndList.get(), LVM_DELETECOLUMN, 0, 0) != FALSE) {
    }

    LVCOLUMNW rowNumberColumn{};
    rowNumberColumn.mask    = LVCF_TEXT | LVCF_WIDTH;
    std::wstring rowNumberHeader(L"#");
    rowNumberColumn.pszText = rowNumberHeader.data();
    rowNumberColumn.cx      = static_cast<int>(kRowNumberColumnWidthDips);
    ::SendMessageW(m_hwndList.get(), LVM_INSERTCOLUMNW, 0, reinterpret_cast<LPARAM>(&rowNumberColumn));

    for (std::size_t i = 0; i < columnLabels.size(); ++i) {
        std::wstring labelW(neomifes::util::toWstringView(columnLabels[i]));
        LVCOLUMNW    column{};
        column.mask    = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = labelW.data();
        column.cx      = static_cast<int>(kDataColumnWidthDips);
        ::SendMessageW(m_hwndList.get(), LVM_INSERTCOLUMNW, static_cast<WPARAM>(i + 1),
                      reinterpret_cast<LPARAM>(&column));
    }
    m_columnCount = columnLabels.size();

    ::SendMessageW(m_hwndList.get(), LVM_SETITEMCOUNT, static_cast<WPARAM>(dataRowCount), LVSICF_NOSCROLL);

    ::ShowWindow(m_hwndList.get(), SW_SHOW);
    ::SetFocus(m_hwndList.get());
}

void CsvGridPane::hide() noexcept {
    if (!m_hwndList) {
        return;
    }
    ::ShowWindow(m_hwndList.get(), SW_HIDE);
}

bool CsvGridPane::isVisible() const noexcept {
    return static_cast<bool>(m_hwndList) && ::IsWindowVisible(m_hwndList.get()) != FALSE;
}

void CsvGridPane::onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight, float dpiScale,
                                  float topInsetDips, float bottomInsetDips) noexcept {
    if (!m_hwndList) {
        return;
    }
    ensureFont(dpiScale);

    const auto topPx    = static_cast<int>(topInsetDips * dpiScale);
    const auto bottomPx = static_cast<int>(bottomInsetDips * dpiScale);
    const auto widthPx  = static_cast<int>(parentWidth);
    const auto heightPx = static_cast<int>(parentHeight) - topPx - bottomPx;

    ::SetWindowPos(m_hwndList.get(), nullptr, 0, topPx, widthPx, heightPx, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CsvGridPane::handleNotify(WPARAM /*wParam*/, LPARAM lParam) noexcept {
    const auto* header = reinterpret_cast<const NMHDR*>(lParam);
    if (header == nullptr || !m_hwndList || header->hwndFrom != m_hwndList.get()) {
        return 0;
    }
    if (header->code == LVN_GETDISPINFOW) {
        handleGetDispInfo(lParam);
    } else if (header->code == LVN_ITEMACTIVATE) {
        handleItemActivate(lParam);
    }
    return 0;
}

void CsvGridPane::handleGetDispInfo(LPARAM lParam) noexcept {
    auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
    if ((info->item.mask & LVIF_TEXT) == 0 || info->item.pszText == nullptr || info->item.cchTextMax <= 0) {
        return;
    }

    std::wstring text;
    if (info->item.iSubItem == 0) {
        text = std::to_wstring(info->item.iItem + 1);
    } else if (m_config.onGetCellText) {
        const auto           rowIndex = static_cast<std::size_t>(info->item.iItem);
        const auto           colIndex = static_cast<std::size_t>(info->item.iSubItem - 1);
        const std::u16string cellText = m_config.onGetCellText(rowIndex, colIndex);
        text                          = std::wstring(neomifes::util::toWstringView(cellText));
    }

    // LVS_OWNERDATA's own contract: the control provides a real buffer of
    // cchTextMax wchar_ts via pszText for this callback to fill - verified
    // in this WI's standalone probe (see this class's header comment),
    // truncating safely is the caller's own responsibility, not something
    // the control does for us.
    ::wcsncpy_s(info->item.pszText, static_cast<std::size_t>(info->item.cchTextMax), text.c_str(), _TRUNCATE);
}

void CsvGridPane::handleItemActivate(LPARAM lParam) noexcept {
    const auto* activate = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
    if (activate->iItem < 0 || !m_config.onCellActivated) {
        return;
    }
    const auto rowIndex = static_cast<std::size_t>(activate->iItem);
    const auto colIndex = static_cast<std::size_t>(activate->iSubItem < 0 ? 0 : activate->iSubItem);
    m_config.onCellActivated(rowIndex, colIndex);
}

void CsvGridPane::ensureFont(float dpiScale) noexcept {
    const auto fontHeightPx = -static_cast<int>(kFontSizeDips * dpiScale);
    // NOLINTNEXTLINE(misc-redundant-expression)
    constexpr int kPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    HFONT         font =
        ::CreateFontW(fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, kPitchAndFamily, L"Segoe UI");
    if (font == nullptr) {
        return;
    }
    m_font.reset(reinterpret_cast<HGDIOBJ>(font));
    if (m_hwndList) {
        ::SendMessageW(m_hwndList.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

LRESULT CsvGridPane::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
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
            ::RemoveWindowSubclass(hwnd, &CsvGridPane::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK CsvGridPane::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<CsvGridPane*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleSubclassMessage(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
