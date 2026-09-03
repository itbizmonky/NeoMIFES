#pragma once

// TabBar - the tab strip's WC_TABCONTROL child control (WI-05). Modeled on
// ui::OutlinePane (outline_pane.h): Win32-mechanics-only, knows nothing
// about neomifes::app::Workspace/EditorSession - it deals only in a
// caller-supplied std::vector<TabBarItem> (each item's index into that
// vector is what onTabSelected echoes back, the same "opaque index" shape
// GrepBarConfig::onResultActivated/OutlinePaneConfig::onItemSelected use).
//
// Like OutlinePane (WC_TREEVIEW) and unlike FindDialog/GrepBar/CommandPalette
// (all WC_EDIT/WC_LISTBOX, which notify via WM_COMMAND), WC_TABCONTROL
// notifies via WM_NOTIFY - see MainWindowConfig::onNotify's doc comment
// (main_window.h). Unlike OutlinePane (toggleable, hidden by default,
// docked full-height on the right), TabBar is ALWAYS visible - docked
// full-width along the top edge, above where RenderPipeline itself starts
// drawing (see RenderPipeline::setTabBarHeightDips()).
//
// v1 scope cut (WI-05): no per-tab close button - WC_TABCONTROL has no
// built-in one (would need TCS_OWNERDRAWFIXED + WM_DRAWITEM custom
// painting), and build_plan.md's WI-05 DoD only requires Ctrl+W to close
// the active tab. No keyboard handling inside this control either (no
// SetWindowSubclass, unlike OutlinePane/FindDialog) - v1 has nothing for it to
// intercept (Escape-to-close etc. are not part of this DoD).

#include <windows.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "neomifes/platform/handle_guard.h"

namespace neomifes::ui {

// One tab's already-derived display data. `label` excludes the dirty
// marker - TabBar itself decides how that renders (a trailing glyph),
// keeping "how is a filename derived from EditorSession" (app-layer
// concern, see formatTabBaseLabel() below) separate from "how does dirty
// look" (ui-layer rendering concern), the same layering split OutlinePane
// draws between syntax::OutlineNode and its own OutlineItem mirror.
struct TabBarItem {
    std::wstring label;
    bool          isDirty = false;
};

struct TabBarConfig {
    // Fired when the user selects a different tab (TCN_SELCHANGE), with the
    // newly-active 0-based index into whatever vector was last passed to
    // setTabs().
    std::function<void(std::size_t index)> onTabSelected;
};

// Pure, dependency-free label formatting - header-only so it stays
// unit-testable without a live HWND (same rationale as
// ui::find_navigation.h's nextMatchIndex()/previousMatchIndex()). `filename`
// is the tab's file name only (not a full path - callers derive that via
// std::filesystem::path::filename() before calling this), nullopt for an
// untitled/unsaved document. `untitledOrdinal` is only used in the untitled
// case (1-based, "Untitled 1"/"Untitled 2"/...) - callers decide how they
// number untitled tabs (e.g. by position among currently-untitled sessions).
[[nodiscard]] inline std::wstring formatTabBaseLabel(const std::optional<std::wstring>& filename,
                                                      std::size_t untitledOrdinal) {
    if (filename) {
        return *filename;
    }
    return L"Untitled " + std::to_wstring(untitledOrdinal);
}

class TabBar {
public:
    TabBar()  = default;
    ~TabBar() = default;

    TabBar(const TabBar&)            = delete;
    TabBar& operator=(const TabBar&) = delete;
    TabBar(TabBar&&)                 = delete;
    TabBar& operator=(TabBar&&)      = delete;

    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const TabBarConfig& config);

    // WI-18a: lets callers recognize a right-click that bubbled up from this
    // control (see MainWindowConfig::onContextMenu's own comment) - same
    // "expose the raw HWND for identity comparison" role as MainWindow::
    // hwnd() itself. nullptr before create() succeeds.
    [[nodiscard]] HWND hwnd() const noexcept { return m_hwndTab.get(); }

    // Full rebuild (delete every existing item, re-insert from `items`) plus
    // marking `activeIndex` as the current selection. Called from the paint
    // handler every frame something was invalidated (see WI-05's completion
    // notes for why a "did the tab list actually change" guard was judged
    // unnecessary at this DoD's <=10-tab scale) rather than only when the
    // session list changes. No-op (returns immediately) if create() hasn't
    // succeeded or `items` is empty - a TabBar with zero tabs can't happen
    // in practice (Workspace always holds at least one session) but this
    // keeps the method safe to call defensively either way.
    void setTabs(std::vector<TabBarItem> items, std::size_t activeIndex) noexcept;

    // Docked full-width along the top edge (y=0..heightDips()) - only needs
    // the parent's width, unlike OutlinePane::onParentResized()'s full-height
    // right dock (which also needs parentHeight).
    void onParentResized(std::uint32_t parentWidth, float dpiScale) noexcept;

    // Routes a WM_NOTIFY the owning MainWindow received (TCN_SELCHANGE
    // arrives here - same routing OutlinePane::handleNotify() already uses
    // for TVN_SELCHANGEDW). Call from MainWindowConfig::onNotify. Filters on
    // NMHDR::hwndFrom (so it's safe to call unconditionally alongside
    // OutlinePane::handleNotify() from the same cfg.onNotify lambda, each
    // checking "is this mine?" independently) and always returns 0 - neither
    // TCN_SELCHANGE nor TVN_SELCHANGEDW require a specific non-zero reply.
    LRESULT handleNotify(WPARAM wParam, LPARAM lParam) noexcept;

    // Fixed DIP height this control always occupies - a compile-time-ish
    // constant (not derived from tab count/content), so
    // RenderPipeline::setTabBarHeightDips() can be told this value without
    // needing a live TabBar instance in scope. See render_pipeline.h's
    // m_tabBarHeightDips comment for how this space is reserved.
    [[nodiscard]] static constexpr float heightDips() noexcept { return kHeightDips; }

private:
    static constexpr float kHeightDips = 32.0F;  // untuned initial value, see WI-05 completion notes

    neomifes::platform::WindowHandle m_hwndTab;
    TabBarConfig                     m_config;
};

}  // namespace neomifes::ui
