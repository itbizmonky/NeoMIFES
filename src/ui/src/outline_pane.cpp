#include "neomifes/ui/outline_pane.h"

#include <commctrl.h>

#include <string>
#include <vector>

#include "neomifes/util/wchar_cast.h"

namespace neomifes::ui {

namespace {

constexpr int      kTreeId     = 5001;
constexpr UINT_PTR kSubclassId = 1;

// Layout constants in DIPs (96-DPI baseline, same convention as
// command_palette.cpp's kWidthDips). Docked full-height on the right, unlike
// every earlier overlay's fixed-size centered/top-anchored box - see the
// Phase 7g plan's Context point 5 for why an outline view needs the room.
constexpr float kPanelWidthDips = 260.0F;
constexpr float kFontSizeDips   = 14.0F;

}  // namespace

bool OutlinePane::create(HWND parent, HINSTANCE hInstance, const OutlinePaneConfig& config) {
    m_config = config;

    HWND tree = ::CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS, 0, 0, 10, 10,
        parent, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kTreeId)), hInstance, nullptr);
    if (tree == nullptr) {
        return false;
    }
    m_hwndTree.reset(tree);

    if (::SetWindowSubclass(m_hwndTree.get(), &OutlinePane::subclassProc, kSubclassId,
                            reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
        return false;
    }

    ensureFont(1.0F);
    return true;
}

void OutlinePane::showWith(std::vector<OutlineItem> items) noexcept {
    if (!m_hwndTree) {
        return;
    }
    m_items = std::move(items);
    populateTree();
    ::ShowWindow(m_hwndTree.get(), SW_SHOW);
    ::SetFocus(m_hwndTree.get());
}

void OutlinePane::hide() noexcept {
    if (!m_hwndTree) {
        return;
    }
    ::ShowWindow(m_hwndTree.get(), SW_HIDE);
}

bool OutlinePane::isVisible() const noexcept {
    return static_cast<bool>(m_hwndTree) && ::IsWindowVisible(m_hwndTree.get()) != FALSE;
}

void OutlinePane::onParentResized(std::uint32_t parentWidth, std::uint32_t parentHeight,
                                  float dpiScale) noexcept {
    if (!m_hwndTree) {
        return;
    }
    ensureFont(dpiScale);

    const auto widthPx = static_cast<int>(kPanelWidthDips * dpiScale);
    const int  startX  = static_cast<int>(parentWidth) - widthPx;

    ::SetWindowPos(m_hwndTree.get(), nullptr, startX, 0, widthPx, static_cast<int>(parentHeight),
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT OutlinePane::handleNotify(WPARAM /*wParam*/, LPARAM lParam) noexcept {
    const auto* header = reinterpret_cast<const NMHDR*>(lParam);
    if (header == nullptr || header->code != TVN_SELCHANGEDW || !m_hwndTree ||
        header->hwndFrom != m_hwndTree.get()) {
        return 0;
    }
    const auto* changed = reinterpret_cast<const NMTREEVIEW*>(lParam);
    if (m_config.onItemSelected) {
        m_config.onItemSelected(static_cast<std::uint64_t>(changed->itemNew.lParam));
    }
    return 0;
}

void OutlinePane::ensureFont(float dpiScale) noexcept {
    const auto fontHeightPx = -static_cast<int>(kFontSizeDips * dpiScale);
    // NOLINTNEXTLINE(misc-redundant-expression)
    constexpr int kPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    HFONT font = ::CreateFontW(fontHeightPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               kPitchAndFamily, L"Segoe UI");
    if (font == nullptr) {
        return;
    }
    m_font.reset(reinterpret_cast<HGDIOBJ>(font));
    if (m_hwndTree) {
        ::SendMessageW(m_hwndTree.get(), WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void OutlinePane::populateTree() noexcept {
    if (!m_hwndTree) {
        return;
    }
    ::SendMessageW(m_hwndTree.get(), TVM_DELETEITEM, 0, reinterpret_cast<LPARAM>(TVI_ROOT));

    // Explicit-stack pre-order walk (not recursive) - see this class's
    // populateTree() doc comment in outline_pane.h for why. Each stack entry
    // is a not-yet-inserted item paired with the HTREEITEM it belongs under;
    // a node's children are pushed in reverse so they pop (and therefore get
    // inserted via TVI_LAST) in their original left-to-right order.
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

        std::wstring nameW(neomifes::util::toWstringView(pending.item->name));
        TVINSERTSTRUCTW insert{};
        insert.hParent      = pending.parent;
        insert.hInsertAfter = TVI_LAST;
        // TVINSERTSTRUCTW::item is a union member (shared with the ANSI
        // itemex layout) - this is CommCtrl.h's own C ABI, not a design
        // choice made here, so the union access is necessary and safe (only
        // ever written before the single TVM_INSERTITEMW call below reads it).
        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        insert.item.mask    = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = nameW.data();
        insert.item.lParam  = static_cast<LPARAM>(pending.item->targetPos);
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

    // Always-expanded (Phase 7g scope - see plan's "折り畳み状態の永続化"
    // scope-out note; every showWith() rebuilds fully expanded).
    for (HTREEITEM parent : parentsToExpand) {
        ::SendMessageW(m_hwndTree.get(), TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(parent));
    }
}

LRESULT OutlinePane::handleSubclassMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
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
            ::RemoveWindowSubclass(hwnd, &OutlinePane::subclassProc, kSubclassId);
            break;
        default:
            break;
    }
    return ::DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OutlinePane::subclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR /*subclassId*/, DWORD_PTR refData) noexcept {
    auto* self = reinterpret_cast<OutlinePane*>(refData);
    if (self == nullptr) {
        return ::DefSubclassProc(hwnd, msg, wParam, lParam);
    }
    return self->handleSubclassMessage(hwnd, msg, wParam, lParam);
}

}  // namespace neomifes::ui
