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
// dispatch.
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
};

}  // namespace neomifes::ui
