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
// WI-16g: the "#" (1-based row number) column is frozen (roadmap §10.2's
// "列固定") - it stays on screen while the real CSV columns scroll
// horizontally. This is implemented as TWO synchronized sibling
// WC_LISTVIEW HWNDs, not one: m_hwndFrozenList (narrow, fixed-width,
// non-horizontally-scrolling, "#" column only) and m_hwndDataList (the
// scrollable one, real CSV columns only - m_hwndDataList's OWN column 0 IS
// the caller's CSV column 0, no internal shift). A single shared ListView
// with NM_CUSTOMDRAW could not do this: native horizontal scrolling moves
// every column's pixels, "frozen" or not, so keeping one column visually
// fixed still requires a second, independently-positioned window. Both
// lists stay LVS_OWNERDATA, backed by the same row data, and are kept
// vertically scroll- and selection-synchronized (see
// tryForwardListScrollMessage()/handleItemChanged()) so they read as ONE
// table to the user rather than two. Both list HWNDs remain visually
// contiguous - m_hwndListDivider (an opaque WC_STATIC, same "cover the
// gap" pattern m_hwndFilterBackdrop below already established) fills the
// seam between them so the Direct2D document view never shows through.
//
// The "#" column itself is never routed through onGetCellText()/
// onCellActivated() - the caller's csvmode::CsvModel-aware bridge code
// (csv_grid_bridge.h) never needs to know this column exists. Real CSV
// columns map 1:1 onto the caller's own CSV columns 0..columnCount-1 (no
// shift - see onGetCellText()'s own comment).

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
    // (never for the synthesized "#" column, which lives in the separate
    // m_hwndFrozenList and never reaches this callback - see this header's
    // own top comment) - `colIndex` is simply 0-based into the caller's OWN
    // CSV column space, with NO shift: it maps 1:1 onto m_hwndDataList's own
    // column index (WI-16g - before the "#"/data split, this pane's single
    // list needed a +1 shift here; now "#" is a different HWND entirely, so
    // there is nothing to shift past). `rowIndex` is the data-row index the
    // pane was given via showWith()'s dataRowCount, not a raw
    // document::Document line number.
    std::function<std::u16string(std::size_t rowIndex, std::size_t colIndex)> onGetCellText;
    // Double-click or Enter (LVN_ITEMACTIVATE). `colIndex` is THIS pane's own
    // internal column index (0 == the "#" row-number column was activated,
    // 1..columnCount == a real CSV column) - deliberately offset by +1 from
    // onGetCellText's own (unshifted) space, reserving 0 for "#", since "the
    // row-number column itself was activated" is meaningful information the
    // caller needs to distinguish (jump to the row's own first cell) from
    // "CSV column 0 was activated". WI-16g: this convention is unchanged by
    // the frozen/data list split - "#" activation now originates from the
    // separate m_hwndFrozenList HWND rather than column 0 of one shared
    // list, but the numeric value the caller sees is identical.
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
    // LVN_COLUMNCLICK. `colIndex` uses the SAME +1-offset space as
    // onCellActivated's (0 == the "#" row-number column's header was
    // clicked - the caller's own convention for "reset to unsorted", since
    // that column has no real CSV data to sort by; 1..columnCount == a real
    // CSV column). WI-16g: as with onCellActivated, this notification can
    // now originate from either m_hwndFrozenList's header (colIndex==0) or
    // m_hwndDataList's own (colIndex-1 == the caller's CSV column) - the
    // numeric contract itself is unchanged.
    std::function<void(std::size_t colIndex)> onSortColumnClicked;
    // WI-16f: fires when the cell-edit overlay (single click on a real CSV
    // cell, never the "#" column) commits - Enter, or the overlay losing
    // focus (spreadsheet-like UX; the pane cancels instead of committing on
    // Escape, see cancelCellEditor()). Only fires when `newText` differs
    // from what onGetCellText originally supplied for this cell - a no-op
    // edit (opened, nothing changed) never reaches the caller. `colIndex`
    // uses the SAME unshifted space onGetCellText's does (0 == the caller's
    // own first CSV column) - the "#" column can never open an editor
    // because it lives in a separate HWND (m_hwndFrozenList) entirely, see
    // handleClick()'s own guard.
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
    // manual LVM_SUBITEMHITTEST is needed. No-ops on the "#" column (WI-16g:
    // now identified by hdr.hwndFrom != m_hwndDataList, since "#" lives in a
    // separate HWND rather than being subitem 0 of one shared list) or when
    // m_config.canBeginCellEdit vetoes.
    void handleClick(LPARAM lParam) noexcept;
    // WI-16g: LVN_ITEMCHANGED from either list - mirrors a selection/focus
    // state change onto the SAME row index in the other list
    // (ListView_SetItemState()), guarded by m_syncingSelection against
    // re-entrant notification loops, so the two lists always show the same
    // row highlighted (read as one table, not two independent ones). Only
    // acts on LVIF_STATE changes to LVIS_SELECTED/LVIS_FOCUSED with a valid
    // iItem>=0 - both lists use LVS_SINGLESEL specifically so ordinary
    // selection changes always arrive this way rather than via the
    // range-oriented LVN_ODSTATECHANGED (verified via this WI's own
    // standalone probe, see this file's own header comment).
    void handleItemChanged(LPARAM lParam) noexcept;
    // WI-16g: intercepts WM_VSCROLL/WM_MOUSEWHEEL/list-navigation
    // WM_KEYDOWN (arrows/PageUp/PageDown/Home/End) on either list, lets
    // DefSubclassProc handle it first (so the source list's own scroll
    // position updates normally), then calls syncScrollAfterMessage() to
    // replay the resulting top-index change onto the other list. Returns
    // true (and sets `result`) only when it actually handled `msg` for
    // `hwnd` - handleSubclassMessage() checks this BEFORE its own switch so
    // nav keys never reach the Escape/Enter/IME handling below (which never
    // matches nav-key virtual-key codes anyway, but checking first keeps
    // the two concerns visibly separate).
    [[nodiscard]] bool tryForwardListScrollMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                                   LRESULT& result) noexcept;
    // WI-16g: reads `source`'s current ListView_GetTopIndex(), compares
    // against m_syncedTopIndex (the last value both lists agreed on), and -
    // if it changed - replays that exact row-index delta onto the OTHER
    // list via ListView_Scroll() (pixel delta = rows * rowHeightPx()).
    // Row-index-delta (not naively re-deriving a pixel delta from the
    // triggering message) is what stays correct across comctl32's own
    // internal clamping at the very top/bottom of a huge list - reading
    // back the ACTUAL resulting top index, verified via this WI's own
    // standalone probe to clamp cleanly rather than drift, means both lists
    // stay exactly in sync even at the boundaries.
    void syncScrollAfterMessage(HWND source) noexcept;
    // WI-16g: lazily measures and caches one row's pixel height via
    // ListView_GetItemRect(m_hwndDataList, 0, ..., LVIR_BOUNDS) - both
    // lists share one font (see ensureFont()) so their row heights match
    // automatically. Returns 0 (verified safe by syncScrollAfterMessage()'s
    // own guard) if there are no rows to measure yet.
    [[nodiscard]] int rowHeightPx() noexcept;
    // WI-16g: extracted from create() - builds m_hwndFrozenList/
    // m_hwndDataList/m_hwndListDivider, subclasses the two lists, and
    // applies the shared extended-style call to both (see
    // applyListExtendedStyles() in the .cpp - a free function specifically
    // so neither list can silently end up with a different style set than
    // the other, the exact WI-16f dogfooding lesson this split risks
    // repeating twice over).
    [[nodiscard]] bool createListViews(HWND parent, HINSTANCE hInstance) noexcept;
    // WI-16g: extracted from onParentResized() - positions the frozen list
    // (fixed kRowNumberColumnWidthDips width), the divider strip, and the
    // data list (the remaining width), given the vertical band
    // onParentResized()'s own filter-row math already computed.
    void positionListViews(int listTopPx, int listHeightPx, int widthPx, float dpiScale) noexcept;
    // Fires onFilterQueryChanged with the filter edit's current text - shared
    // by the debounce WM_TIMER and VK_RETURN's "fire now" path (mirrors
    // ui::FindBar::fireQueryChanged()).
    void fireFilterQueryChanged() noexcept;
    // WI-16f: positions+shows m_hwndCellEditor over the given cell's
    // on-screen rect (LVM_GETSUBITEMRECT, mapped from the data list's own
    // client coordinates into the shared parent's - m_hwndCellEditor is a
    // sibling of m_hwndDataList, not its child; the "#" column can never
    // open an editor because it lives in the separate m_hwndFrozenList, see
    // handleClick()'s own comment), pre-filled via m_config.onGetCellText
    // and remembered in m_cellEditorOriginalText for commitCellEditor()'s
    // later no-op-edit comparison.
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

    // WI-16g: the "#"-only frozen list and the real-CSV-columns scrollable
    // list - see this header's own top comment for why this is two HWNDs,
    // not one.
    neomifes::platform::WindowHandle    m_hwndFrozenList;
    neomifes::platform::WindowHandle    m_hwndDataList;
    // WI-16g: opaque WC_STATIC filling the seam between m_hwndFrozenList and
    // m_hwndDataList - same "cover the gap so the Direct2D document view
    // never shows through" pattern m_hwndFilterBackdrop below already
    // established (WI-16f), applied to a second seam this WI introduces.
    neomifes::platform::WindowHandle    m_hwndListDivider;
    // WI-16g: the last top-visible-row index BOTH lists agree on - a single
    // shared baseline (not one per list) because the two lists are defined
    // to always match after any sync event, regardless of which one the
    // user actually scrolled/navigated. Reset to 0 in showWith() (a column
    // rebuild always resets each ListView's own top index to 0 anyway).
    int m_syncedTopIndex = 0;
    // WI-16g: cached row height in pixels (see rowHeightPx()) - 0 means
    // "not yet measured". Reset to 0 whenever the font changes (ensureFont())
    // since a different font can change row height.
    int m_rowHeightPx = 0;
    // WI-16g: reentrancy guard for handleItemChanged()'s cross-list
    // ListView_SetItemState() mirroring - set while THIS class is itself
    // the one driving a state change on the "other" list, so that list's
    // own resulting LVN_ITEMCHANGED (if comctl32 sends one) is ignored
    // rather than bouncing back and forth.
    bool m_syncingSelection = false;
    // WI-16f bugfix: an opaque WC_STATIC spanning the ENTIRE filter row band
    // (behind m_hwndFilterLabel/m_hwndFilterEdit in z-order, created before
    // them so they paint on top of it), not just the sub-rects those two
    // controls themselves occupy. Without it, the few-DIP margins
    // onParentResized() leaves around them (deliberate - centers the
    // controls within the taller band, see kFilterRowHeightDips vs
    // kFilterControlHeightDips) were bare client area, and the Direct2D
    // document view painted underneath (MainWindow::setPaintHandler() paints
    // the whole client rect every WM_PAINT regardless of what native child
    // HWNDs sit on top of it) showed through as a persistent visual glitch -
    // a real user found this during WI-16f dogfooding, present since WI-16c
    // (2026-08-19). Skipping the Direct2D render while this pane is visible
    // (see normal_mode_wiring.cpp's handlePaintEvent()) only stops it from
    // getting worse; it does not erase what was already painted there before
    // the pane opened, so full opaque coverage is the actual fix.
    neomifes::platform::WindowHandle    m_hwndFilterBackdrop;
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
