#pragma once

// GrepBar - the Grep results pane's WC_EDIT x2 + WC_LISTBOX child controls
// (Phase 5c3, Ctrl+Shift+F). Win32-mechanics-only, same separation as
// FindBar/CommandPalette: GrepBar knows nothing about neomifes::search/
// document/core - it deals only in u16string text (queries, folder paths,
// pre-formatted result rows) and opaque std::function callbacks, exposing
// GrepBarConfig the app layer (src/app/main.cpp) wires to actually run
// search::GrepService::findAll() and call neomifes::app::openDocumentAt().
//
// Structurally a merge of its two precedents: two WC_EDIT controls sharing
// one SetWindowSubclass callback/dwRefData (FindBar's find/replace edits),
// plus a WC_LISTBOX subclassed the same way CommandPalette's is (a standard
// listbox SetFocus()s itself inside its own default WM_LBUTTONDOWN handling,
// which must be reclaimed for the query edit afterward - see
// CommandPalette's class comment for the double-click subtlety this
// requires). All three controls share the SAME subclass callback/dwRefData,
// distinguished by the hwnd each message carries.
//
// Deliberately does NOT reuse FindBar's debounce timer or CommandPalette's
// live-refresh-on-EN_CHANGE: Ctrl+Shift+F's search is a synchronous
// directory walk (search::GrepService::findAll(), no async infrastructure
// exists anywhere in this codebase), so re-running it on every keystroke
// risked freezing the UI on a large folder - confirmed with the user, who
// chose explicit Enter-to-run over live incremental search. There is
// consequently no WM_TIMER/EN_CHANGE handling and no Alt+C/W/R toggle UI in
// this class at all.

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

struct GrepBarConfig {
    // Enter in either edit (query or folder). The caller runs
    // search::GrepService::findAll() synchronously and reports rows back via
    // setResults(). Never fired while an IME composition is in progress.
    std::function<void(std::u16string_view queryText, std::u16string_view folderText)> onRunQuery;
    // Double-click on a result row (single click only selects it - see
    // setResults()'s comment). `resultIndex` indexes the vector most
    // recently passed to setResults(); GrepBar never sees the underlying
    // search::GrepMatch data itself, so the caller must keep its own
    // parallel copy and resolve resultIndex against it.
    std::function<void(std::size_t resultIndex)> onResultActivated;
    // Escape while either edit has focus. The caller is responsible for
    // restoring focus to the document editing area - GrepBar does not know
    // where that is (same contract as FindBarConfig::onClosed).
    std::function<void()> onClosed;
    // Ctrl+Up while the QUERY edit has focus (Phase 5c5) - never fires for
    // the folder edit (a directory path isn't a "search pattern" history
    // concept). `currentText` is the query edit's text at the moment of the
    // keypress; the caller looks up core::SearchHistory::older(currentText)
    // and, if it returns a value, calls GrepBar::setQueryText() with it.
    // Never fired while an IME composition is in progress.
    std::function<void(std::u16string_view currentText)> onHistoryOlder;
    // Ctrl+Down - symmetric to onHistoryOlder (core::SearchHistory::newer()).
    std::function<void(std::u16string_view currentText)> onHistoryNewer;
};

class GrepBar {
public:
    GrepBar()  = default;
    ~GrepBar() = default;

    // Not movable/copyable: SetWindowSubclass's dwRefData stores a raw
    // `this` for the lifetime of the subclassed HWNDs - moving this object
    // would leave that pointer dangling/stale.
    GrepBar(const GrepBar&)            = delete;
    GrepBar& operator=(const GrepBar&) = delete;
    GrepBar(GrepBar&&)                 = delete;
    GrepBar& operator=(GrepBar&&)      = delete;

    // Creates the (initially hidden) child HWNDs as children of `parent`.
    // Must be called once, after `parent` exists.
    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const GrepBarConfig& config);

    // Reveals all three controls and focuses the query edit (select-all,
    // same "re-press reselects" convention as FindBar::show()). Does NOT
    // clear the folder/query text or the existing results list - re-running
    // a Grep against the same folder with a tweaked query is the expected
    // common case (FindBar's "preserve query" convention, not
    // CommandPalette's "always reset" one - see this header's rationale).
    void show() noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    // Rebuilds the listbox from pre-formatted rows (LB_RESETCONTENT +
    // LB_ADDSTRING, one row per string, no owner-draw). Resets the selection
    // to row 0 if non-empty. GrepBar has no knowledge of what a row
    // represents - see grep_result_formatting.h (src/app/) for the
    // search::GrepMatch -> row-text formatting this codebase uses. Takes a
    // const reference (not by value like CommandPalette::create()'s
    // `commands`) since GrepBar only reads rows to populate the listbox and
    // never stores the vector itself.
    void setResults(const std::vector<std::u16string>& rows) noexcept;

    // Programmatically replaces the QUERY edit's text (Phase 5c5, Ctrl+Up/
    // Down history recall) and moves the caret to the end - not the folder
    // edit, which has no history concept (see GrepBarConfig::onHistoryOlder).
    // Unlike FindBar::setQueryText(), this does NOT trigger a re-run (Grep
    // stays Enter-only, the existing Phase 5c3 design - see this header's
    // file comment on why there is no debounce/live-refresh here).
    void setQueryText(std::u16string_view text) noexcept;

    // Repositions the child HWNDs against the parent's current client width
    // and re-selects a DPI-scaled font. Call from MainWindow's onResize hook
    // and once after create().
    void onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept;

    // Routes a WM_COMMAND the owning MainWindow received (EN_CHANGE is
    // intentionally not handled here - see this header's rationale;
    // LBN_SELCHANGE/LBN_DBLCLK from the listbox arrive here). Call from
    // MainWindowConfig::onCommand.
    void handleCommand(WPARAM wParam, LPARAM lParam) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR subclassId, DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    LRESULT handleListSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    // `hwnd` distinguishes which of the two edits fired the key - both
    // trigger the identical action (fireRunQuery()/moveSelection()), unlike
    // FindBar's find/replace edits, so no hwnd-based branching is needed
    // beyond Tab-cycling and reading both edits' text on Enter.
    [[nodiscard]] bool handleEditKeyDown(HWND hwnd, UINT vkCode) noexcept;
    // Moves focus between the query and folder edits - unconditional
    // two-way toggle (unlike FindBar::cycleFocus(), which is gated on
    // m_replaceVisible; both of GrepBar's edits are always shown together).
    void cycleFocus(HWND hwnd) noexcept;
    void moveSelection(int delta) noexcept;
    // Reads both edits and invokes onRunQuery - shared by Enter in either
    // edit (see this header's rationale for why there is no debounce here).
    void fireRunQuery() noexcept;
    // Invokes onResultActivated(m_selectedIndex) then hide() - mirrors
    // CommandPalette::runSelectedCommand()'s "execute then close" shape.
    void activateSelectedResult() noexcept;
    void ensureFont(float dpiScale) noexcept;
    [[nodiscard]] static std::u16string readEditText(HWND hwnd);

    neomifes::platform::WindowHandle    m_hwndQueryEdit;
    neomifes::platform::WindowHandle    m_hwndFolderEdit;
    neomifes::platform::WindowHandle    m_hwndList;
    neomifes::platform::GdiObjectHandle m_font;
    // Tracks WM_IME_STARTCOMPOSITION/WM_IME_ENDCOMPOSITION so Enter/Up/Down/
    // Escape are left to the IME while converting Japanese/Chinese/Korean
    // input - same "CJK IME一級市民" guard as FindBar/CommandPalette.
    bool        m_composing     = false;
    // Number of rows currently in the listbox (setResults()'s row count) -
    // needed by moveSelection()'s clamp without round-tripping through
    // LB_GETCOUNT on every keystroke.
    std::size_t m_resultCount   = 0;
    std::size_t m_selectedIndex = 0;
    GrepBarConfig m_config;
};

}  // namespace neomifes::ui
