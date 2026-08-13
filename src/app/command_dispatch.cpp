#include "neomifes/app/command_dispatch.h"

#include <algorithm>
#include <array>
#include <iterator>

// buildAcceleratorTable() (WI-07 step2) - self-contained (no dependency on
// normal_mode_wiring.cpp's private helpers, unlike dispatchCommand() -
// defined in normal_mode_wiring.cpp instead, see command_dispatch.h's own
// top comment for why).
//
// kAcceleratorTable's membership (command_dispatch.h) is deliberately
// narrow. The concrete conflict that ruled out a wider table:
// TranslateAcceleratorW intercepts a matching WM_KEYDOWN BEFORE Win32's
// normal focus-based delivery ever reaches the window that actually has
// keyboard focus. The 5 overlay widgets (FindBar/GrepBar/CommandPalette/
// GotoLineBar - all standard WC_EDIT-based text fields) rely on receiving
// Ctrl+C/X/V/Z themselves via comctl32's own built-in copy/cut/paste/undo
// handling; a GLOBAL accelerator entry for the same key combo would
// silently break editing INSIDE those text fields (e.g. Ctrl+V while typing
// a Find query would paste into the DOCUMENT instead of the query field).
// F3/Shift+F3 have a subtler version of the same problem:
// FindBarConfig::onFindNext/onFindPrevious (fired while the find edit has
// focus) additionally record search history - promoting F3 to a global
// accelerator would silently skip that recording whenever F3 is pressed
// while the find edit itself has focus. Save/SaveAs/Open/New/tab-switch/
// tab-close have no such conflict (no overlay widget claims these
// combinations, and none has any existing per-widget special casing for
// them), so only those are included.

namespace neomifes::app {

platform::AcceleratorTableHandle buildAcceleratorTable() {
    // CreateAcceleratorTableW's LPACCEL parameter is non-const (it copies
    // the array internally; it does not retain or mutate the caller's copy)
    // - a long-standing Win32 API constness quirk, not a real writability
    // requirement. Copying kAcceleratorTable into a local mutable array
    // (rather than const_cast-ing the constexpr table directly) satisfies
    // the API's signature without a const_cast (CLAUDE.md's RAII/no-raw-cast
    // discipline, clang-tidy cppcoreguidelines-pro-type-const-cast).
    std::array<ACCEL, std::size(kAcceleratorTable)> mutableTable{};
    std::ranges::copy(kAcceleratorTable, mutableTable.begin());
    return platform::AcceleratorTableHandle(
        ::CreateAcceleratorTableW(mutableTable.data(), static_cast<int>(mutableTable.size())));
}

}  // namespace neomifes::app
