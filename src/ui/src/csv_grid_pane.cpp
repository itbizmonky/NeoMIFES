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
// continue within the same block rather than starting a new one. WI-16g:
// kListId is renamed kFrozenListId (same numeric value - it was always the
// "#" column's own control id, now it names the whole HWND that column
// lives in); kDataListId/kListDividerId are new, continuing past
// kFilterBackdropId.
constexpr int      kFrozenListId  = 10001;
constexpr int      kFilterLabelId = 10002;
constexpr int      kFilterEditId  = 10003;
// WI-16f: the cell-edit overlay - continues the same block, no WM_COMMAND
// notification is ever routed by this id (unlike kFilterEditId's EN_CHANGE)
// so it does not need to appear in handleCommand()'s own id comparison.
constexpr int      kCellEditorId  = 10004;
// WI-16f bugfix: the filter-row backdrop - see m_hwndFilterBackdrop's own
// header comment. No notification is ever routed by this id either.
constexpr int      kFilterBackdropId = 10005;
// WI-16g: the scrollable real-CSV-columns list and the opaque divider
// strip between it and kFrozenListId - see this file's own class header
// comment for why the pane is now two ListView HWNDs, not one.
constexpr int      kDataListId      = 10006;
constexpr int      kListDividerId   = 10007;
constexpr UINT_PTR kSubclassId      = 1;
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
// WI-16g: the seam between the frozen "#" list and the scrollable data
// list - filled by m_hwndListDivider (an opaque WC_STATIC, same "cover the
// gap" reasoning m_hwndFilterBackdrop already established for WI-16f) so
// the Direct2D document view never shows through it.
constexpr float kListDividerWidthDips = 2.0F;

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

// WI-16g: applies the exact same extended styles to a list, called once per
// list from createListViews() - a shared free function specifically so
// neither list can silently end up with a different style set than the
// other. Verified alongside LVS_OWNERDATA in WI-16c's own standalone probe
// (see this class's header comment) - grid lines make column boundaries
// legible for a spreadsheet-like view, double-buffering avoids the flicker
// LVS_OWNERDATA's per-cell repaint would otherwise cause. LVS_EX_FULLROWSELECT
// is required for hit-testing to resolve a valid iItem outside subitem 0 (a
// real-machine WI-16c dogfooding discovery, see git history for the WI-16f
// bugfix commit) - the exact lesson this shared function exists to prevent
// from being silently reintroduced on just one of the two new lists.
void applyListExtendedStyles(HWND hwndList) noexcept {
    ::SendMessageW(hwndList, LVM_SETEXTENDEDLISTVIEWSTYLE,
                  LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT,
                  LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT);
}

// WI-16g: true for the virtual keys that move a ListView's own top-visible
// row (arrow up/down, PageUp/PageDown, Home/End) - the set
// tryForwardListScrollMessage() intercepts on WM_KEYDOWN.
[[nodiscard]] bool isListNavigationKey(UINT vkCode) noexcept {
    return vkCode == VK_UP || vkCode == VK_DOWN || vkCode == VK_PRIOR || vkCode == VK_NEXT || vkCode == VK_HOME ||
           vkCode == VK_END;
}

}  // namespace

bool CsvGridPane::createListViews(HWND parent, HINSTANCE hInstance) noexcept {
    // WI-16g: LVS_SINGLESEL - the original single-list design had no
    // explicit single/multi-select choice (multi-select was technically
    // enabled but never used or tested). Adopting LVS_SINGLESEL here keeps
    // selection changes routed through per-item LVN_ITEMCHANGED rather than
    // the range-oriented LVN_ODSTATECHANGED even under owner-data virtual
    // mode (verified via this WI's own standalone probe), which is what
    // handleItemChanged()'s cross-list selection mirroring below relies on
    // - and matches the "reads as one spreadsheet-like table" mental model
    // this whole feature is meant to create.
    constexpr DWORD kListStyle = WS_CHILD | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS;

    HWND frozenList = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", kListStyle, 0, 0, 10, 10, parent,
                                        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kFrozenListId)), hInstance,
                                        nullptr);
    if (frozenList == nullptr) {
        return false;
    }
    m_hwndFrozenList.reset(frozenList);

    HWND dataList = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", kListStyle, 0, 0, 10, 10, parent,
                                      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kDataListId)), hInstance,
                                      nullptr);
    if (dataList == nullptr) {
        return false;
    }
    m_hwndDataList.reset(dataList);

    // WI-16g bugfix-in-advance: an opaque WC_STATIC filling the seam
    // between the two lists - same "the Direct2D document view paints
    // through any gap between native child HWNDs" lesson m_hwndFilterBackdrop
    // already exists to prevent (WI-16f dogfooding discovery), applied
    // preemptively here rather than waiting for a user to find it.
    HWND divider = ::CreateWindowExW(0, WC_STATICW, L"", WS_CHILD | SS_LEFT, 0, 0, 10, 10, parent,
                                     reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kListDividerId)), hInstance,
                                     nullptr);
    if (divider == nullptr) {
        return false;
    }
    m_hwndListDivider.reset(divider);

    if (::SetWindowSubclass(m_hwndFrozenList.get(), &CsvGridPane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }
    if (::SetWindowSubclass(m_hwndDataList.get(), &CsvGridPane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    applyListExtendedStyles(m_hwndFrozenList.get());
    applyListExtendedStyles(m_hwndDataList.get());

    // WI-16g bugfix: the frozen list's single "#" column is inserted HERE,
    // once, rather than deleted+reinserted on every showWith() call the way
    // the data list's columns are - unlike the data list (whose column set/
    // labels genuinely change per document), the "#" column is always
    // identical (same width, same "#" text) on every call, so there is
    // nothing for a rebuild to accomplish. A real-machine dogfooding
    // discovery: repeating the delete+insert dance on this list on a SECOND
    // showWith() call (e.g. triggered by a sort-column click) left the
    // frozen list's rows visually blank - LVN_GETDISPINFOW kept firing
    // correctly (confirmed via temporary diagnostic logging) and this
    // class's own handleGetDispInfo() kept writing the row-number text into
    // the supplied buffer, but nothing painted, pointing at some comctl32
    // repaint/layout quirk specific to deleting and reinserting a REPORT-view
    // list's only column rather than a bug in this class's own data flow.
    // Inserting once and never touching it again sidesteps the quirk
    // entirely rather than working around its symptom.
    LVCOLUMNW rowNumberColumn{};
    rowNumberColumn.mask    = LVCF_TEXT | LVCF_WIDTH;
    std::wstring rowNumberHeader(L"#");
    rowNumberColumn.pszText = rowNumberHeader.data();
    rowNumberColumn.cx      = static_cast<int>(kRowNumberColumnWidthDips);
    ::SendMessageW(m_hwndFrozenList.get(), LVM_INSERTCOLUMNW, 0, reinterpret_cast<LPARAM>(&rowNumberColumn));

    return true;
}

bool CsvGridPane::create(HWND parent, HINSTANCE hInstance, const CsvGridPaneConfig& config) {
    m_config = config;

    if (!createListViews(parent, hInstance)) {
        return false;
    }

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
    // a sibling of m_hwndDataList under the same `parent`, not the list's
    // own child - see showCellEditor()'s own comment for why its position
    // needs MapWindowPoints().
    HWND cellEditor = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10,
                                        parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kCellEditorId)),
                                        hInstance, nullptr);
    if (cellEditor == nullptr) {
        return false;
    }
    m_hwndCellEditor.reset(cellEditor);

    // The filter edit and the cell editor share the same subclass
    // callback/dwRefData the two lists already got in createListViews() -
    // handleSubclassMessage() distinguishes them by the `hwnd` it receives
    // (same pattern ui::FindBar's find/replace edits already established).
    if (::SetWindowSubclass(m_hwndFilterEdit.get(), &CsvGridPane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }
    if (::SetWindowSubclass(m_hwndCellEditor.get(), &CsvGridPane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    ensureFont(1.0F);
    return true;
}

void CsvGridPane::showWith(std::vector<std::u16string> columnLabels, std::size_t dataRowCount) noexcept {
    if (!m_hwndFrozenList || !m_hwndDataList) {
        return;
    }

    // WI-16g bugfix: the frozen list's single "#" column is NOT rebuilt here
    // - it was inserted once in createListViews() and never touched again
    // (see that function's own comment for the real-machine dogfooding
    // discovery this avoids: repeating the delete+insert dance here left
    // the frozen list's rows visually blank on any call after the first,
    // even though LVN_GETDISPINFOW kept firing and this class's own
    // handleGetDispInfo() kept supplying the right text).

    // Re-invoking while already visible refreshes in place - drop every
    // existing column before rebuilding, same "delete then reinsert" idiom
    // populateTree()'s TVM_DELETEITEM call uses for OutlinePane/JsonTreePane.
    // WI-16g: the data list's own column 0 IS the caller's CSV column 0 now
    // - no "#" column lives in this list anymore, so no +1 offset on the
    // insertion index (unlike the pre-WI-16g single-list design).
    while (::SendMessageW(m_hwndDataList.get(), LVM_DELETECOLUMN, 0, 0) != FALSE) {
    }
    for (std::size_t i = 0; i < columnLabels.size(); ++i) {
        std::wstring labelW(neomifes::util::toWstringView(columnLabels[i]));
        LVCOLUMNW    column{};
        column.mask    = LVCF_TEXT | LVCF_WIDTH;
        column.pszText = labelW.data();
        column.cx      = static_cast<int>(kDataColumnWidthDips);
        ::SendMessageW(m_hwndDataList.get(), LVM_INSERTCOLUMNW, static_cast<WPARAM>(i),
                      reinterpret_cast<LPARAM>(&column));
    }
    m_columnCount = columnLabels.size();

    ::SendMessageW(m_hwndFrozenList.get(), LVM_SETITEMCOUNT, static_cast<WPARAM>(dataRowCount), LVSICF_NOSCROLL);
    ::SendMessageW(m_hwndDataList.get(), LVM_SETITEMCOUNT, static_cast<WPARAM>(dataRowCount), LVSICF_NOSCROLL);
    // WI-16g: comctl32 reasserts the frozen list's own vertical scrollbar on
    // every LVM_SETITEMCOUNT (confirmed via this WI's own standalone probe -
    // ShowScrollBar(FALSE) does not durably persist across it), so this must
    // be re-called every time a row count changes, not just once at
    // creation. Needed because the frozen list's 50-DIP width has no room
    // to spare for a scrollbar competing with up-to-8-digit row numbers at
    // the roadmap's stated 10-million-row scale.
    ::ShowScrollBar(m_hwndFrozenList.get(), SB_VERT, FALSE);
    // A fresh column rebuild resets each ListView's own top index to 0.
    m_syncedTopIndex = 0;

    if (m_hwndFilterBackdrop) {
        ::ShowWindow(m_hwndFilterBackdrop.get(), SW_SHOW);
    }
    if (m_hwndFilterLabel) {
        ::ShowWindow(m_hwndFilterLabel.get(), SW_SHOW);
    }
    if (m_hwndFilterEdit) {
        ::ShowWindow(m_hwndFilterEdit.get(), SW_SHOW);
    }
    if (m_hwndListDivider) {
        ::ShowWindow(m_hwndListDivider.get(), SW_SHOW);
    }
    ::ShowWindow(m_hwndFrozenList.get(), SW_SHOW);
    ::ShowWindow(m_hwndDataList.get(), SW_SHOW);
    ::SetFocus(m_hwndDataList.get());
}

void CsvGridPane::setRowCount(std::size_t dataRowCount) noexcept {
    if (!m_hwndFrozenList || !m_hwndDataList) {
        return;
    }
    ::SendMessageW(m_hwndFrozenList.get(), LVM_SETITEMCOUNT, static_cast<WPARAM>(dataRowCount), LVSICF_NOSCROLL);
    ::SendMessageW(m_hwndDataList.get(), LVM_SETITEMCOUNT, static_cast<WPARAM>(dataRowCount), LVSICF_NOSCROLL);
    ::ShowScrollBar(m_hwndFrozenList.get(), SB_VERT, FALSE);
    // Unlike showWith() (whose column rebuild deterministically resets to
    // 0), a row-count-only change may have comctl32 clamp each list's own
    // top index if the new count is smaller than the current scroll
    // position - both lists receive the identical call so they clamp
    // identically, but re-deriving from the actual post-clamp state (rather
    // than assuming 0) is the safer, "measure don't assume" choice here.
    // ListView_GetTopIndex, like ListView_GetSubItemRect above, is a
    // commctrl.h macro, not a real function - no `::` scope prefix.
    m_syncedTopIndex = ListView_GetTopIndex(m_hwndDataList.get());
}

void CsvGridPane::hide() noexcept {
    if (!m_hwndFrozenList || !m_hwndDataList) {
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
    if (m_hwndListDivider) {
        ::ShowWindow(m_hwndListDivider.get(), SW_HIDE);
    }
    ::ShowWindow(m_hwndFrozenList.get(), SW_HIDE);
    ::ShowWindow(m_hwndDataList.get(), SW_HIDE);
}

bool CsvGridPane::isVisible() const noexcept {
    return static_cast<bool>(m_hwndDataList) && ::IsWindowVisible(m_hwndDataList.get()) != FALSE;
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
    if (!m_hwndFrozenList || !m_hwndDataList) {
        return;
    }
    ensureFont(dpiScale);

    const auto topPx    = static_cast<int>(topInsetDips * dpiScale);
    const auto bottomPx = static_cast<int>(bottomInsetDips * dpiScale);
    const auto widthPx  = static_cast<int>(parentWidth);

    // WI-16e: filter row occupies a fixed-height strip immediately below
    // topInsetDips (the tab strip) - the lists themselves start below THAT,
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
    positionListViews(listTopPx, listHeightPx, widthPx, dpiScale);
}

void CsvGridPane::positionListViews(int listTopPx, int listHeightPx, int widthPx, float dpiScale) noexcept {
    const auto frozenWidthPx  = static_cast<int>(kRowNumberColumnWidthDips * dpiScale);
    const auto dividerWidthPx = static_cast<int>(kListDividerWidthDips * dpiScale);

    if (m_hwndFrozenList) {
        ::SetWindowPos(m_hwndFrozenList.get(), nullptr, 0, listTopPx, frozenWidthPx, listHeightPx,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (m_hwndListDivider) {
        ::SetWindowPos(m_hwndListDivider.get(), nullptr, frozenWidthPx, listTopPx, dividerWidthPx, listHeightPx,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
    const auto dataListX     = frozenWidthPx + dividerWidthPx;
    const auto dataListWidth = std::max(0, widthPx - dataListX);
    if (m_hwndDataList) {
        ::SetWindowPos(m_hwndDataList.get(), nullptr, dataListX, listTopPx, dataListWidth, listHeightPx,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

LRESULT CsvGridPane::handleNotify(WPARAM /*wParam*/, LPARAM lParam) noexcept {
    const auto* header = reinterpret_cast<const NMHDR*>(lParam);
    if (header == nullptr) {
        return 0;
    }
    if (header->hwndFrom != m_hwndFrozenList.get() && header->hwndFrom != m_hwndDataList.get()) {
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
    } else if (header->code == LVN_ITEMCHANGED) {
        handleItemChanged(lParam);
    }
    return 0;
}

void CsvGridPane::handleGetDispInfo(LPARAM lParam) noexcept {
    auto* info = reinterpret_cast<NMLVDISPINFOW*>(lParam);
    if ((info->item.mask & LVIF_TEXT) == 0 || info->item.pszText == nullptr || info->item.cchTextMax <= 0) {
        return;
    }

    std::wstring text;
    if (info->hdr.hwndFrom == m_hwndFrozenList.get()) {
        text = std::to_wstring(info->item.iItem + 1);
    } else if (m_config.onGetCellText) {
        // WI-16g: no "#" column lives in this list anymore, so iSubItem maps
        // straight onto the caller's own colIndex - no -1 shift.
        const auto           rowIndex = static_cast<std::size_t>(info->item.iItem);
        const auto           colIndex = static_cast<std::size_t>(info->item.iSubItem);
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
    // WI-16g: "#" activation now arrives from the separate m_hwndFrozenList
    // HWND rather than subitem 0 of one shared list - the numeric contract
    // (0 == "#", 1..columnCount == a real CSV column) is unchanged.
    const auto colIndex = activate->hdr.hwndFrom == m_hwndFrozenList.get()
                              ? std::size_t{0}
                              : static_cast<std::size_t>(activate->iSubItem < 0 ? 0 : activate->iSubItem) + 1;
    m_config.onCellActivated(rowIndex, colIndex);
}

void CsvGridPane::handleColumnClick(LPARAM lParam) noexcept {
    const auto* columnClick = reinterpret_cast<const NMLISTVIEW*>(lParam);
    if (!m_config.onSortColumnClicked) {
        return;
    }
    // WI-16g: the frozen list's own single column has no real
    // iSubItem-driven CSV sort meaning - clicking its header is always "#"
    // (reset to unsorted), matching the pre-split colIndex==0 convention.
    if (columnClick->hdr.hwndFrom == m_hwndFrozenList.get()) {
        m_config.onSortColumnClicked(0);
        return;
    }
    if (columnClick->iSubItem < 0) {
        return;
    }
    m_config.onSortColumnClicked(static_cast<std::size_t>(columnClick->iSubItem) + 1);
}

void CsvGridPane::handleClick(LPARAM lParam) noexcept {
    const auto* click = reinterpret_cast<const NMITEMACTIVATE*>(lParam);
    // WI-16g: the "#" column can never open a cell editor because it now
    // lives in the separate m_hwndFrozenList HWND entirely - a click that
    // didn't land on m_hwndDataList is rejected outright, rather than the
    // pre-split "iSubItem<=0" check (subitem 0 on the data list IS now a
    // real, editable CSV column).
    if (click->hdr.hwndFrom != m_hwndDataList.get() || click->iItem < 0 || click->iSubItem < 0) {
        return;
    }
    if (m_config.canBeginCellEdit && !m_config.canBeginCellEdit()) {
        return;
    }
    showCellEditor(static_cast<std::size_t>(click->iItem), static_cast<std::size_t>(click->iSubItem));
}

void CsvGridPane::handleItemChanged(LPARAM lParam) noexcept {
    if (m_syncingSelection || !m_hwndFrozenList || !m_hwndDataList) {
        return;
    }
    const auto* changed = reinterpret_cast<const NMLISTVIEW*>(lParam);
    // A bulk/summary notification (iItem==-1, seen in this WI's own
    // standalone probe alongside per-item ones during a selection move) has
    // no single row to mirror - only real state changes are worth
    // forwarding, so this also skips anything that isn't an
    // LVIS_SELECTED/LVIS_FOCUSED transition.
    if (changed->iItem < 0 || (changed->uChanged & LVIF_STATE) == 0) {
        return;
    }
    constexpr UINT kStateMask = LVIS_SELECTED | LVIS_FOCUSED;
    if ((changed->uOldState & kStateMask) == (changed->uNewState & kStateMask)) {
        return;
    }
    HWND other = (changed->hdr.hwndFrom == m_hwndFrozenList.get()) ? m_hwndDataList.get() : m_hwndFrozenList.get();
    m_syncingSelection = true;
    // ListView_SetItemState is a commctrl.h macro - no `::` scope prefix.
    ListView_SetItemState(other, changed->iItem, changed->uNewState & kStateMask, kStateMask);
    m_syncingSelection = false;
}

int CsvGridPane::rowHeightPx() noexcept {
    if (m_rowHeightPx > 0 || !m_hwndDataList) {
        return m_rowHeightPx;
    }
    RECT rect{};
    if (ListView_GetItemRect(m_hwndDataList.get(), 0, &rect, LVIR_BOUNDS) == FALSE) {
        return 0;
    }
    m_rowHeightPx = static_cast<int>(rect.bottom - rect.top);
    return m_rowHeightPx;
}

bool CsvGridPane::tryForwardListScrollMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                              LRESULT& result) noexcept {
    if (hwnd != m_hwndFrozenList.get() && hwnd != m_hwndDataList.get()) {
        return false;
    }
    const bool isScrollOrWheel = msg == WM_VSCROLL || msg == WM_MOUSEWHEEL;
    const bool isNavKey        = msg == WM_KEYDOWN && isListNavigationKey(static_cast<UINT>(wParam));
    if (!isScrollOrWheel && !isNavKey) {
        return false;
    }
    result = ::DefSubclassProc(hwnd, msg, wParam, lParam);
    syncScrollAfterMessage(hwnd);
    return true;
}

void CsvGridPane::syncScrollAfterMessage(HWND source) noexcept {
    if (!m_hwndFrozenList || !m_hwndDataList) {
        return;
    }
    // ListView_GetTopIndex/ListView_Scroll are commctrl.h macros - no `::`
    // scope prefix (same "ListView_GetSubItemRect" caveat showCellEditor()
    // notes).
    const int newTop = ListView_GetTopIndex(source);
    const int deltaRows = newTop - m_syncedTopIndex;
    if (deltaRows != 0) {
        const int heightPx = rowHeightPx();
        if (heightPx > 0) {
            HWND other = (source == m_hwndFrozenList.get()) ? m_hwndDataList.get() : m_hwndFrozenList.get();
            ListView_Scroll(other, 0, deltaRows * heightPx);
        }
    }
    m_syncedTopIndex = newTop;
}

void CsvGridPane::showCellEditor(std::size_t rowIndex, std::size_t colIndex) noexcept {
    if (!m_hwndCellEditor || !m_hwndDataList || !m_config.onGetCellText) {
        return;
    }
    RECT rect{};
    // WI-16g: no "#" column shift anymore - colIndex maps straight onto
    // m_hwndDataList's own subitem index. LVM_GETSUBITEMRECT has its own
    // quirk here though (confirmed via this WI's own standalone probe):
    // with iSubItem==0 it returns the WHOLE ROW's bounds (every column),
    // not column 0's own - a pre-WI-16g impossibility, since subitem 0 was
    // always the (never-editable) "#" column back then. Narrow it to
    // column 0's actual width via LVM_GETCOLUMNWIDTH when that happens;
    // subitem>=1 already returns a correctly column-scoped rect natively.
    const int subItem = static_cast<int>(colIndex);
    // ListView_GetSubItemRect is a commctrl.h macro, not a real function -
    // no `::` scope prefix (unlike every other Win32 API call in this
    // file).
    if (ListView_GetSubItemRect(m_hwndDataList.get(), static_cast<int>(rowIndex), subItem, LVIR_BOUNDS, &rect) ==
        FALSE) {
        return;
    }
    if (subItem == 0) {
        rect.right = rect.left + ListView_GetColumnWidth(m_hwndDataList.get(), 0);
    }
    // rect is in m_hwndDataList's own client coordinates; m_hwndCellEditor is
    // a sibling under the same parent (see create()'s own comment), not the
    // list's child, so it needs the parent's coordinate space.
    HWND parent = ::GetParent(m_hwndDataList.get());
    ::MapWindowPoints(m_hwndDataList.get(), parent, reinterpret_cast<POINT*>(&rect), 2);

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
    // WI-16g: a font change can change row height (rowHeightPx()'s cached
    // measurement), so invalidate it here - the next scroll-sync call
    // re-measures lazily.
    m_rowHeightPx = 0;
    if (m_hwndFrozenList) {
        ::SendMessageW(m_hwndFrozenList.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (m_hwndDataList) {
        ::SendMessageW(m_hwndDataList.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
    // WI-16g: scroll/keyboard-navigation messages on either list are
    // forwarded to the other list FIRST, before any of the switch below -
    // Escape/Enter/IME handling never matches a nav-key virtual-key code
    // anyway, but checking this first keeps "keep the two lists in sync"
    // visibly separate from "everything else this subclass already did".
    LRESULT scrollResult = 0;
    if (tryForwardListScrollMessage(hwnd, msg, wParam, lParam, scrollResult)) {
        return scrollResult;
    }
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
                // branch is shared by every subclassed HWND (neither list
                // ever composes, so m_composing is always false when
                // Escape arrives from either of them).
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
                    ::SetFocus(m_hwndDataList.get());
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
            // data list - moving focus synchronously delivers WM_KILLFOCUS to
            // the editor first, which would call commitCellEditor() a
            // second time, but that call is a safe no-op by then
            // (m_cellEditorActive is already false, see commitCellEditor()'s
            // own comment).
            if (m_hwndCellEditor && hwnd == m_hwndCellEditor.get() && !m_composing &&
                static_cast<UINT>(wParam) == VK_RETURN) {
                commitCellEditor();
                ::SetFocus(m_hwndDataList.get());
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
