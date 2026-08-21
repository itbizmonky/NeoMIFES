#pragma once

#include <cstdint>

// CommandId (WI-07 step1) - numeric identifiers for commands that are
// invoked BOTH via the command palette (ui::CommandDescriptor, dot-separated
// string ids like "find.show") and via a keyboard shortcut. Win32's
// accelerator-table mechanism (CreateAcceleratorTable/TranslateAcceleratorW,
// wired in WI-07 step2) requires a WORD command id, not an arbitrary string,
// and WM_COMMAND's LOWORD(wParam) is that same WORD - so a single numeric id
// per command lets both paths (menu/accelerator -> WM_COMMAND -> a
// dispatchCommand(CommandId) switch, and the palette -> CommandDescriptor::
// action) resolve to the same logical command without duplicating its body.
//
// Deliberately a closed enum (not extensible by plugins) - this is host-only
// UI plumbing, unrelated to the plugin_sdk.h C ABI surface.
//
// Range: 40000+, chosen to sit far above every widget's own child-control-id
// block (find_bar.cpp 1001-1003, command_palette.cpp 2001-2002,
// goto_line_bar.cpp 3001, grep_bar.cpp 4001-4003, outline_pane.cpp 5001,
// tab_bar.cpp 6001, status_bar.cpp 7001 - see each file's own `namespace {
// constexpr int kXxxId = ...; }`) so accelerator command ids and native
// child-window control ids can never collide inside the same WM_COMMAND
// dispatch. WI-11's "recent files" menu items (menu_bar.h's
// kRecentFileIdBase) take the next available block, 8001-8020 (20 slots,
// core::RecentFiles' own MRU cap) - kept a full order of magnitude below
// this enum's own 40000+ range so CommandId's future growth can never
// collide with it, the same reasoning that separates 40000+ from the
// 1001-7001 child-control blocks above. Deliberately NOT CommandId
// enumerators themselves: their identity (which file index N points at)
// changes at runtime as files are opened/saved, unlike every fixed,
// compile-time-constant CommandId below.
namespace neomifes::ui {

enum class CommandId : std::uint16_t {
    None = 0,

    FindShow = 40000,
    FindReplace,
    FindNext,
    FindPrevious,
    GrepShow,
    CommandPaletteShow,
    OutlineToggle,
    // Phase 10.3, WI-15c: toggles ui::JsonTreePane, the JSON/XML structure
    // tree panel - same "toggle a right-docked panel" shape as
    // OutlineToggle, declared immediately after it for that reason.
    JsonTreeToggle,
    GotoLineShow,
    BookmarkToggle,
    BookmarkNext,
    BookmarkPrevious,
    TagJump,
    Save,
    SaveAs,
    Open,
    New,
    TabNext,
    TabPrevious,
    TabClose,
    TabSwitch1,
    TabSwitch2,
    TabSwitch3,
    TabSwitch4,
    TabSwitch5,
    TabSwitch6,
    TabSwitch7,
    TabSwitch8,
    TabSwitch9,
    Copy,
    Cut,
    Paste,
    Undo,
    Redo,
    // WI-07 step3: menu-only (Help menu), never accelerator-routed or
    // palette-registered - shows a minimal version MessageBoxW.
    About,
    // WI-07 step5: toggles EditorSession::overwriteMode(). Bound to a bare
    // VK_INSERT keypress in normal_mode_wiring.cpp's key-down chain (NOT the
    // accelerator table - see handleClipboardOrUndoRedoKey()'s own comment
    // for why: a native WC_EDIT control, like the overlay widgets' own text
    // fields, supports a built-in overtype toggle on a bare Insert keypress,
    // which a global accelerator entry would intercept before it ever
    // reaches the focused control). Not menu- or palette-registered (no
    // approved design point calls for that yet).
    ToggleOverwriteMode,
};

}  // namespace neomifes::ui
