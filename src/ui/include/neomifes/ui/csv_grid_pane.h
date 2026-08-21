#pragma once

// CsvGridPane - the CSV grid panel's WC_LISTVIEW child control (Phase 10.2,
// WI-16c). Win32-mechanics-only, knows nothing about neomifes::csvmode - see
// ui::OutlinePane's own class comment for why (the same "ui:: never depends
// on the domain module that produces its data" principle this codebase
// already established for OutlinePane/syntax:: and JsonTreePane/jsontree::).
//
// LVS_REPORT | LVS_OWNERDATA (virtual mode), NOT a normal WC_LISTVIEW that
// holds every row as a real LVITEM - the requirements doc's own stated scale
// (10 million rows) makes per-row item storage a real cost from the start,
// the same "decided now, not optimized later" reasoning csv_model.h's own
// CSR-layout comment gives for CsvModel::m_cells. Verified via a standalone
// probe before this file was written (per CLAUDE.md rule 3): LVM_SETITEMCOUNT
// accepted 10,000,000 in 0ms, LVN_GETDISPINFOW correctly reported iItem/
// iSubItem for both programmatic ListView_GetItemText() calls and real
// paint-driven queries, and cchTextMax truncation behaves exactly as
// documented. onGetCellText() below is called from exactly that code path.
//
// Unlike ui::OutlinePane/ui::JsonTreePane (260dip right-docked strips that
// coexist with the document view), CsvGridPane replaces the ENTIRE client
// area between the tab strip and the status bar - a multi-column table
// cannot fit in a narrow side panel. onParentResized() therefore takes
// explicit top/bottom insets (the caller's own ui::TabBar::heightDips()/
// ui::StatusBar::heightDips(), scaled by the same dpiScale) rather than
// hardcoding a dependency on those sibling classes - CsvGridPane stays
// exactly as decoupled from TabBar/StatusBar as OutlinePane/JsonTreePane
// already are from every OTHER widget class.
//
// Column 0 is always a synthesized "#" (1-based row number) column this
// class fills in directly from rowIndex+1, never routed through
// onGetCellText()/onCellActivated() - the caller's csvmode::CsvModel-aware
// bridge code (csv_grid_bridge.h) never needs to know this column exists.
// Columns 1..columnCount map to the caller's own CSV columns 0..columnCount-1
// (see onGetCellText()'s own comment for the exact index shift).

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

struct CsvGridPaneConfig {
    // Called synchronously from handleNotify()'s LVN_GETDISPINFOW handling
    // (never for the synthesized "#" column, see this header's own top
    // comment) - `colIndex` is 0-based into the caller's OWN column space
    // (already shifted back down from this pane's internal 1-based column
    // slot). `rowIndex` is the data-row index the pane was given via
    // showWith()'s dataRowCount, not a raw document::Document line number.
    std::function<std::u16string(std::size_t rowIndex, std::size_t colIndex)> onGetCellText;
    // Double-click or Enter (LVN_ITEMACTIVATE). `colIndex` is THIS pane's own
    // internal column index (0 == the "#" row-number column was activated,
    // 1..columnCount == a real CSV column) - deliberately NOT pre-shifted
    // the way onGetCellText's is, since "the row-number column itself was
    // activated" is meaningful information the caller needs to distinguish
    // (jump to the row's own first cell) from "CSV column 0 was activated".
    std::function<void(std::size_t rowIndex, std::size_t colIndex)> onCellActivated;
    // Escape. Caller restores focus to the document (same contract as
    // OutlinePaneConfig::onClosed/JsonTreePaneConfig::onClosed).
    std::function<void()> onClosed;
};

class CsvGridPane {
public:
    CsvGridPane()  = default;
    ~CsvGridPane() = default;

    CsvGridPane(const CsvGridPane&)            = delete;
    CsvGridPane& operator=(const CsvGridPane&) = delete;
    CsvGridPane(CsvGridPane&&)                 = delete;
    CsvGridPane& operator=(CsvGridPane&&)      = delete;

    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const CsvGridPaneConfig& config);

    // Replaces the grid's columns/row count and shows the panel.
    // `columnLabels.size()` becomes this pane's real-CSV-column count (the
    // synthesized "#" column is always an additional, implicit column 0 on
    // top of these) - pass app::buildCsvGridColumnLabels()'s result
    // directly. No row DATA is passed here (unlike OutlinePane::showWith()'s
    // full item tree) - virtual mode means actual cell text is pulled
    // lazily through onGetCellText() only for rows the control actually
    // paints. Re-invoking while already visible refreshes in place (mirrors
    // OutlinePane::showWith()'s own contract).
    void showWith(std::vector<std::u16string> columnLabels, std::size_t dataRowCount) noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    // topInsetDips/bottomInsetDips are the caller's own tab-strip/status-bar
    // heights (see this header's top comment) - already in DIPs, scaled by
    // dpiScale the same way parentWidth/parentHeight's derived pixel values
    // are.
    void onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight, float dpiScale,
                         float topInsetDips, float bottomInsetDips) noexcept;

    // Routes a WM_NOTIFY the owning MainWindow received (LVN_GETDISPINFOW/
    // LVN_ITEMACTIVATE arrive here, not at the child itself - same routing
    // OutlinePane/JsonTreePane already use for their own WM_NOTIFY codes,
    // see MainWindowConfig::onNotify's doc comment). Call from
    // MainWindowConfig::onNotify.
    LRESULT handleNotify(WPARAM wParam, LPARAM lParam) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                         DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    void handleGetDispInfo(LPARAM lParam) noexcept;
    void handleItemActivate(LPARAM lParam) noexcept;
    void ensureFont(float dpiScale) noexcept;

    neomifes::platform::WindowHandle    m_hwndList;
    neomifes::platform::GdiObjectHandle m_font;
    CsvGridPaneConfig                   m_config;
    std::size_t                         m_columnCount = 0;  // real CSV columns only, excludes the "#" column
};

}  // namespace neomifes::ui
