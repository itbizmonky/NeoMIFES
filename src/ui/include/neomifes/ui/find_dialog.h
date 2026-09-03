#pragma once

// FindDialog - standalone floating Find window (WI-24), the Ctrl+F
// counterpart to ui::FindReplaceDialog (Ctrl+H, WI-18b). Forked from
// FindReplaceDialog's skeleton with the replace row/edit/2 buttons removed
// - same genuine independent top-level WS_POPUP window (own window class +
// WndProc, owned not parented by MainWindow), replacing FindBar's
// WS_CHILD-docked-to-MainWindow embedded bar. The user explicitly asked for
// a real Win32 search dialog (秀丸-style) rather than the VSCode-style
// embedded bar every OTHER overlay in this codebase (GrepBar/GotoLineBar/
// CommandPalette) still uses - see docs/history/TIMELINE.md's WI-24 entry
// for why FindBar itself, rather than FindReplaceDialog, was chosen as the
// one to also convert.
//
// Unlike FindReplaceDialog, this class also carries 3 callbacks
// FindBarConfig used to own (onReplaceRequested/onHistoryOlder/
// onHistoryNewer) - ported over rather than dropped, since this dialog is
// FindBar's direct replacement and losing them would be a real feature
// regression, not just a UI-shape change. See each field's own comment
// below for why.
//
// Win32-mechanics-only, same "knows nothing about neomifes::search/
// document/core" separation FindReplaceDialog already documents - it
// deals only in u16string queries and plain booleans, exposing
// FindDialogConfig callbacks the app layer wires to actually run a search
// and update SelectionModel/RenderPipeline.

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

struct FindDialogConfig {
    // Fired after a 150ms debounce (same FindReplaceDialog UI-timing
    // convention) once the query text or a Case/Word/Regex checkbox
    // changes. The caller runs the actual search and reports results back
    // via setMatchCount().
    std::function<void(std::u16string_view query, bool caseSensitive, bool wholeWord, bool regex)>
        onQueryChanged;
    // Enter or F3 while the find edit has focus, or the Find Next button
    // itself regardless of focus.
    std::function<void()> onFindNext;
    // Shift+Enter, Shift+F3, or the Find Previous button.
    std::function<void()> onFindPrevious;
    // Escape, the title bar close button, or Alt+F4 - the caller is
    // responsible for restoring focus to the document editing area (this
    // class does not know where that is, same contract as
    // FindReplaceDialog's onClosed).
    std::function<void()> onClosed;
    // Ctrl+H while the find edit has focus. Ctrl+H normally reaches
    // MainWindow's own onKeyDown hook and opens ui::FindReplaceDialog from
    // there, but that hook never fires while keyboard focus is on this
    // dialog's own edit (Win32 routes WM_KEYDOWN to whichever HWND actually
    // has focus, and this dialog is a separate top-level window, not a
    // child of MainWindow) - this callback exists purely so Ctrl+H still
    // works while the user is mid-search here. Ported verbatim from
    // FindBarConfig::onReplaceRequested (same rationale, same decoupling -
    // this class deliberately does not know FindReplaceDialog exists).
    std::function<void()> onReplaceRequested;
    // Ctrl+Up while the find edit has focus. `currentText` is the find
    // edit's text at the moment of the keypress; the caller looks up
    // core::SearchHistory::older(currentText) and, if it returns a value,
    // calls FindDialog::setQueryText() with it. Never fired while an IME
    // composition is in progress. Ported from FindBarConfig::onHistoryOlder
    // - FindReplaceDialog never grew an equivalent, but FindBar's Ctrl+F
    // users should not lose this on migration to a dialog.
    std::function<void(std::u16string_view currentText)> onHistoryOlder;
    // Ctrl+Down - symmetric to onHistoryOlder (core::SearchHistory::newer()).
    std::function<void(std::u16string_view currentText)> onHistoryNewer;
};

class FindDialog {
public:
    FindDialog()  = default;
    ~FindDialog();

    // Not movable/copyable: the window class's GWLP_USERDATA and the find
    // edit's SetWindowSubclass dwRefData store a raw `this` for the
    // lifetime of the window - moving this object would leave them dangling
    // (same reasoning as FindReplaceDialog's own deleted copy/move).
    FindDialog(const FindDialog&)            = delete;
    FindDialog& operator=(const FindDialog&) = delete;
    FindDialog(FindDialog&&)                 = delete;
    FindDialog& operator=(FindDialog&&)      = delete;

    // Registers this window's class (idempotent) and creates the (initially
    // hidden) top-level window owned by `owner`, plus every child control.
    // Must be called once, after `owner` exists.
    [[nodiscard]] bool create(HWND owner, HINSTANCE hInstance, const FindDialogConfig& config);

    // Centers over `owner`'s current window rect the FIRST time this is
    // called (a fresh FindDialog has no prior position to restore);
    // subsequent calls just show/focus/select-all at wherever the user last
    // left it, matching how a real modeless dialog behaves (same convention
    // as FindReplaceDialog::show()). Focuses the find edit and selects any
    // existing query text (same "re-invoking re-selects" convention as
    // FindReplaceDialog::show()).
    void show(HWND owner) noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    // Updates the "N/M" (1-based) label. count==0 shows a distinct
    // no-matches state instead of "1/0" - same formatMatchCountLabel()
    // FindReplaceDialog already uses (find_navigation.h).
    void setMatchCount(std::size_t currentIndex, std::size_t count) noexcept;

    // Programmatically replaces the find edit's text (Ctrl+Up/Down history
    // recall) and moves the caret to the end. Setting the text triggers
    // EN_CHANGE -> the existing debounce timer -> onQueryChanged as usual
    // (no special-casing needed): recalling a history entry is expected to
    // actually re-run the search, matching what typing the same text would
    // do. Ported verbatim from FindBar::setQueryText().
    void setQueryText(std::u16string_view text) noexcept;

    // Win32 WNDPROC entry point. Public because it is registered as the
    // window class's lpfnWndProc from a free helper in find_dialog.cpp -
    // same reasoning as ui::MainWindow::wndProcTrampoline's identical doc
    // comment. Do not call it from application code.
    static LRESULT CALLBACK wndProcTrampoline(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

private:
    // Takes `hwnd` explicitly (unlike ui::MainWindow::wndProc(), which reads
    // its own already-set m_hwnd member instead) - m_hwnd here is an
    // owning WindowHandle, not a plain HWND, and isn't actually assigned
    // until create()'s ::CreateWindowExW() call returns; messages that
    // arrive DURING that call (WM_NCCREATE/WM_CREATE) would otherwise have
    // no valid hwnd to fall back to for DefWindowProcW.
    LRESULT wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;

    static LRESULT CALLBACK editSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR subclassId, DWORD_PTR refData) noexcept;
    LRESULT handleEditSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    // Returns true if the key was one this class handles (caller should
    // consume the message) - false lets ordinary typing/navigation keys
    // reach the stock edit control unchanged. Same role as FindBar::
    // handleSubclassKeyDown()/FindReplaceDialog::handleEditKeyDown().
    [[nodiscard]] bool handleEditKeyDown(HWND hwnd, UINT vkCode) noexcept;
    void handleCommand(WPARAM wParam) noexcept;
    void fireQueryChanged() noexcept;
    void requestClose() noexcept;
    [[nodiscard]] static std::u16string readEditText(HWND hwnd);

    neomifes::platform::WindowHandle    m_hwnd;
    neomifes::platform::WindowHandle    m_hwndFindEdit;
    neomifes::platform::WindowHandle    m_hwndCaseCheck;
    neomifes::platform::WindowHandle    m_hwndWordCheck;
    neomifes::platform::WindowHandle    m_hwndRegexCheck;
    neomifes::platform::WindowHandle    m_hwndInfoLabel;
    neomifes::platform::WindowHandle    m_hwndFindNextButton;
    neomifes::platform::WindowHandle    m_hwndFindPrevButton;
    neomifes::platform::GdiObjectHandle m_font;
    // Tracks WM_IME_STARTCOMPOSITION/WM_IME_ENDCOMPOSITION so Enter/Escape
    // are left to the IME instead of being intercepted as dialog shortcuts
    // while converting Japanese/Chinese/Korean input - same rationale as
    // FindReplaceDialog's own m_composing.
    bool m_composing      = false;
    bool m_positionedOnce = false;
    FindDialogConfig m_config;
};

}  // namespace neomifes::ui
