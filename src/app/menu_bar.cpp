#include "neomifes/app/menu_bar.h"

#include <span>

namespace neomifes::app {

namespace {

// Builds one flyout menu from `items` and appends it to `menuBar` under
// `title`. Returns false (menuBar is left with whatever popups already
// succeeded before the first failure - buildMenuBar() below destroys the
// whole thing on any false) if CreatePopupMenu/AppendMenuW fails.
[[nodiscard]] bool appendPopupMenu(HMENU menuBar, const wchar_t* title, std::span<const MenuItemSpec> items) {
    // Not `const HMENU` - HMENU is a pointer typedef, so a top-level const
    // here would apply to the pointer itself (misc-misplaced-const), not
    // add any real safety.
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return false;
    }
    for (const MenuItemSpec& item : items) {
        ::AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(item.commandId), item.label);
    }
    // MF_POPUP's uIDNewItem parameter is the submenu HMENU reinterpreted as
    // UINT_PTR - a standard Win32 handle<->integer conversion (same pattern
    // find_bar.cpp/command_palette.cpp already use for control ids), not the
    // "opaque handle smuggled through a void*" kind CLAUDE.md's dynamic_cast
    // ban targets.
    return ::AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(menu), title) != 0;
}

}  // namespace

HMENU buildMenuBar() {
    // Not `const HMENU` - see appendPopupMenu()'s own comment.
    HMENU menuBar = ::CreateMenu();
    if (menuBar == nullptr) {
        return nullptr;
    }
    const bool ok = appendPopupMenu(menuBar, L"ファイル(&F)", kFileMenuItems) &&
                    appendPopupMenu(menuBar, L"編集(&E)", kEditMenuItems) &&
                    appendPopupMenu(menuBar, L"検索(&S)", kSearchMenuItems) &&
                    appendPopupMenu(menuBar, L"表示(&V)", kViewMenuItems) &&
                    appendPopupMenu(menuBar, L"ツール(&T)", kToolsMenuItems) &&
                    appendPopupMenu(menuBar, L"ヘルプ(&H)", kHelpMenuItems);
    if (!ok) {
        ::DestroyMenu(menuBar);
        return nullptr;
    }
    return menuBar;
}

}  // namespace neomifes::app
