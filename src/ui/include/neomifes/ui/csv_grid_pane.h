#pragma once

// CsvGridPane - the CSV grid panel's WC_LISTVIEW child control (Phase 10.2,
// WI-16c) plus a filter edit row above it (WI-16e). Win32-mechanics-only,
// knows nothing about neomifes::csvmode - see ui::OutlinePane's own class
// comment for why (the same "ui:: never depends on the domain module that
// produces its data" principle this codebase already established for
// OutlinePane/syntax:: and JsonTreePane/jsontree::). The filter edit's
// WC_EDIT + 150ms-debounce + IME-composition-guard mechanics (WI-16e) are a
// direct copy of ui::FindBar's own established pattern (find_bar.h/.cpp) -
// this class does not invent a second UI-timing convention for the same
// kind of "text input drives an incremental result" control.
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
#include <string_view>
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
    // Escape (from either the ListView or the filter edit - see this
    // header's own class comment). Caller restores focus to the document
    // (same contract as OutlinePaneConfig::onClosed/JsonTreePaneConfig::
    // onClosed).
    std::function<void()> onClosed;
    // WI-16e: fires 150ms after the filter edit's text last changed (same
    // UI-timing convention as ui::FindBarConfig::onQueryChanged - a burst of
    // keystrokes restarts the timer, so this fires once per pause, not once
    // per keystroke) OR immediately on Enter. The caller recomputes its own
    // filtered row order and reflects it via setRowCount()/showWith().
    std::function<void(std::u16string_view query)> onFilterQueryChanged;
    // LVN_COLUMNCLICK. `colIndex` uses the SAME non-shifted space as
    // onCellActivated's (0 == the "#" row-number column's header was
    // clicked - the caller's own convention for "reset to unsorted", since
    // that column has no real CSV data to sort by; 1..columnCount == a real
    // CSV column).
    std::function<void(std::size_t colIndex)> onSortColumnClicked;
    // WI-16f: fires when the cell-edit overlay (single click on a real CSV
    // cell, never the "#" column) commits - Enter, or the overlay losing
    // focus (spreadsheet-like UX; the pane cancels instead of committing on
    // Escape, see cancelCellEditor()). Only fires when `newText` differs
    // from what onGetCellText originally supplied for this cell - a no-op
    // edit (opened, nothing changed) never reaches the caller.  `colIndex`
    // uses the SAME shifted space onGetCellText's does (0 == the caller's
    // own first CSV column) - the "#" column can never open an editor, see
    // handleClick()'s own iSubItem<=0 guard.
    std::function<void(std::size_t rowIndex, std::size_t colIndex, std::u16string newText)> onCellEditCommitted;
    // WI-16f: checked before opening a cell editor; unset behaves as
    // "always allowed" (matches onGetCellText/onCellActivated's own "unset
    // callback = feature quietly does nothing extra" shape, just inverted
    // for a permission gate). The caller vetoes while a previous edit's
    // async re-index is still in flight - opening a second editor against a
    // CsvModel that has not caught up yet would let the user commit a
    // second edit against now-stale cell positions, corrupting the
    // document (see this WI's own design notes for the full reasoning).
    std::function<bool()> canBeginCellEdit;
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
    // WI-16e: updates ONLY the row count (LVM_SETITEMCOUNT) - unlike
    // showWith(), does not delete/reinsert columns, so a user's drag-resized
    // column widths survive. Call this instead of showWith() when only the
    // FILTERED row count changed (columns/labels unchanged, e.g. the
    // caller's onFilterQueryChanged handler); showWith() remains necessary
    // whenever the column set or a label's own text changes (initial load,
    // a sort-arrow indicator update).
    void setRowCount(std::size_t dataRowCount) noexcept;
    // WI-16f: also cancels an active cell editor (never commits it) - same
    // "closing discards an in-progress, uncommitted interaction" contract
    // Escape already gives the cell editor itself (see cancelCellEditor()).
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    // Programmatically sets the filter edit's text WITHOUT firing
    // onFilterQueryChanged (WM_SETTEXT's standard contract - it does not
    // generate EN_CHANGE) - unlike ui::FindBar::setQueryText(), this is
    // deliberately a silent sync, not a "re-run the filter" trigger. Used to
    // restore a tab's own previously-set filter text when CsvGridPane is
    // reopened for a DIFFERENT EditorSession than the one it last showed
    // (the pane itself has no concept of "which session" - the caller is
    // responsible for calling this every time it also calls showWith() for
    // a session whose csvFilter() might differ from what's currently typed).
    void setFilterQueryText(std::u16string_view text) noexcept;

    // topInsetDips/bottomInsetDips are the caller's own tab-strip/status-bar
    // heights (see this header's top comment) - already in DIPs, scaled by
    // dpiScale the same way parentWidth/parentHeight's derived pixel values
    // are. WI-16e: the filter row (label+edit) reserves its own fixed-height
    // strip immediately below topInsetDips - see kFilterRowHeightDips in the
    // .cpp.
    void onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight, float dpiScale,
                         float topInsetDips, float bottomInsetDips) noexcept;

    // Routes a WM_NOTIFY the owning MainWindow received (LVN_GETDISPINFOW/
    // LVN_ITEMACTIVATE/LVN_COLUMNCLICK arrive here, not at the child itself -
    // same routing OutlinePane/JsonTreePane already use for their own
    // WM_NOTIFY codes, see MainWindowConfig::onNotify's doc comment). Call
    // from MainWindowConfig::onNotify.
    LRESULT handleNotify(WPARAM wParam, LPARAM lParam) noexcept;

    // WI-16e: routes a WM_COMMAND the owning MainWindow received (EN_CHANGE
    // from the filter edit arrives here, not at the child HWND itself - same
    // routing ui::FindBar::handleCommand() already uses). Call from
    // MainWindowConfig::onCommand.
    void handleCommand(WPARAM wParam, LPARAM lParam) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                         DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    // Returns true if the key was handled (caller should return 0 rather
    // than falling through to DefSubclassProc) - mirrors ui::FindBar::
    // handleSubclassKeyDown()'s own return-value contract. `hwnd` is always
    // m_hwndFilterEdit.get() here (the ListView's own VK_ESCAPE handling
    // stays inline in handleSubclassMessage() - it has no VK_RETURN/IME
    // concern of its own to share this function's shape with).
    [[nodiscard]] bool handleFilterEditKeyDown(HWND hwnd, UINT vkCode) noexcept;
    void handleGetDispInfo(LPARAM lParam) noexcept;
    void handleItemActivate(LPARAM lParam) noexcept;
    void handleColumnClick(LPARAM lParam) noexcept;
    // WI-16f: NM_CLICK - comctl32 delivers a populated NMITEMACTIVATE for
    // this notification too (same struct LVN_ITEMACTIVATE uses), so no
    // manual LVM_SUBITEMHITTEST is needed. No-ops on the "#" column
    // (iSubItem<=0) or when m_config.canBeginCellEdit vetoes.
    void handleClick(LPARAM lParam) noexcept;
    // Fires onFilterQueryChanged with the filter edit's current text - shared
    // by the debounce WM_TIMER and VK_RETURN's "fire now" path (mirrors
    // ui::FindBar::fireQueryChanged()).
    void fireFilterQueryChanged() noexcept;
    // WI-16f: positions+shows m_hwndCellEditor over the given cell's
    // on-screen rect (LVM_GETSUBITEMRECT, mapped from the ListView's own
    // client coordinates into the shared parent's - m_hwndCellEditor is a
    // sibling of m_hwndList, not its child), pre-filled via
    // m_config.onGetCellText and remembered in m_cellEditorOriginalText for
    // commitCellEditor()'s later no-op-edit comparison.
    void showCellEditor(std::size_t rowIndex, std::size_t colIndex) noexcept;
    // Reads the editor's current text; fires onCellEditCommitted only if it
    // differs from m_cellEditorOriginalText, then hides the editor. Safe to
    // call when no editor is active (checks m_cellEditorActive itself) -
    // both the explicit Enter path and the WM_KILLFOCUS path call this
    // unconditionally and rely on that guard for re-entrancy safety (Enter
    // synchronously moving focus back to the ListView also triggers
    // WM_KILLFOCUS on the editor).
    void commitCellEditor() noexcept;
    // Hides the editor and discards its text - never fires
    // onCellEditCommitted. Escape's own contract for the cell editor (see
    // hide()'s own comment for CsvGridPane-wide Escape/close behavior).
    void cancelCellEditor() noexcept;
    void ensureFont(float dpiScale) noexcept;

    neomifes::platform::WindowHandle    m_hwndList;
    neomifes::platform::WindowHandle    m_hwndFilterLabel;
    neomifes::platform::WindowHandle    m_hwndFilterEdit;
    neomifes::platform::WindowHandle    m_hwndCellEditor;
    neomifes::platform::GdiObjectHandle m_font;
    CsvGridPaneConfig                   m_config;
    std::size_t                         m_columnCount = 0;  // real CSV columns only, excludes the "#" column
    // WI-16f: which cell m_hwndCellEditor is currently positioned over (only
    // meaningful while m_cellEditorActive), and what onGetCellText supplied
    // when it was opened (commitCellEditor()'s no-op-edit comparison).
    bool           m_cellEditorActive = false;
    std::size_t    m_cellEditorRow    = 0;
    std::size_t    m_cellEditorCol    = 0;
    std::u16string m_cellEditorOriginalText;
    // Tracks WM_IME_STARTCOMPOSITION/WM_IME_ENDCOMPOSITION on whichever
    // subclassed edit control (filter edit or, since WI-16f, the cell
    // editor) currently has focus, so Enter/Escape are left to the IME
    // (confirm/cancel the composition) instead of being intercepted while
    // converting Japanese/Chinese/Korean input - same guard ui::FindBar::
    // m_composing provides. One shared flag is safe across both edit
    // controls: Win32 IME composition is tied to whichever HWND currently
    // owns input focus, and losing focus force-ends composition before
    // another control can gain it, so the two edits can never be
    // mid-composition simultaneously.
    bool m_composing = false;
};

}  // namespace neomifes::ui
