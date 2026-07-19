#pragma once

// FindBar - the Find bar's WC_EDIT child controls (Phase 5b3a, this
// project's first child HWND). Win32-mechanics-only, same separation as
// MainWindow: FindBar knows nothing about neomifes::search/document/core -
// it deals only in u16string queries and plain booleans, exposing
// FindBarConfig callbacks the app layer (src/app/main.cpp) wires to actually
// run a search and update SelectionModel/RenderPipeline. This mirrors
// MainWindowConfig's existing hook pattern (main_window.h) rather than
// following docs/design/master_roadmap.md sec.5.3's FindBarState sketch,
// which put search state directly inside the Find bar's own struct - see
// docs/history/TIMELINE.md's Phase 5b3a entry for why that would have
// coupled this UI-mechanics class to neomifes::search.

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

struct FindBarConfig {
    // Fired after a 150ms debounce (FindBar's own UI-timing concern) once
    // the query text or a Case/Word/Regex toggle changes. The caller runs
    // the actual search and reports results back via setMatchCount().
    std::function<void(std::u16string_view query, bool caseSensitive, bool wholeWord, bool regex)>
        onQueryChanged;
    // Enter or F3 while the find edit has focus (not fired while an IME
    // composition is in progress - see FindBar's class comment above).
    std::function<void()> onFindNext;
    // Shift+Enter or Shift+F3 while the find edit has focus.
    std::function<void()> onFindPrevious;
    // Escape while the find edit has focus. The caller is responsible for
    // restoring focus to the document editing area - FindBar does not know
    // where that is.
    std::function<void()> onClosed;
    // Enter while the replace edit has focus (Phase 5b3b) - replaces just
    // the current match. Passes the replace edit's current text verbatim
    // (a raw template, e.g. containing "$1") - same "push the current text"
    // shape as onQueryChanged, not a separate pull-style getter.
    std::function<void(std::u16string_view replacementText)> onReplaceCurrent;
    // Ctrl+Enter while the replace edit has focus - replaces every match.
    std::function<void(std::u16string_view replacementText)> onReplaceAll;
};

class FindBar {
public:
    FindBar()  = default;
    ~FindBar() = default;

    // Not movable/copyable: SetWindowSubclass's dwRefData stores a raw
    // `this` for the lifetime of the subclassed HWND - moving this object
    // would leave that pointer dangling/stale.
    FindBar(const FindBar&)            = delete;
    FindBar& operator=(const FindBar&) = delete;
    FindBar(FindBar&&)                 = delete;
    FindBar& operator=(FindBar&&)      = delete;

    // Creates the (initially hidden) child HWNDs as children of `parent`.
    // Must be called once, after `parent` exists.
    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const FindBarConfig& config);

    // Reveals the find edit + info label, focuses the find edit, and
    // selects any existing query text (standard Ctrl+F convention -
    // re-pressing Ctrl+F while already focused re-selects rather than doing
    // nothing).
    void show() noexcept;
    // Reveals everything show() does, plus the replace edit (Ctrl+H
    // convention - Phase 5b3b). Still focuses the find edit, matching
    // VSCode: the user Tabs to the replace edit rather than starting there.
    void showWithReplace() noexcept;
    // Hides every child HWND, including the replace edit if visible, and
    // resets replace-mode off - the next show()/showWithReplace() call
    // decides fresh rather than a hidden bar "remembering" replace-mode.
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    // Updates the "N/M" (1-based) label. count==0 shows a distinct
    // no-matches state instead of "1/0".
    void setMatchCount(std::size_t currentIndex, std::size_t count) noexcept;

    // Repositions the child HWNDs against the parent's current client width
    // and re-selects a DPI-scaled font. Call from MainWindow's onResize hook
    // (including the resize WM_DPICHANGED triggers) and once after create().
    void onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept;

    // Routes a WM_COMMAND the owning MainWindow received (EN_CHANGE
    // notifications from the find edit arrive here, not at the child HWND
    // itself - Win32 always directs child-control notifications to the
    // parent). Call from MainWindowConfig::onCommand.
    void handleCommand(WPARAM wParam, LPARAM lParam) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR subclassId, DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    // `hwnd` distinguishes which of the two edit controls fired the key -
    // both are subclassed through the same subclassProc/dwRefData=this
    // (Phase 5b3b), so Enter/Ctrl+Enter mean "find next"/n-a in the find
    // edit but "replace current"/"replace all" in the replace edit. Returns
    // true if the key was one FindBar handles (caller should consume the
    // message, i.e. return 0 rather than falling through to
    // DefSubclassProc) - false lets ordinary typing/navigation keys reach
    // the stock edit control unchanged.
    [[nodiscard]] bool handleSubclassKeyDown(HWND hwnd, UINT vkCode) noexcept;
    // Enter/Ctrl+Enter on the replace edit - split out of
    // handleSubclassKeyDown()'s VK_RETURN case to stay under clang-tidy's
    // cognitive-complexity threshold.
    void handleReplaceReturn(HWND hwnd, bool ctrlDown) noexcept;
    // Alt+C/W/R arrive as WM_SYSKEYDOWN (Alt is a "system key" modifier),
    // never WM_KEYDOWN - see the .cpp for why this needs its own handler.
    void handleSubclassSysKeyDown(UINT vkCode) noexcept;
    // Fired directly on a toggle keypress (no debounce - a single discrete
    // event, not a burst of typing) and from the debounce WM_TIMER once it
    // fires (which it kills first, so it only ever fires once per burst).
    void fireQueryChanged() noexcept;
    // Moves focus between the find/replace edits (Phase 5b3b) - only
    // meaningful while the replace row is visible; a no-op (falls through
    // to DefSubclassProc, ordinary Tab navigation) otherwise, since there is
    // nothing to cycle to.
    void cycleFocus(HWND hwnd) noexcept;
    void ensureFont(float dpiScale) noexcept;
    // Reads `hwnd`'s current text (WM_GETTEXT) as a u16string - shared by
    // fireQueryChanged() (find edit) and handleReplaceReturn() (replace
    // edit), which need the identical GetWindowTextLengthW/GetWindowTextW/
    // fromWstringView() sequence. static: touches no instance state.
    [[nodiscard]] static std::u16string readEditText(HWND hwnd);

    neomifes::platform::WindowHandle    m_hwndFindEdit;
    neomifes::platform::WindowHandle    m_hwndReplaceEdit;
    neomifes::platform::WindowHandle    m_hwndInfoLabel;
    neomifes::platform::GdiObjectHandle m_font;
    // Tracks WM_IME_STARTCOMPOSITION/WM_IME_ENDCOMPOSITION so Enter/Escape/F3
    // are left to the IME (confirm/cancel the composition) instead of being
    // intercepted as Find bar shortcuts while converting Japanese/Chinese/
    // Korean input - see the class comment.
    bool m_composing     = false;
    bool m_caseSensitive = false;
    bool m_wholeWord     = false;
    bool m_regex         = false;
    bool m_replaceVisible = false;
    FindBarConfig m_config;
};

}  // namespace neomifes::ui
