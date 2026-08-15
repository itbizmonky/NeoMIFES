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

// WI-11: the Win32-calling half of the recent-files submenu -
// buildRecentFileMenuItems() (menu_bar.h, pure/testable) decides WHAT
// items should exist, this just appends them. Shared by buildMenuBar()'s
// initial population and refreshRecentFilesMenu()'s rebuild.
void populateRecentFilesSubmenu(HMENU recentFilesSubmenu, const core::RecentFiles& recentFiles) {
    for (const RecentFileMenuItemSpec& item : buildRecentFileMenuItems(recentFiles)) {
        const UINT flags = item.id == 0 ? (MF_STRING | MF_GRAYED) : MF_STRING;
        ::AppendMenuW(recentFilesSubmenu, flags, static_cast<UINT_PTR>(item.id), item.label.c_str());
    }
}

}  // namespace

MenuBarHandles buildMenuBar(const core::RecentFiles& recentFiles) {
    // Not `const HMENU` - see appendPopupMenu()'s own comment.
    HMENU menuBar = ::CreateMenu();
    if (menuBar == nullptr) {
        return {.menuBar = nullptr, .recentFilesSubmenu = nullptr};
    }
    bool ok = appendPopupMenu(menuBar, L"ファイル(&F)", kFileMenuItems);

    // WI-11: the "最近使ったファイル" submenu is appended to the File popup
    // AFTER kFileMenuItems' 4 static entries, as its 5th item - a nested
    // MF_POPUP, unlike every entry appendPopupMenu() itself builds (those
    // are all flat MF_STRING commands).
    HMENU recentFilesSubmenu = nullptr;
    if (ok) {
        recentFilesSubmenu = ::CreatePopupMenu();
        if (recentFilesSubmenu == nullptr) {
            ok = false;
        } else {
            populateRecentFilesSubmenu(recentFilesSubmenu, recentFiles);
            HMENU fileMenu = ::GetSubMenu(menuBar, 0);
            ok = fileMenu != nullptr &&
                 ::AppendMenuW(fileMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(recentFilesSubmenu),
                              L"最近使ったファイル(&R)") != 0;
        }
    }

    ok = ok && appendPopupMenu(menuBar, L"編集(&E)", kEditMenuItems) &&
         appendPopupMenu(menuBar, L"検索(&S)", kSearchMenuItems) &&
         appendPopupMenu(menuBar, L"表示(&V)", kViewMenuItems) &&
         appendPopupMenu(menuBar, L"ツール(&T)", kToolsMenuItems) &&
         appendPopupMenu(menuBar, L"ヘルプ(&H)", kHelpMenuItems);
    if (!ok) {
        ::DestroyMenu(menuBar);  // also destroys every nested popup, including recentFilesSubmenu
        return {.menuBar = nullptr, .recentFilesSubmenu = nullptr};
    }
    return {.menuBar = menuBar, .recentFilesSubmenu = recentFilesSubmenu};
}

void refreshRecentFilesMenu(const MenuBarHandles& handles, HWND hwnd, const core::RecentFiles& recentFiles) {
    if (handles.recentFilesSubmenu == nullptr) {
        return;
    }
    while (::RemoveMenu(handles.recentFilesSubmenu, 0, MF_BYPOSITION)) {
    }
    populateRecentFilesSubmenu(handles.recentFilesSubmenu, recentFiles);
    ::DrawMenuBar(hwnd);
}

}  // namespace neomifes::app
