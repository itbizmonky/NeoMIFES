#pragma once

// menu_bar (WI-07 step3) - builds the top-level HMENU wireNormalMode() hands
// to MainWindowConfig::menuBar. Menu item ids are ui::CommandId values (same
// 40000+ range command_ids.h reserves for kAcceleratorTable's WORD cmd
// field) - a menu click generates WM_COMMAND with LOWORD(wParam) == the
// item's id, routed through wireNormalMode()'s existing cfg.onCommand
// exactly like an accelerator-originated WM_COMMAND (see command_dispatch.h's
// own LOWORD(wParam) comment). No new routing mechanism needed: menu items
// whose CommandId dispatchCommand() already understands (Save/SaveAs/Open/
// New/Copy/Cut/Paste/Undo/Redo) reach it unchanged; menu items for the
// toggle-widget commands dispatchCommand() deliberately does NOT handle
// (Find/FindReplace/FindNext/FindPrevious/Grep/CommandPalette/Outline/
// GotoLine/About) are intercepted earlier in cfg.onCommand by
// dispatchWidgetShowCommand() (normal_mode_wiring.cpp) - unlike a global
// accelerator table entry, a MENU click carries no focus-interception risk
// (see command_dispatch.cpp's own top comment for why THAT risk ruled out
// promoting these to kAcceleratorTable), so wiring them here is safe.

#include <array>
#include <windows.h>

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

inline constexpr std::array<MenuItemSpec, 1> kViewMenuItems = {{
    {ui::CommandId::OutlineToggle, L"アウトライン(&O)\tCtrl+Shift+O"},
}};

inline constexpr std::array<MenuItemSpec, 1> kToolsMenuItems = {{
    {ui::CommandId::CommandPaletteShow, L"コマンドパレット(&P)\tCtrl+Shift+P"},
}};

inline constexpr std::array<MenuItemSpec, 1> kHelpMenuItems = {{
    {ui::CommandId::About, L"バージョン情報(&A)"},
}};

// Returns nullptr on failure (CreateMenu/CreatePopupMenu/AppendMenuW can all
// fail under resource exhaustion) - MainWindow::create() treats a null
// MainWindowConfig::menuBar exactly like Win32's own "no menu" default (the
// window simply has no menu bar), same non-fatal-degradation convention
// findBar.create()'s own failure gets. No RAII wrapper: once handed to
// CreateWindowExW's hMenu parameter, the WINDOW owns the HMENU for its
// lifetime and DestroyWindow destroys it automatically (standard Win32
// ownership transfer) - callers must not also call DestroyMenu on a menu
// that was successfully attached to a window.
[[nodiscard]] HMENU buildMenuBar();

}  // namespace neomifes::app
