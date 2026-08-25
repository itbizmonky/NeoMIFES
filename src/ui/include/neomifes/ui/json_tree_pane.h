#pragma once

// JsonTreePane - the JSON/XML structure tree panel's WC_TREEVIEW child
// control (Phase 10.3, WI-15c). Directly modeled on ui::OutlinePane
// (outline_pane.h) - same Win32 mechanics (WC_TREEVIEW subclass, WM_NOTIFY
// routing, DPI-aware right-docked layout, Escape-to-close), same reused
// ui::OutlineItem tree shape (see below for why no JSON-specific item struct
// exists). Kept as an independent class rather than generalizing
// OutlinePane, matching this codebase's established "direct template, not
// shared base class" choice already made for LogIndexWorker/JsonTreeWorker/
// CsvModelWorker (see json_tree_worker.h's own header comment) - avoids a
// refactor of OutlinePane's already-shipped, working code for the sake of
// one sibling consumer.
//
// Reuses ui::OutlineItem as-is (name/targetPos/children) rather than
// introducing a JsonTreeItem: the shape is identical (a label, a jump
// target, child items) and OutlinePane's own item type already carries no
// syntax::OutlineNode-specific fields - app::buildJsonTreeItems()
// (src/app/include/neomifes/app/json_tree_bridge.h) is the bridge that
// turns a jsontree::JsonNode into this same OutlineItem shape, the same way
// app::buildOutlineItems() does for syntax::OutlineNode.
//
// Unlike OutlinePane's data source (extractOutline(), synchronous), this
// pane's data arrives via JsonTreeWorker's asynchronous background parse
// (WI-15b) - showWith() itself stays synchronous (just populates the tree
// control from whatever OutlineItem list it's handed), but the caller
// (normal_mode_wiring.cpp's refreshJsonTreePane()/toggleJsonTreePane()) is
// responsible for deciding whether that list is available yet. See
// build_plan.md's WI-15c plan for the "pending session token" design this
// asynchrony requires - none of that lives in this class.

#include <windows.h>

#include <cstdint>
#include <functional>
#include <vector>

#include "neomifes/platform/handle_guard.h"
#include "neomifes/ui/outline_pane.h"

namespace neomifes::ui {

struct JsonTreePaneConfig {
    // Fired when the user selects any tree item, leaf or container - a
    // container's own opening delimiter position is still a meaningful jump
    // target (matches OutlinePaneConfig::onItemSelected's same rationale for
    // symbol containers).
    std::function<void(std::uint64_t targetPos)> onItemSelected;
    // Escape. Caller restores focus to the document and clears any pending
    // async-display state (same contract as OutlinePaneConfig::onClosed,
    // plus the pending-token cleanup WI-15c's asynchronous data source
    // requires - see this class's header comment).
    std::function<void()> onClosed;
};

class JsonTreePane {
public:
    JsonTreePane()  = default;
    ~JsonTreePane() = default;

    JsonTreePane(const JsonTreePane&)            = delete;
    JsonTreePane& operator=(const JsonTreePane&) = delete;
    JsonTreePane(JsonTreePane&&)                 = delete;
    JsonTreePane& operator=(JsonTreePane&&)      = delete;

    [[nodiscard]] bool create(HWND parent, HINSTANCE hInstance, const JsonTreePaneConfig& config);

    // Replaces the tree contents and shows the panel (docks full-height on
    // the right - see onParentResized). Re-invoking while already visible
    // refreshes in place - mirrors OutlinePane::showWith()'s own contract.
    void showWith(std::vector<OutlineItem> items) noexcept;
    void hide() noexcept;
    [[nodiscard]] bool isVisible() const noexcept;

    void onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight, float dpiScale) noexcept;

    // WI-15i: see ui::OutlinePane::widthDips()'s own comment - same shape,
    // same duplicated-constant reasoning, same 260.0F value as this class's
    // own private json_tree_pane.cpp kPanelWidthDips (both panes share the
    // identical docked-strip layout, see this class's own header comment).
    [[nodiscard]] static constexpr float widthDips() noexcept { return 260.0F; }

    // Routes a WM_NOTIFY the owning MainWindow received (TVN_SELCHANGEDW
    // arrives here, not at the child itself - same routing OutlinePane
    // already uses, see MainWindowConfig::onNotify's doc comment). Call from
    // MainWindowConfig::onNotify.
    LRESULT handleNotify(WPARAM wParam, LPARAM lParam) noexcept;

private:
    static LRESULT CALLBACK subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId,
                                          DWORD_PTR refData) noexcept;
    LRESULT handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
    // Iterative (explicit-stack) rebuild of the tree control's items from
    // m_items - required here, unlike OutlinePane's own populateTree()
    // (which only tolerates the pattern out of caution): m_items comes from
    // app::buildJsonTreeItems(), whose own depth is bounded only by
    // json_tree.cpp's kMaxJsonNestingDepth guard (200), not by a naturally
    // shallow symbol-definition nesting the way syntax::OutlineNode's is.
    void populateTree() noexcept;
    void ensureFont(float dpiScale) noexcept;

    neomifes::platform::WindowHandle    m_hwndTree;
    neomifes::platform::GdiObjectHandle m_font;
    std::vector<OutlineItem>            m_items;
    JsonTreePaneConfig                  m_config;
};

}  // namespace neomifes::ui
