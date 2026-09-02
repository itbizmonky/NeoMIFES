#pragma once

// FindReplaceDialog - standalone floating Find/Replace window (WI-18b).
//
// Unlike FindBar/GrepBar/GotoLineBar/CommandPalette (all WS_CHILD strips
// docked to an edge of the main window, notifying the app layer through
// MainWindowConfig::onCommand), this is a genuine independent top-level
// WS_POPUP window - the user explicitly asked for a real Win32 dialog
// (秀丸/MIFES-style Find/Replace) rather than the VSCode-style embedded bar
// every other overlay in this codebase uses. It owns its OWN window class +
// WndProc (same registration/creation shape as ui::MainWindow, this
// codebase's only other top-level window), rather than piggybacking on
// MainWindow's message handling the way FindBar does.
//
// "Owned" by the main window (CreateWindowExW's hWndParent, not WS_CHILD -
// the standard Win32 mechanism for a modeless dialog: stays above its owner
// in z-order, minimizes/restores with it, and never gets its own taskbar
// entry) rather than truly parented - this window positions itself
// independently (centered over the main window on first show) instead of
// being docked at a fixed edge offset the way the bars are.
//
// Win32-mechanics-only, same "knows nothing about neomifes::search/
// document/core" separation FindBar already documents - it deals only in
// u16string queries/replacement text and plain booleans, exposing
// FindReplaceDialogConfig callbacks the app layer wires to actually run a
// search/replace and update SelectionModel/RenderPipeline. The callback
// shape deliberately mirrors FindBarConfig's onQueryChanged/onFindNext/
// onFindPrevious/onReplaceCurrent/onReplaceAll/onClosed so the app layer's
// existing buildFindBarConfig()-style wiring logic can be reused almost
// verbatim for this dialog instead of duplicating it.

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

struct FindReplaceDialogConfig {
    // Fired after a 150ms debounce (same FindBar-established UI-timing
    // convention) once the query text or a Case/Word/Regex checkbox
    // changes. The caller runs the actual search and reports results back
    // via setMatchCount().
    std::function<void(std::u16string_view query, bool caseSensitive, bool wholeWord, bool regex)>
        onQueryChanged;
    // Enter/Find Next button while the find edit has focus, or the Find
    // Next button itself regardless of focus.
    std::function<void()> onFindNext;
    // Shift+Enter, or the Find Previous button.
    std::function<void()> onFindPrevious;
    // Escape, the title bar close button, or Alt+F4 - the caller is
    // responsible for restoring focus to the document editing area (this
    // class does not know where that is, same contract as FindBar::
    // FindBarConfig::onClosed).
    std::function<void()> onClosed;
    // Enter while the replace edit has focus, or the Replace button -
    // replaces just the current match. Passes the replace edit's current
    // text verbatim (a raw template, e.g. containing "$1").
    std::function<void(std::u16string_view replacementText)> onReplaceCurrent;
    // Ctrl+Enter while the replace edit has focus, or the Replace All
    // button - replaces every match.
    std::function<void(std::u16string_view replacementText)> onReplaceAll;
};

class FindReplaceDialog {
public:
    FindReplaceDialog()  = default;
    ~FindReplaceDialog();

    // Not movable/copyable: the window class's GWLP_USERDATA and both edit
    // controls' SetWindowSubclass dwRefData store a raw `this` for the
    // lifetime of the window - moving this object would leave them dangling
    // (same reasoning as FindBar's own deleted copy/move).
    FindReplaceDialog(const FindReplaceDialog&)            = delete;
    FindReplaceDialog& operator=(const FindReplaceDialog&) = delete;
    FindReplaceDialog(FindReplaceDialog&&)                 = delete;
    FindReplaceDialog& operator=(FindReplaceDialog&&)      = delete;

    // Registers this window's class (idempotent) and creates the (initially
    // hidden) top-level window owned by `owner`, plus every child control.
    // Must be called once, after `owner` exists.
    [[nodiscard]] bool create(HWND owner, HINSTANCE hInstance, const FindReplaceDialogConfig& config);

    // Centers over `owner`'s current window rect the FIRST time this is
    // called (a fresh FindReplaceDialog has no prior position to restore);
    // subsequent calls just show/focus/select-all at wherever the user last
    // left it, matching how a real modeless dialog behaves. Focuses the
    // find edit and selects any existing query text (same "re-invoking
    // re-selects" convention as FindBar::show()).
    void show(HWND owner) noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    // Updates the "N/M" (1-based) label. count==0 shows a distinct
    // no-matches state instead of "1/0" - same formatMatchCountLabel()
    // FindBar itself already uses (find_navigation.h).
    void setMatchCount(std::size_t currentIndex, std::size_t count) noexcept;

    // Win32 WNDPROC entry point. Public because it is registered as the
    // window class's lpfnWndProc from a free helper in
    // find_replace_dialog.cpp - same reasoning as ui::MainWindow::
    // wndProcTrampoline's identical doc comment. Do not call it from
    // application code.
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
    // handleSubclassKeyDown().
    [[nodiscard]] bool handleEditKeyDown(HWND hwnd, UINT vkCode) noexcept;
    void handleReplaceReturn(bool ctrlDown) noexcept;
    void handleCommand(WPARAM wParam) noexcept;
    void fireQueryChanged() noexcept;
    void requestClose() noexcept;
    [[nodiscard]] static std::u16string readEditText(HWND hwnd);

    neomifes::platform::WindowHandle    m_hwnd;
    neomifes::platform::WindowHandle    m_hwndFindEdit;
    neomifes::platform::WindowHandle    m_hwndReplaceEdit;
    neomifes::platform::WindowHandle    m_hwndCaseCheck;
    neomifes::platform::WindowHandle    m_hwndWordCheck;
    neomifes::platform::WindowHandle    m_hwndRegexCheck;
    neomifes::platform::WindowHandle    m_hwndInfoLabel;
    neomifes::platform::WindowHandle    m_hwndFindNextButton;
    neomifes::platform::WindowHandle    m_hwndFindPrevButton;
    neomifes::platform::WindowHandle    m_hwndReplaceButton;
    neomifes::platform::WindowHandle    m_hwndReplaceAllButton;
    neomifes::platform::GdiObjectHandle m_font;
    // Tracks WM_IME_STARTCOMPOSITION/WM_IME_ENDCOMPOSITION so Enter/Escape
    // are left to the IME instead of being intercepted as dialog shortcuts
    // while converting Japanese/Chinese/Korean input - same rationale as
    // FindBar's own m_composing.
    bool m_composing        = false;
    bool m_positionedOnce   = false;
    FindReplaceDialogConfig m_config;
};

}  // namespace neomifes::ui
