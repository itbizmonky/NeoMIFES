#include "neomifes/app/command_dispatch.h"

// buildAcceleratorTable() (WI-07 step2, WI-10) - self-contained (no
// dependency on normal_mode_wiring.cpp's private helpers, unlike
// dispatchCommand() - defined in normal_mode_wiring.cpp instead, see
// command_dispatch.h's own top comment for why).
//
// kAcceleratorEligibleCommands' membership (keybinding_dispatch.h) is
// deliberately narrow, fixed, and NOT user-configurable. The concrete
// conflict that ruled out a wider table (WI-07): TranslateAcceleratorW
// intercepts a matching WM_KEYDOWN BEFORE Win32's normal focus-based
// delivery ever reaches the window that actually has keyboard focus. The
// overlay widgets (FindDialog/GrepBar/CommandPalette/GotoLineBar - all
// standard WC_EDIT-based text fields) rely on receiving Ctrl+C/X/V/Z
// themselves via comctl32's own built-in copy/cut/paste/undo handling; a
// GLOBAL accelerator entry for the same key combo would silently break
// editing INSIDE those text fields (e.g. Ctrl+V while typing a Find query
// would paste into the DOCUMENT instead of the query field). F3/Shift+F3
// have a subtler version of the same problem: FindDialogConfig::onFindNext/
// onFindPrevious (fired while the find edit has focus) additionally record
// search history - promoting F3 to a global accelerator would silently
// skip that recording whenever F3 is pressed while the find edit itself
// has focus. Save/SaveAs/Open/New/tab-switch/tab-close have no such
// conflict (no overlay widget claims these combinations, and none has any
// existing per-widget special casing for them), so only those are eligible.

namespace neomifes::app {

platform::AcceleratorTableHandle buildAcceleratorTable(const core::KeyBindings& keyBindings) {
    // buildAcceleratorRows() (keybinding_dispatch.h) already returns a
    // fresh, owned, non-const std::vector<ACCEL> - unlike the old constexpr
    // kAcceleratorTable it replaced, no copy-into-a-mutable-array step is
    // needed to satisfy CreateAcceleratorTableW's non-const LPACCEL
    // parameter (a long-standing Win32 API constness quirk, not a real
    // writability requirement - it copies the array internally and never
    // retains or mutates the caller's copy).
    std::vector<ACCEL> rows = buildAcceleratorRows(keyBindings);
    return platform::AcceleratorTableHandle(
        ::CreateAcceleratorTableW(rows.data(), static_cast<int>(rows.size())));
}

}  // namespace neomifes::app
