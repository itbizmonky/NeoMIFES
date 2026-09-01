#include "neomifes/ui/json_tree_pane.h"

#include <commctrl.h>

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

// Continues the child-control ID block list outline_pane.cpp's own kTreeId
// comment documents (find_bar 1001-1003 / command_palette 2001-2002 /
// goto_line_bar 3001 / grep_bar 4001-4003 / outline_pane 5001 / tab_bar 6001
// / status_bar 7001) - 9001 is the next free block (8xxx is unused/reserved
// by no current control, left open per that same list's own gaps).
constexpr int      kTreeId     = 9001;
constexpr UINT_PTR kSubclassId = 1;

// Same layout constants as outline_pane.cpp's own (kPanelWidthDips/
// kFontSizeDips) - both panes dock full-height on the right and are never
// shown simultaneously in practice, so there is no reason for this pane to
// look different.
constexpr float kPanelWidthDips = 260.0F;
constexpr float kFontSizeDips   = 14.0F;

// json_tree_ui_population_hang.md: measured via a standalone probe
// (treeview_probe.cpp) - TVM_INSERTITEMW costs ~100-150us/item uncached,
// ~20-50us/item with WM_SETREDRAW suppressed. 20,000 items redraw-suppressed
// stays under ~1s, matching this codebase's other synchronous-UI-thread
// budgets (e.g. WI-16d's computeCsvRowOrder() "<=1s for 1M rows" precedent
// for a different kind of bulk operation). Below this, populateTree() keeps
// today's "insert everything, expand everything" behavior unchanged.
constexpr std::size_t kEagerFullyExpandThreshold = 20000;
// Cap on how many of a single node's children insertChildrenCapped() ever
// inserts as real HTREEITEMs at once - same probe, ~5,000 * 30-50us stays
// under ~250ms. Exists because a JSON document can be one giant flat array
// (json_tree_ui_population_hang.md's own repro was ~1.45M array elements at
// a single level) - on-demand lazy loading alone does not help there, since
// expanding that one level would still insert everything at once without
// this cap.
constexpr std::size_t kMaxChildrenPerLevel = 5000;

// Iterative (explicit-stack, see populateTree()'s own doc comment for why)
// count of every OutlineItem reachable from `items`, root items included -
// this is deliberately cheap (no SendMessageW, no string work) so it is safe
// to run unconditionally before deciding which of populateTree()'s two paths
// to take.
[[nodiscard]] std::size_t countTotalItems(const std::vector<OutlineItem>& items) noexcept {
    std::size_t total = 0;
    std::vector<const OutlineItem*> stack;
    stack.reserve(items.size());
    for (const auto& item : items) {
        stack.push_back(&item);
    }
    while (!stack.empty()) {
        const OutlineItem* current = stack.back();
        stack.pop_back();
        ++total;
        for (const auto& child : current->children) {
            stack.push_back(&child);
        }
    }
    return total;
}

}  // namespace

bool JsonTreePane::create(HWND parent, HINSTANCE hInstance, const JsonTreePaneConfig& config) {
    m_config = config;

    HWND tree = ::CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                                   WS_CHILD | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS, 0,
                                   0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kTreeId)),
                                   hInstance, nullptr);
    if (tree == nullptr) {
        return false;
    }
    m_hwndTree.reset(tree);

    if (::SetWindowSubclass(m_hwndTree.get(), &JsonTreePane::subclassProc, kSubclassId,
                             reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    ensureFont(1.0F);
    return true;
}

void JsonTreePane::showWith(std::vector<OutlineItem> items) noexcept {
    if (!m_hwndTree) {
        return;
    }
    m_items = std::move(items);
    populateTree();
    ::ShowWindow(m_hwndTree.get(), SW_SHOW);
    ::SetFocus(m_hwndTree.get());
}

void JsonTreePane::hide() noexcept {
    if (!m_hwndTree) {
        return;
    }
    ::ShowWindow(m_hwndTree.get(), SW_HIDE);
}

bool JsonTreePane::isVisible() const noexcept {
    return static_cast<bool>(m_hwndTree) && ::IsWindowVisible(m_hwndTree.get()) != FALSE;
}

void JsonTreePane::onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight, float dpiScale) noexcept {
    if (!m_hwndTree) {
        return;
    }
    ensureFont(dpiScale);

    const auto widthPx = static_cast<int>(kPanelWidthDips * dpiScale);
    const int  startX  = static_cast<int>(parentWidth) - widthPx;

    ::SetWindowPos(m_hwndTree.get(), nullptr, startX, 0, widthPx, static_cast<int>(parentHeight),
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT JsonTreePane::handleNotify(WPARAM /*wParam*/, LPARAM lParam) noexcept {
    const auto* header = reinterpret_cast<const NMHDR*>(lParam);
    if (header == nullptr || !m_hwndTree || header->hwndFrom != m_hwndTree.get()) {
        return 0;
    }

    if (header->code == TVN_SELCHANGEDW) {
        const auto* changed = reinterpret_cast<const NMTREEVIEW*>(lParam);
        // lParam is an OutlineItem* (see populateTree()/insertChildrenCapped()),
        // except the "... N more" informational row insertChildrenCapped()
        // appends, which deliberately carries 0 - not a real item, ignored.
        const auto* item = reinterpret_cast<const OutlineItem*>(changed->itemNew.lParam);
        if (item != nullptr && m_config.onItemSelected) {
            m_config.onItemSelected(item->targetPos);
        }
        return 0;
    }

    if (header->code == TVN_ITEMEXPANDINGW) {
        // json_tree_ui_population_hang.md: the lazy-load path's on-demand
        // fill. Verified via a standalone probe (see this WI's plan) that
        // this notification carries itemNew.lParam intact with no extra
        // TVM_GETITEM call needed, and - because comctl32 itself stops
        // sending it once an item has real children - fires at most once per
        // item, making the TVM_GETNEXTITEM/TVGN_CHILD guard below belt-and-
        // suspenders rather than the only thing preventing double-insertion.
        const auto* expanding = reinterpret_cast<const NMTREEVIEW*>(lParam);
        const auto* item      = reinterpret_cast<const OutlineItem*>(expanding->itemNew.lParam);
        if (item != nullptr) {
            auto* const existingChild = reinterpret_cast<HTREEITEM>(::SendMessageW(
                m_hwndTree.get(), TVM_GETNEXTITEM, TVGN_CHILD, reinterpret_cast<LPARAM>(expanding->itemNew.hItem)));
            if (existingChild == nullptr) {
                insertChildrenCapped(expanding->itemNew.hItem, item->children);
            }
        }
        return 0;
    }

    return 0;
}

void JsonTreePane::ensureFont(float dpiScale) noexcept {
    const auto fontHeightPx = -static_cast<int>(kFontSizeDips * dpiScale);
    // NOLINTNEXTLINE(misc-redundant-expression)
    constexpr int kPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    HFONT         font =
        ::CreateFontW(fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, kPitchAndFamily, L"Segoe UI");
    if (font == nullptr) {
        return;
    }
    m_font.reset(reinterpret_cast<HGDIOBJ>(font));
    if (m_hwndTree) {
        ::SendMessageW(m_hwndTree.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void JsonTreePane::populateTree() noexcept {
    if (!m_hwndTree) {
        return;
    }
    ::SendMessageW(m_hwndTree.get(), TVM_DELETEITEM, 0, reinterpret_cast<LPARAM>(TVI_ROOT));

    // WM_SETREDRAW around the whole rebuild in both paths below - measured
    // ~4.7x speedup (treeview_probe.cpp), no behavioral change. Absent from
    // this function entirely before json_tree_ui_population_hang.md's fix.
    ::SendMessageW(m_hwndTree.get(), WM_SETREDRAW, FALSE, 0);

    if (countTotalItems(m_items) <= kEagerFullyExpandThreshold) {
        // Explicit-stack pre-order walk - required here (m_items' depth is
        // bounded only by json_tree.cpp's kMaxJsonNestingDepth guard, not by
        // a naturally shallow tree shape - see this class's populateTree()
        // doc comment in json_tree_pane.h). Directly ported from
        // OutlinePane::populateTree() (outline_pane.cpp) - same PendingItem
        // frame shape, same reverse-push-for-original-order trick.
        struct PendingItem {
            const OutlineItem* item;
            HTREEITEM           parent;
        };
        std::vector<PendingItem> stack;
        stack.reserve(m_items.size());
        for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
            stack.push_back(PendingItem{.item = &*it, .parent = TVI_ROOT});
        }

        std::vector<HTREEITEM> parentsToExpand;
        while (!stack.empty()) {
            const PendingItem pending = stack.back();
            stack.pop_back();

            std::wstring     nameW(neomifes::util::toWstringView(pending.item->name));
            TVINSERTSTRUCTW insert{};
            insert.hParent      = pending.parent;
            insert.hInsertAfter = TVI_LAST;
            // TVINSERTSTRUCTW::item is a union member (shared with the ANSI
            // itemex layout) - this is CommCtrl.h's own C ABI, not a design
            // choice made here, so the union access is necessary and safe
            // (only ever written before the single TVM_INSERTITEMW call
            // below reads it).
            // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
            insert.item.mask    = TVIF_TEXT | TVIF_PARAM;
            insert.item.pszText = nameW.data();
            // lParam carries the OutlineItem* itself, not targetPos directly
            // - handleNotify() needs the full item (for TVN_ITEMEXPANDINGW
            // on the lazy path below), so both paths use the same encoding
            // and TVN_SELCHANGEDW dereferences it either way.
            insert.item.lParam = reinterpret_cast<LPARAM>(pending.item);
            // NOLINTEND(cppcoreguidelines-pro-type-union-access)

            auto* const inserted = reinterpret_cast<HTREEITEM>(
                ::SendMessageW(m_hwndTree.get(), TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert)));
            if (inserted == nullptr) {
                continue;
            }
            if (!pending.item->children.empty()) {
                parentsToExpand.push_back(inserted);
            }
            for (auto it = pending.item->children.rbegin(); it != pending.item->children.rend(); ++it) {
                stack.push_back(PendingItem{.item = &*it, .parent = inserted});
            }
        }

        // Always-expanded, matching OutlinePane's own choice (see
        // json_tree_pane.h's header comment / build_plan.md's WI-15c
        // "折り畳み状態の永続化" scope-out note) - every showWith() rebuilds
        // fully expanded, as long as the total stays within
        // kEagerFullyExpandThreshold (see this function's doc comment).
        for (HTREEITEM parent : parentsToExpand) {
            ::SendMessageW(m_hwndTree.get(), TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(parent));
        }
    } else {
        // json_tree_ui_population_hang.md: past the threshold, only the top
        // level goes in now (collapsed - deliberately NOT auto-expanded,
        // since m_items itself can already be a single node whose own
        // children number in the millions). Deeper levels populate on
        // demand via TVN_ITEMEXPANDINGW (handleNotify()).
        insertChildrenCapped(TVI_ROOT, m_items);
    }

    ::SendMessageW(m_hwndTree.get(), WM_SETREDRAW, TRUE, 0);
    ::InvalidateRect(m_hwndTree.get(), nullptr, TRUE);
}

void JsonTreePane::insertChildrenCapped(HTREEITEM parentHandle, const std::vector<OutlineItem>& children) noexcept {
    if (!m_hwndTree) {
        return;
    }
    const std::size_t insertCount = std::min(children.size(), kMaxChildrenPerLevel);
    for (std::size_t i = 0; i < insertCount; ++i) {
        const OutlineItem& child = children[i];
        std::wstring        nameW(neomifes::util::toWstringView(child.name));
        TVINSERTSTRUCTW      insert{};
        insert.hParent      = parentHandle;
        insert.hInsertAfter = TVI_LAST;
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        insert.item.mask    = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = nameW.data();
        insert.item.lParam  = reinterpret_cast<LPARAM>(&child);
        if (!child.children.empty()) {
            // Deferred-expand glyph, no real children inserted yet (Win32
            // standard lazy-tree pattern - verified against this exact
            // control via a standalone probe before this code was written,
            // see json_tree_ui_population_hang.md's plan). handleNotify()'s
            // TVN_ITEMEXPANDINGW case populates the real children the first
            // (and, per the probe, only ever the first) time this item is
            // expanded.
            insert.item.mask |= TVIF_CHILDREN;
            insert.item.cChildren = 1;
        }
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
        ::SendMessageW(m_hwndTree.get(), TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert));
    }

    if (children.size() > insertCount) {
        const std::size_t omitted = children.size() - insertCount;
        std::wstring       label  = L"… 他 " + std::to_wstring(omitted) + L" 件 (省略)";
        TVINSERTSTRUCTW     insert{};
        insert.hParent      = parentHandle;
        insert.hInsertAfter = TVI_LAST;
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        insert.item.mask    = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = label.data();
        // lParam 0 marks this as the informational "N more" row, not a real
        // OutlineItem - handleNotify()'s TVN_SELCHANGEDW case guards against
        // this exact sentinel (matching the existing `inserted == nullptr`
        // defensive style already used elsewhere in this file).
        insert.item.lParam = 0;
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)
        ::SendMessageW(m_hwndTree.get(), TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&insert));
    }
}

LRESULT JsonTreePane::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    switch (msg) {
        case WM_KEYDOWN:
            if (static_cast<UINT>(wParam) == VK_ESCAPE) {
                hide();
                if (m_config.onClosed) {
                    m_config.onClosed();
                }
                return 0;
            }
            break;
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hwnd, &JsonTreePane::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK JsonTreePane::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                             UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<JsonTreePane*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleSubclassMessage(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
