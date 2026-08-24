#include "neomifes/ui/csv_grid_pane.h"

#include <commctrl.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

// Continues the child-control ID block list json_tree_pane.cpp's own kTreeId
// comment documents (..., outline_pane 5001, tab_bar 6001, status_bar 7001,
// json_tree_pane 9001) - 10001 is the next free block. 10002/10003 (WI-16e)
// continue within the same block rather than starting a new one.
constexpr int      kListId        = 10001;
constexpr int      kFilterLabelId = 10002;
constexpr int      kFilterEditId  = 10003;
// WI-16f: the cell-edit overlay - continues the same block, no WM_COMMAND
// notification is ever routed by this id (unlike kFilterEditId's EN_CHANGE)
// so it does not need to appear in handleCommand()'s own id comparison.
constexpr int      kCellEditorId  = 10004;
// WI-16f bugfix: the filter-row backdrop - see m_hwndFilterBackdrop's own
// header comment. No notification is ever routed by this id either.
constexpr int      kFilterBackdropId = 10005;
constexpr UINT_PTR kSubclassId    = 1;
// Scoped per-HWND by Win32 (SetTimer/KillTimer take the owning window as a
// parameter) - safe to reuse id 1 here even though ui::FindBar also uses
// id 1 for its own debounce timer, since the two are never set on the same
// HWND.
constexpr UINT_PTR kFilterDebounceTimerId = 1;
constexpr UINT     kFilterDebounceMs      = 150;

constexpr float kFontSizeDips = 14.0F;

// Fixed DIP widths - same "pick something reasonable, adjust later if
// dogfooding shows a problem" spirit as status_bar.cpp's kXxxWidthDips.
// Data columns are wider than the row-number column since CSV values are
// typically longer than a row ordinal; the user can drag either wider
// (LVS_REPORT's standard column-resize behavior, no extra code needed).
constexpr float kRowNumberColumnWidthDips = 50.0F;
constexpr float kDataColumnWidthDips      = 120.0F;

// WI-16e: filter row layout, in DIPs (96-DPI baseline, scaled by
// onParentResized()'s dpiScale) - same convention as find_bar.cpp's own
// kEditWidthDips/kLabelWidthDips/kHeightDips/kMarginDips.
constexpr float kFilterRowHeightDips     = 32.0F;
constexpr float kFilterLabelWidthDips    = 60.0F;
constexpr float kFilterMarginDips        = 8.0F;
constexpr float kFilterControlHeightDips = 24.0F;

// Shared by fireFilterQueryChanged() - identical
// GetWindowTextLengthW/GetWindowTextW/fromWstringView() sequence
// ui::FindBar::readEditText() (find_bar.cpp) already established for the
// same purpose.
[[nodiscard]] std::u16string readEditText(HWND hwnd) {
    const int length = ::GetWindowTextLengthW(hwnd);
    std::wstring buffer(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        ::GetWindowTextW(hwnd, buffer.data(), length + 1);
    }
    return std::u16string(neomifes::util::fromWstringView(buffer));
}

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

    // WI-16f bugfix: filter-row backdrop, created BEFORE the label/edit below
    // so it sits behind them in z-order (each subsequently created sibling
    // is inserted at the front) - see m_hwndFilterBackdrop's own header
    // comment for why this needs to exist at all.
    HWND filterBackdrop = ::CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | SS_LEFT, 0, 0, 10, 10, parent,
                                            reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFilterBackdropId)),
                                            hInstance, nullptr);
    if (filterBackdrop == nullptr) {
        return false;
    }
    m_hwndFilterBackdrop.reset(filterBackdrop);

    // WI-16e: filter row - a WC_STATIC label + WC_EDIT, same control shapes
    // find_bar.cpp's own m_hwndInfoLabel/m_hwndFindEdit use.
    HWND filterLabel = ::CreateWindowExW(0, WC_STATICW, L"フィルタ:", WS_CHILD | SS_LEFT, 0, 0, 10, 10, parent,
                                        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFilterLabelId)), hInstance,
                                        nullptr);
    if (filterLabel == nullptr) {
        return false;
    }
    m_hwndFilterLabel.reset(filterLabel);

    HWND filterEdit = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10,
                                        parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFilterEditId)),
                                        hInstance, nullptr);
    if (filterEdit == nullptr) {
        return false;
    }
    m_hwndFilterEdit.reset(filterEdit);

    // WI-16f: cell-edit overlay - created once here (like the filter edit
    // above), positioned+shown per click by showCellEditor() rather than
    // being created/destroyed on every edit. Starts hidden (no WS_VISIBLE);
    // a sibling of m_hwndList under the same `parent`, not the ListView's
    // own child - see showCellEditor()'s own comment for why its position
    // needs MapWindowPoints().
    HWND cellEditor = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10,
                                        parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kCellEditorId)),
                                        hInstance, nullptr);
    if (cellEditor == nullptr) {
        return false;
    }
    m_hwndCellEditor.reset(cellEditor);

    // The ListView, the filter edit, and the cell editor all share one
    // subclass callback/dwRefData - handleSubclassMessage() distinguishes
    // them by the `hwnd` it receives (same pattern ui::FindBar's find/
    // replace edits already established).
    if (::SetWindowSubclass(m_hwndList.get(), &CsvGridPane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }
    if (::SetWindowSubclass(m_hwndFilterEdit.get(), &CsvGridPane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }
    if (::SetWindowSubclass(m_hwndCellEditor.get(), &CsvGridPane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    // Verified alongside LVS_OWNERDATA in this WI's standalone probe (see
    // this class's header comment) - grid lines make column boundaries
    // legible for a spreadsheet-like view, double-buffering avoids the
    // flicker LVS_OWNERDATA's per-cell repaint would otherwise cause.
    // WI-16f: added LVS_EX_FULLROWSELECT - without it, NM_CLICK/
    // LVN_ITEMACTIVATE's hit-testing only resolves a valid iItem for clicks
    // in subitem 0 (the "#" column); every real CSV column (subitem 1+)
    // reported iItem=-1, a real-machine dogfooding discovery (WI-16c's own
    // cell-activate-by-mouse path had the identical latent bug since
    // 2026-08-19, never exercised by a real click before now - WI-16c's own
    // dogfooding only confirmed the keyboard/WM_COMMAND path, see that WI's
    // own completion record).
    ::SendMessageW(m_hwndList.get(), LVM_SETEXTENDEDLISTVIEWSTYLE,
                  LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT,
                  LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);

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

    if (m_hwndFilterBackdrop) {
        ::ShowWindow(m_hwndFilterBackdrop.get(), SW_SHOW);
    }
    if (m_hwndFilterLabel) {
        ::ShowWindow(m_hwndFilterLabel.get(), SW_SHOW);
    }
    if (m_hwndFilterEdit) {
        ::ShowWindow(m_hwndFilterEdit.get(), SW_SHOW);
    }
    ::ShowWindow(m_hwndList.get(), SW_SHOW);
    ::SetFocus(m_hwndList.get());
}

void CsvGridPane::setRowCount(std::size_t dataRowCount) noexcept {
    if (!m_hwndList) {
        return;
    }
    ::SendMessageW(m_hwndList.get(), LVM_SETITEMCOUNT, static_cast<WPARAM>(dataRowCount), LVSICF_NOSCROLL);
}

void CsvGridPane::hide() noexcept {
    if (!m_hwndList) {
        return;
    }
    cancelCellEditor();
    if (m_hwndFilterBackdrop) {
        ::ShowWindow(m_hwndFilterBackdrop.get(), SW_HIDE);
    }
    if (m_hwndFilterLabel) {
        ::ShowWindow(m_hwndFilterLabel.get(), SW_HIDE);
    }
    if (m_hwndFilterEdit) {
        ::ShowWindow(m_hwndFilterEdit.get(), SW_HIDE);
    }
    ::ShowWindow(m_hwndList.get(), SW_HIDE);
}

bool CsvGridPane::isVisible() const noexcept {
    return static_cast<bool>(m_hwndList) && ::IsWindowVisible(m_hwndList.get()) != FALSE;
}

void CsvGridPane::setFilterQueryText(std::u16string_view text) noexcept {
    if (!m_hwndFilterEdit) {
        return;
    }
    const std::wstring textW(neomifes::util::toWstringView(text));
    ::SetWindowTextW(m_hwndFilterEdit.get(), textW.c_str());
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

    // WI-16e: filter row occupies a fixed-height strip immediately below
    // topInsetDips (the tab strip) - the ListView itself starts below THAT,
    // not below topInsetDips directly.
    const auto filterRowPx     = static_cast<int>(kFilterRowHeightDips * dpiScale);
    const auto marginPx        = static_cast<int>(kFilterMarginDips * dpiScale);
    const auto labelWidthPx    = static_cast<int>(kFilterLabelWidthDips * dpiScale);
    const auto controlHeightPx = static_cast<int>(kFilterControlHeightDips * dpiScale);
    const auto controlYPx      = topPx + ((filterRowPx - controlHeightPx) / 2);

    // WI-16f bugfix: spans the WHOLE [topPx, topPx+filterRowPx) band, not
    // just the sub-rect the label/edit controls themselves occupy - see
    // m_hwndFilterBackdrop's own header comment for why this needs to fully
    // cover the band with no gaps.
    if (m_hwndFilterBackdrop) {
        ::SetWindowPos(m_hwndFilterBackdrop.get(), nullptr, 0, topPx, widthPx, filterRowPx,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hwndFilterLabel) {
        ::SetWindowPos(m_hwndFilterLabel.get(), nullptr, marginPx, controlYPx, labelWidthPx, controlHeightPx,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hwndFilterEdit) {
        const auto editX       = marginPx + labelWidthPx + marginPx;
        const auto editWidthPx = std::max(0, widthPx - editX - marginPx);
        ::SetWindowPos(m_hwndFilterEdit.get(), nullptr, editX, controlYPx, editWidthPx, controlHeightPx,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }

    const auto listTopPx    = topPx + filterRowPx;
    const auto listHeightPx = static_cast<int>(parentHeight) - listTopPx - bottomPx;
    ::SetWindowPos(m_hwndList.get(), nullptr, 0, listTopPx, widthPx, listHeightPx, SWP_NOZORDER | SWP_NOACTIVATE);
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
    } else if (header->code == LVN_COLUMNCLICK) {
        handleColumnClick(lParam);
    } else if (header->code == NM_CLICK) {
        handleClick(lParam);
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

void CsvGridPane::handleColumnClick(LPARAM lParam) noexcept {
    const auto* columnClick = reinterpret_cast<const NMLISTVIEW*>(lParam);
    if (columnClick->iSubItem < 0 || !m_config.onSortColumnClicked) {
        return;
    }
    m_config.onSortColumnClicked(static_cast<std::size_t>(columnClick->iSubItem));
}

void CsvGridPane::handleClick(LPARAM lParam) noexcept {
    const auto* click = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
    if (click->iItem < 0 || click->iSubItem <= 0) {
        return;  // no row under the click, or the synthesized "#" column
    }
    if (m_config.canBeginCellEdit && !m_config.canBeginCellEdit()) {
        return;
    }
    showCellEditor(static_cast<std::size_t>(click->iItem), static_cast<std::size_t>(click->iSubItem - 1));
}

void CsvGridPane::showCellEditor(std::size_t rowIndex, std::size_t colIndex) noexcept {
    if (!m_hwndCellEditor || !m_hwndList || !m_config.onGetCellText) {
        return;
    }
    RECT rect{};
    const int subItem = static_cast<int>(colIndex) + 1;  // +1 for the "#" column
    // ListView_GetSubItemRect is a commctrl.h macro, not a real function -
    // no `::` scope prefix (unlike every other Win32 API call in this
    // file).
    if (ListView_GetSubItemRect(m_hwndList.get(), static_cast<int>(rowIndex), subItem, LVIR_BOUNDS, &rect) ==
        FALSE) {
        return;
    }
    // rect is in m_hwndList's own client coordinates; m_hwndCellEditor is a
    // sibling under the same parent (see create()'s own comment), not the
    // ListView's child, so it needs the parent's coordinate space.
    HWND parent = ::GetParent(m_hwndList.get());
    ::MapWindowPoints(m_hwndList.get(), parent, reinterpret_cast<POINT*>(&rect), 2);

    m_cellEditorOriginalText = m_config.onGetCellText(rowIndex, colIndex);
    m_cellEditorRow          = rowIndex;
    m_cellEditorCol          = colIndex;
    m_cellEditorActive       = true;

    ::SetWindowPos(m_hwndCellEditor.get(), HWND_TOP, rect.left, rect.top, rect.right - rect.left,
                   rect.bottom - rect.top, SWP_SHOWWINDOW);
    const std::wstring textW(neomifes::util::toWstringView(m_cellEditorOriginalText));
    ::SetWindowTextW(m_hwndCellEditor.get(), textW.c_str());
    ::SetFocus(m_hwndCellEditor.get());
    // Select-all so typing immediately replaces the prefilled value (the
    // usual spreadsheet cell-edit convention).
    ::SendMessageW(m_hwndCellEditor.get(), EM_SETSEL, 0, -1);
}

void CsvGridPane::commitCellEditor() noexcept {
    if (!m_cellEditorActive || !m_hwndCellEditor) {
        return;
    }
    const std::u16string newText = readEditText(m_hwndCellEditor.get());
    m_cellEditorActive           = false;
    ::ShowWindow(m_hwndCellEditor.get(), SW_HIDE);
    if (newText != m_cellEditorOriginalText && m_config.onCellEditCommitted) {
        m_config.onCellEditCommitted(m_cellEditorRow, m_cellEditorCol, newText);
    }
}

void CsvGridPane::cancelCellEditor() noexcept {
    if (!m_cellEditorActive) {
        return;
    }
    m_cellEditorActive = false;
    if (m_hwndCellEditor) {
        ::ShowWindow(m_hwndCellEditor.get(), SW_HIDE);
    }
}

void CsvGridPane::fireFilterQueryChanged() noexcept {
    if (m_hwndFilterEdit) {
        ::KillTimer(m_hwndFilterEdit.get(), kFilterDebounceTimerId);
    }
    if (!m_config.onFilterQueryChanged || !m_hwndFilterEdit) {
        return;
    }
    m_config.onFilterQueryChanged(readEditText(m_hwndFilterEdit.get()));
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
    if (m_hwndFilterLabel) {
        ::SendMessageW(m_hwndFilterLabel.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (m_hwndFilterEdit) {
        ::SendMessageW(m_hwndFilterEdit.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (m_hwndCellEditor) {
        ::SendMessageW(m_hwndCellEditor.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void CsvGridPane::handleCommand(WPARAM wParam, LPARAM /*lParam*/) noexcept {
    if (LOWORD(wParam) != kFilterEditId || HIWORD(wParam) != EN_CHANGE || !m_hwndFilterEdit) {
        return;
    }
    // Debounced (same convention ui::FindBar::handleCommand() established) -
    // rapid keystrokes each restart the timer, so onFilterQueryChanged only
    // fires once the user pauses for kFilterDebounceMs.
    ::KillTimer(m_hwndFilterEdit.get(), kFilterDebounceTimerId);
    ::SetTimer(m_hwndFilterEdit.get(), kFilterDebounceTimerId, kFilterDebounceMs, nullptr);
}

bool CsvGridPane::handleFilterEditKeyDown(HWND /*hwnd*/, UINT vkCode) noexcept {
    // While an IME composition is active, Enter belongs to the IME (confirm
    // the current conversion) - same reasoning ui::FindBar::
    // handleSubclassKeyDown() gives for its own m_composing guard.
    if (m_composing) {
        return false;
    }
    if (vkCode == VK_RETURN) {
        fireFilterQueryChanged();
        return true;
    }
    return false;
}

LRESULT CsvGridPane::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_IME_STARTCOMPOSITION:
            m_composing = true;
            break;
        case WM_IME_ENDCOMPOSITION:
            m_composing = false;
            break;
        case WM_KEYDOWN:
            if (static_cast<UINT>(wParam) == VK_ESCAPE) {
                // While composing, Escape cancels the IME conversion instead
                // of closing the pane/editor (same guard
                // handleFilterEditKeyDown() applies to VK_RETURN) - this
                // branch is shared by every subclassed HWND (the ListView
                // never composes, so m_composing is always false when
                // Escape arrives from it).
                if (m_composing) {
                    break;
                }
                // WI-16f: Escape from the cell editor cancels just the edit
                // (discarding the typed text) rather than closing the whole
                // pane - a deliberately different contract from the filter
                // edit's own Escape (which falls through to the
                // whole-pane-close below, matching its pre-WI-16f
                // behavior).
                if (m_hwndCellEditor && hwnd == m_hwndCellEditor.get() && m_cellEditorActive) {
                    cancelCellEditor();
                    ::SetFocus(m_hwndList.get());
                    return 0;
                }
                hide();
                if (m_config.onClosed) {
                    m_config.onClosed();
                }
                return 0;
            }
            if (m_hwndFilterEdit && hwnd == m_hwndFilterEdit.get() &&
                handleFilterEditKeyDown(hwnd, static_cast<UINT>(wParam))) {
                return 0;
            }
            // WI-16f: Enter commits the cell editor and returns focus to the
            // ListView - moving focus synchronously delivers WM_KILLFOCUS to
            // the editor first, which would call commitCellEditor() a
            // second time, but that call is a safe no-op by then
            // (m_cellEditorActive is already false, see commitCellEditor()'s
            // own comment).
            if (m_hwndCellEditor && hwnd == m_hwndCellEditor.get() && !m_composing &&
                static_cast<UINT>(wParam) == VK_RETURN) {
                commitCellEditor();
                ::SetFocus(m_hwndList.get());
                return 0;
            }
            break;
        case WM_KILLFOCUS:
            // WI-16f: losing focus also commits (spreadsheet-like UX) -
            // e.g. clicking a different cell, which itself triggers this
            // synchronously before the new cell's own NM_CLICK is
            // processed, so ordering (commit old, then open new) is safe.
            if (m_hwndCellEditor && hwnd == m_hwndCellEditor.get()) {
                commitCellEditor();
            }
            break;
        case WM_TIMER:
            if (wParam == kFilterDebounceTimerId) {
                fireFilterQueryChanged();
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
