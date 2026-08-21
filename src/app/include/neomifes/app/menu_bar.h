#pragma once

// menu_bar (WI-07 step3, WI-11) - builds the top-level HMENU
// wireNormalMode() hands to MainWindowConfig::menuBar. Menu item ids are
// ui::CommandId values (same 40000+ range command_ids.h reserves for
// kAcceleratorTable's WORD cmd field) - a menu click generates WM_COMMAND
// with LOWORD(wParam) == the item's id, routed through wireNormalMode()'s
// existing cfg.onCommand exactly like an accelerator-originated WM_COMMAND
// (see command_dispatch.h's own LOWORD(wParam) comment). No new routing
// mechanism needed: menu items whose CommandId dispatchCommand() already
// understands (Save/SaveAs/Open/New/Copy/Cut/Paste/Undo/Redo) reach it
// unchanged; menu items for the toggle-widget commands dispatchCommand()
// deliberately does NOT handle (Find/FindReplace/FindNext/FindPrevious/
// Grep/CommandPalette/Outline/GotoLine/About) are intercepted earlier in
// cfg.onCommand by dispatchWidgetShowCommand() (normal_mode_wiring.cpp) -
// unlike a global accelerator table entry, a MENU click carries no
// focus-interception risk (see command_dispatch.cpp's own top comment for
// why THAT risk ruled out promoting these to kAcceleratorTable), so wiring
// them here is safe.
//
// WI-11's "最近使ったファイル" (recent files) submenu is a NEW kind of menu
// content this codebase hasn't needed before: unlike every fixed
// compile-time kXxxMenuItems array below, its item COUNT and LABELS change
// at runtime as files are opened/saved. This is this codebase's first use
// of GetSubMenu/RemoveMenu/DrawMenuBar (see docs/issues/
// menu_bar_keybinding_label_stale.md, a P2 issue WI-10 filed noting this
// exact gap for a different symptom - static Ctrl+X accelerator hint text
// going stale; the mechanism built here to solve THIS problem could later
// be reused to also fix that one, though that isn't done here).
// buildMenuBar() returns BOTH the top-level HMENU and a direct handle to
// the recent-files submenu it just built (MenuBarHandles) - refreshRecent
// FilesMenu() reuses that SAME handle later rather than re-deriving it via
// a fragile GetSubMenu(fileMenu, <hardcoded position>) lookup that would
// silently break if kFileMenuItems' own item count/order ever changes.

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include <windows.h>

#include "neomifes/core/recent_files.h"
#include "neomifes/ui/command_ids.h"

namespace neomifes::app {

struct MenuItemSpec {
    ui::CommandId  commandId;
    const wchar_t* label;  // '&' marks the mnemonic, '\t' separates the trailing accelerator hint text
};

// The 6 flyout menus' contents, exposed here (not just inside menu_bar.cpp)
// so tests/unit/app_menu_bar_test.cpp can verify membership directly without
// menu_bar.cpp (which compiles into the NeoMIFES executable target, not a
// library tests/unit links) linked into the test binary - same
// "header-expose the pure data for headless testability" convention
// command_dispatch.h's kAcceleratorTable already follows.
inline constexpr std::array<MenuItemSpec, 4> kFileMenuItems = {{
    {ui::CommandId::Open, L"開く(&O)\tCtrl+O"},
    {ui::CommandId::Save, L"保存(&S)\tCtrl+S"},
    {ui::CommandId::SaveAs, L"名前を付けて保存(&A)\tCtrl+Shift+S"},
    {ui::CommandId::New, L"新規(&N)\tCtrl+N"},
}};

inline constexpr std::array<MenuItemSpec, 5> kEditMenuItems = {{
    {ui::CommandId::Undo, L"元に戻す(&U)\tCtrl+Z"},
    {ui::CommandId::Redo, L"やり直し(&R)\tCtrl+Y"},
    {ui::CommandId::Cut, L"切り取り(&T)\tCtrl+X"},
    {ui::CommandId::Copy, L"コピー(&C)\tCtrl+C"},
    {ui::CommandId::Paste, L"貼り付け(&P)\tCtrl+V"},
}};

inline constexpr std::array<MenuItemSpec, 6> kSearchMenuItems = {{
    {ui::CommandId::FindShow, L"検索(&F)\tCtrl+F"},
    {ui::CommandId::FindReplace, L"置換(&H)\tCtrl+H"},
    {ui::CommandId::FindNext, L"次を検索(&N)\tF3"},
    {ui::CommandId::FindPrevious, L"前を検索(&V)\tShift+F3"},
    {ui::CommandId::GrepShow, L"フォルダ内検索(&G)\tCtrl+Shift+F"},
    {ui::CommandId::GotoLineShow, L"指定行へ移動(&L)\tCtrl+G"},
}};

inline constexpr std::array<MenuItemSpec, 3> kViewMenuItems = {{
    {ui::CommandId::OutlineToggle, L"アウトライン(&O)\tCtrl+Shift+O"},
    {ui::CommandId::JsonTreeToggle, L"JSON構造ツリー(&J)\tCtrl+Shift+J"},
    {ui::CommandId::CsvGridToggle, L"CSVグリッド(&G)\tCtrl+Shift+G"},
}};

inline constexpr std::array<MenuItemSpec, 1> kToolsMenuItems = {{
    {ui::CommandId::CommandPaletteShow, L"コマンドパレット(&P)\tCtrl+Shift+P"},
}};

inline constexpr std::array<MenuItemSpec, 1> kHelpMenuItems = {{
    {ui::CommandId::About, L"バージョン情報(&A)"},
}};

// WI-11: id range for the "最近使ったファイル" submenu's dynamic items - see
// command_ids.h's own comment on why this sits at 8001-8020, a fresh block
// below CommandId's 40000+ range and above every native child-control-id
// block (highest existing one: status_bar.cpp's 7001).
inline constexpr int kRecentFileIdBase        = 8001;
inline constexpr int kMaxRecentFileMenuItems  = 20;  // mirrors core::RecentFiles::kMaxEntries

struct RecentFileMenuItemSpec {
    int          id;  // 0 for the disabled "(なし)" placeholder - never a real WM_COMMAND id
    std::wstring label;
};

// Pure data transformation (no Win32 calls) - the testable half of the
// recent-files submenu; menu_bar.cpp's populateRecentFilesSubmenu() is the
// thin AppendMenuW-calling half that consumes this. Empty recentFiles ->
// a single disabled placeholder item. Otherwise one item per entry (id =
// kRecentFileIdBase + index, index 0 = most recent - matches core::
// RecentFiles::entries()' own MRU order), label = the entry's FULL path
// (not just the filename - same-named files living in different
// directories would otherwise be indistinguishable, and this app has no
// tooltip/status-bar preview mechanism to disambiguate; no truncation for
// very long paths either - both accepted minor UX simplifications, not
// load-bearing for correctness).
[[nodiscard]] inline std::vector<RecentFileMenuItemSpec> buildRecentFileMenuItems(
    const core::RecentFiles& recentFiles) {
    const auto& entries = recentFiles.entries();
    if (entries.empty()) {
        return {RecentFileMenuItemSpec{.id = 0, .label = L"(なし)"}};
    }
    const std::size_t count = std::min(entries.size(), static_cast<std::size_t>(kMaxRecentFileMenuItems));
    std::vector<RecentFileMenuItemSpec> items;
    items.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        items.push_back(RecentFileMenuItemSpec{.id = kRecentFileIdBase + static_cast<int>(i),
                                                .label = entries[i].wstring()});
    }
    return items;
}

// WI-11: both HMENUs buildMenuBar() constructs - recentFilesSubmenu is
// returned separately (not re-derived later via GetSubMenu) so
// refreshRecentFilesMenu() can mutate the SAME menu instance directly, see
// this header's own top comment.
struct MenuBarHandles {
    HMENU menuBar;
    HMENU recentFilesSubmenu;
};

// Returns {nullptr, nullptr} on failure (CreateMenu/CreatePopupMenu/
// AppendMenuW can all fail under resource exhaustion) - MainWindow::create()
// treats a null MainWindowConfig::menuBar exactly like Win32's own "no
// menu" default (the window simply has no menu bar), same non-fatal-
// degradation convention findBar.create()'s own failure gets. No RAII
// wrapper: once handed to CreateWindowExW's hMenu parameter, the WINDOW
// owns the HMENU for its lifetime and DestroyWindow destroys it
// automatically (standard Win32 ownership transfer, including every nested
// popup submenu) - callers must not also call DestroyMenu on a menu that
// was successfully attached to a window. `recentFiles`' CURRENT entries()
// populate the File menu's "最近使ったファイル" submenu immediately (no
// separate post-build refresh call needed for the startup case - main.cpp
// loads recent.json before calling this).
[[nodiscard]] MenuBarHandles buildMenuBar(const core::RecentFiles& recentFiles);

// Rebuilds `handles.recentFilesSubmenu`'s contents from `recentFiles`'
// CURRENT entries() (clears every existing item first via repeated
// RemoveMenu(..., 0, MF_BYPOSITION) until empty, then re-appends via the
// same buildRecentFileMenuItems() the initial build used) and forces the
// OS to repaint the already-visible top-level menu bar (DrawMenuBar) -
// required per Win32 docs whenever a menu already attached to a visible
// window is mutated after the fact. Called (via normal_mode_wiring.cpp)
// after every successful open/save that changes `recentFiles`' entries. A
// no-op-looking call (recentFiles unchanged) is harmless - clear+rebuild
// is not conditioned on an actual change, callers only invoke this after
// having just called recentFiles.record().
void refreshRecentFilesMenu(const MenuBarHandles& handles, HWND hwnd, const core::RecentFiles& recentFiles);

}  // namespace neomifes::app
