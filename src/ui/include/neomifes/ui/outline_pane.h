#pragma once

// OutlinePane - the symbol outline panel's WC_TREEVIEW child control
// (Phase 7g). Modeled on ui::CommandPalette (command_palette.h): Win32-
// mechanics-only, knows nothing about neomifes::syntax - it deals only in a
// caller-supplied OutlineItem tree (each item's targetPos is an opaque
// std::uint64_t this class never interprets, only echoes back via
// onItemSelected, the same relationship GrepBarConfig::onResultActivated has
// to its resultIndex).
//
// Unlike every earlier child control this codebase has used (WC_EDIT/
// WC_LISTBOX, both of which notify the parent via WM_COMMAND), WC_TREEVIEW
// notifies via WM_NOTIFY - a different message entirely. See
// MainWindowConfig::onNotify's doc comment (main_window.h) for why that
// required a new hook rather than reusing onCommand.
//
// Unlike FindBar/GrepBar/CommandPalette (all of which hide themselves after
// a single action - a search or a command is a one-shot tool), OutlinePane
// stays open after a jump: browsing an outline means visiting several
// symbols in a row, closer in spirit to BookmarkManager's persistent gutter
// than to a modal-style overlay. Only Escape closes it.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

// UI-only mirror of syntax::OutlineNode (Phase 7f) - see this header's top
// comment for why ui:: does not depend on neomifes::syntax directly.
struct OutlineItem {
    std::u16string            name;
    std::uint64_t             targetPos = 0;
    std::vector<OutlineItem>  children;
};

struct OutlinePaneConfig {
    // Fired when the user selects any tree item, leaf or container - a
    // container symbol's own name position is still a meaningful jump
    // target (e.g. landing on `class Widget {`).
    std::function<void(std::uint64_t targetPos)> onItemSelected;
    // Escape. Caller restores focus to the document (same contract as
    // FindBarConfig::onClosed / CommandPaletteConfig::onClosed).
    std::function<void()> onClosed;
};

class OutlinePane {
public:
    OutlinePane()  = default;
    ~OutlinePane() = default;

    OutlinePane(const OutlinePane&)            = delete;
    OutlinePane& operator=(const OutlinePane&) = delete;
    OutlinePane(OutlinePane&&)                 = delete;
    OutlinePane& operator=(OutlinePane&&)      = delete;

    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const OutlinePaneConfig& config);

    // Replaces the tree contents and shows the panel (docks full-height on
    // the right - see onParentResized). Re-invoking while already visible
    // refreshes in place; toggling visibility off is main.cpp's
    // handleOutlineKey() job, not this method's (mirrors how show()/hide()
    // are already split apart on every other overlay in this codebase).
    void showWith(std::vector<OutlineItem> items) noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    void onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight,
                         float dpiScale) noexcept;

    // WI-15i: the panel's own fixed DIP width - same "public static
    // constexpr accessor" shape as ui::TabBar::heightDips()/ui::StatusBar::
    // heightDips(), so normal_mode_wiring.cpp's syncRightPaneWidthDips() can
    // learn how much of RenderPipeline's own right edge to reserve without
    // this class exposing anything else about its own layout. Deliberately
    // duplicates outline_pane.cpp's own private kPanelWidthDips constant
    // (same value, 260.0F) rather than sharing storage across the header/.cpp
    // boundary - this codebase's established "small duplicated constant over
    // premature cross-TU sharing" precedent (see build_plan.md's Phase 7e
    // kTabWidth history).
    [[nodiscard]] static constexpr float widthDips() noexcept { return 260.0F; }

    // Routes a WM_NOTIFY the owning MainWindow received (TVN_SELCHANGEDW
    // arrives here, not at the child itself - same routing MainWindow
    // already uses for WM_COMMAND, see MainWindowConfig::onNotify's doc
    // comment). Call from MainWindowConfig::onNotify.
    LRESULT handleNotify(WPARAM wParam, LPARAM lParam) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR subclassId, DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    // Iterative (explicit-stack) rebuild of the tree control's items from
    // m_items - deliberately not recursive, even though m_items' own nesting
    // depth (symbol definitions only, already collapsed out of raw AST depth
    // by extractOutline()) would likely be safe either way. See the Phase 7g
    // plan's Context point 7: avoids repeating the misc-no-recursion finding
    // Phase 7f's walkForOutline() hit for a *different*, genuinely unbounded
    // tree shape - applying the same iterative idiom here preemptively.
    void populateTree() noexcept;
    void ensureFont(float dpiScale) noexcept;

    neomifes::platform::WindowHandle    m_hwndTree;
    neomifes::platform::GdiObjectHandle m_font;
    std::vector<OutlineItem>            m_items;
    OutlinePaneConfig                   m_config;
};

}  // namespace neomifes::ui
