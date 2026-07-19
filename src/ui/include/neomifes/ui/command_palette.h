#pragma once

// CommandPalette - the command palette's WC_EDIT + WC_LISTBOX child
// controls (Phase 5b3c). Modeled directly on ui::FindBar (find_bar.h):
// Win32-mechanics-only, knows nothing about neomifes::core/document/search -
// it deals only in a caller-supplied CommandDescriptor registry (each
// entry's `action` is an opaque std::function<void()> the app layer
// supplies, same relationship FindBarConfig's callbacks have to their
// domain logic) plus command_palette_filter.h's pure ranking function.
//
// Unlike FindBar's two WC_EDIT controls sharing one subclass, this class
// subclasses two DIFFERENT control types (an edit and a listbox) through
// the same SetWindowSubclass callback/dwRefData, distinguished by the HWND
// each message carries. The listbox needs its own subclass for a reason
// FindBar's controls didn't: a standard WC_LISTBOX calls SetFocus on itself
// inside its own default WM_LBUTTONDOWN handling, which would otherwise
// steal focus away from the query edit the instant a result row is
// clicked - after which Up/Down/Enter/Escape would reach the listbox's own
// unsubclassed DefWindowProc (this app's message loop has no
// IsDialogMessageW to give those any dialog-navigation meaning) and simply
// do nothing. The listbox subclass lets the default click-to-select
// handling run via DefSubclassProc, then immediately reclaims focus for
// the edit - see command_palette.cpp for the double-click subtlety this
// requires (a command's action may have already hidden the palette and
// moved focus elsewhere by the time that reclaim would run).

#include <windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "neomifes/platform/handle_guard.h"
#include "neomifes/ui/command_descriptor.h"

namespace neomifes::ui {

struct CommandPaletteConfig {
    // Escape while the query edit has focus. The caller is responsible for
    // restoring focus to the document editing area - CommandPalette does
    // not know where that is (same contract as FindBarConfig::onClosed).
    std::function<void()> onClosed;
};

class CommandPalette {
public:
    CommandPalette()  = default;
    ~CommandPalette() = default;

    CommandPalette(const CommandPalette&)            = delete;
    CommandPalette& operator=(const CommandPalette&) = delete;
    CommandPalette(CommandPalette&&)                 = delete;
    CommandPalette& operator=(CommandPalette&&)      = delete;

    // `commands` is the full static registry, supplied once. Unlike
    // FindBarConfig there is no per-keystroke callback out to the app layer -
    // filtering is entirely internal (command_palette_filter.h), since the
    // candidate list is small enough to re-rank synchronously on every
    // keystroke (no debounce needed, unlike FindBar's document search).
    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const CommandPaletteConfig& config,
                              std::vector<CommandDescriptor> commands);

    // Clears the query, shows every command unfiltered, selects the first,
    // focuses the query edit. Re-invoking while already open (Ctrl+Shift+P
    // pressed twice) resets to this same known state - same "always land
    // somewhere well-defined" convention as FindBar::show()'s select-all.
    void show() noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    void onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept;
    // Routes a WM_COMMAND the owning MainWindow received (EN_CHANGE from
    // the edit, LBN_SELCHANGE/LBN_DBLCLK from the listbox all arrive here,
    // not at the child itself - same Win32 routing FindBar::handleCommand()
    // relies on). Call from MainWindowConfig::onCommand.
    void handleCommand(WPARAM wParam, LPARAM lParam) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR subclassId, DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    [[nodiscard]] bool handleEditKeyDown(UINT vkCode) noexcept;
    LRESULT handleListSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    void moveSelection(int delta) noexcept;
    void refreshFilter() noexcept;
    // Rebuilds the listbox rows from m_filtered ("Title\tKeybinding", using
    // LBS_USETABSTOPS' default tab stop rather than owner-draw columns -
    // deliberately simple, see the plan's scope notes).
    void populateListBox() noexcept;
    void runSelectedCommand() noexcept;
    void ensureFont(float dpiScale) noexcept;
    [[nodiscard]] static std::u16string readEditText(HWND hwnd);

    neomifes::platform::WindowHandle    m_hwndEdit;
    neomifes::platform::WindowHandle    m_hwndList;
    neomifes::platform::GdiObjectHandle m_font;
    bool m_composing = false;
    std::vector<CommandDescriptor> m_commands;
    // Indices into m_commands, in ranked/filtered order - what the listbox
    // currently displays.
    std::vector<std::size_t> m_filtered;
    // Index into m_filtered (not m_commands) of the currently-highlighted
    // row.
    std::size_t          m_selectedIndex = 0;
    CommandPaletteConfig m_config;
};

}  // namespace neomifes::ui
