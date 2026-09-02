#pragma once

// keybinding_dispatch (WI-10) - the generalized-dispatch layer core::
// KeyBindings feeds into. Two independent mechanisms this codebase already
// established (WI-07's command_dispatch.h/normal_mode_wiring.cpp) stay
// exactly as split as before - only WHICH CHORD triggers a given CommandId
// becomes data-driven, not the mechanism itself:
//
//   - kAcceleratorEligibleCommands (16 commands): routed through the real
//     Win32 accelerator table (TranslateAcceleratorW). buildAcceleratorRows()
//     is consulted ONCE whenever keybindings.json is loaded/reloaded/a
//     preset is switched (command_dispatch.h's buildAcceleratorTable()
//     wraps it in CreateAcceleratorTableW) - never per-keystroke.
//   - The other 20 commands: routed through normal_mode_wiring.cpp's
//     existing handle*Key() functions, each now calling chordMatches()
//     instead of a hardcoded `if (ctrlDown && vkCode == 'X')` literal
//     comparison. chordMatches() is called PER KEYSTROKE (it's inside the
//     WM_KEYDOWN chain), so it deliberately does the cheapest possible
//     check - a direct membership test against keyBindings.chordsFor(),
//     NOT a re-run of conflict resolution (see chordMatches()'s own comment
//     for why that's still correct, not merely fast).
//
// This split exists because TranslateAcceleratorW intercepts a matching
// WM_KEYDOWN BEFORE Win32's normal focus-based delivery reaches whichever
// widget (FindBar/GrepBar/CommandPalette/GotoLineBar, all native WC_EDIT
// controls) actually has keyboard focus - promoting the other 20 commands
// into the real accelerator table would silently break in-focus editing in
// those widgets (see command_dispatch.cpp's own comment for the concrete
// conflict WI-07 found). WI-10 does not revisit that constraint - which
// CommandIds are HACCEL-eligible remains a fixed architectural allowlist,
// NOT something a user's keybindings.json can change (a user remapping
// e.g. find.show onto a chord does not move FindShow into HACCEL - it just
// changes which chord chordMatches() looks for).

#include <array>
#include <map>
#include <string>
#include <vector>

#include <windows.h>

#include "neomifes/app/key_chord.h"
#include "neomifes/core/key_bindings.h"
#include "neomifes/ui/command_id_name.h"

namespace neomifes::app {

// The 17 CommandIds pre-WI-10's kAcceleratorTable (command_dispatch.h)
// already routed through the real Win32 accelerator mechanism, plus
// NewWindow (WI-20b - confirmed unclaimed by every preset and no overlay
// widget). Fixed, not user-configurable - see this header's own top
// comment.
inline constexpr std::array<ui::CommandId, 17> kAcceleratorEligibleCommands{
    ui::CommandId::Save,        ui::CommandId::SaveAs,      ui::CommandId::Open,
    ui::CommandId::New,         ui::CommandId::NewWindow,   ui::CommandId::TabClose,
    ui::CommandId::TabNext,     ui::CommandId::TabPrevious, ui::CommandId::TabSwitch1,
    ui::CommandId::TabSwitch2,  ui::CommandId::TabSwitch3,  ui::CommandId::TabSwitch4,
    ui::CommandId::TabSwitch5,  ui::CommandId::TabSwitch6,  ui::CommandId::TabSwitch7,
    ui::CommandId::TabSwitch8,  ui::CommandId::TabSwitch9,
};

// Maps each chord (in parseKeyChord()/keyChordToString()'s canonical string
// form, so "ctrl+s" and "Ctrl+S" collide to the same entry) to whichever
// CommandId claims it, across ALL 36 remappable commands - not just the 16
// HACCEL-eligible ones, since a user could bind an HACCEL-eligible command
// and a manual-chain command to the identical chord. Deterministic
// last-registered-wins, iterating ui::kAllRemappableCommandIds in its FIXED
// enum-declaration order (command_ids.h) - NOT keybindings.json key order
// (nlohmann::json's std::map-backed object dump is alphabetical, which
// would make "which command wins" silently depend on chord-id spelling
// rather than anything meaningful) and NOT core::KeyBindings' own internal
// map order either. A chord with no claimant at all simply has no entry.
//
// Called once whenever keybindings.json is loaded/reloaded/a preset is
// switched (by buildAcceleratorRows() below) - NOT per-keystroke. See
// key_bindings.cpp's own header comment: conflict surfacing is
// Debug-build-only OutputDebugStringW logging, no blocking UI exists for
// this (no toast/dialog infrastructure is wired to keybinding changes).
[[nodiscard]] std::map<std::u16string, ui::CommandId> resolveKeyBindingConflicts(
    const core::KeyBindings& keyBindings);

// Per-keystroke check used by normal_mode_wiring.cpp's handle*Key()
// functions (the 20 commands NOT in kAcceleratorEligibleCommands): true iff
// (ctrlDown, shiftDown, altDown, vkCode) matches one of commandId's
// configured chords. Deliberately does NOT re-run resolveKeyBindingConflicts()
// - a plain membership test is correct here (not merely a shortcut) because:
// (a) if an HACCEL-eligible command's chord collided with this one,
// buildAcceleratorRows() already omitted that HACCEL row for exactly this
// reason, so TranslateAcceleratorW never intercepts it and this WM_KEYDOWN
// reaches here in the first place; (b) if a DIFFERENT manual-chain command
// also claims the identical chord, handleKeyDownEvent()'s own fixed call
// order (unchanged by WI-10) is the tie-break, exactly as it already was
// before any command became remappable.
[[nodiscard]] bool chordMatches(const core::KeyBindings& keyBindings, ui::CommandId commandId, bool ctrlDown,
                                 bool shiftDown, bool altDown, UINT vkCode);

// One ACCEL row per (chord, CommandId) pair among kAcceleratorEligibleCommands
// that actually WON conflict resolution for that chord - a command that lost
// (to another HACCEL-eligible command or to one of the other 18) contributes
// no row, letting whichever mechanism actually owns that chord receive it.
// Pure logic, no CreateAcceleratorTableW call here (see command_dispatch.h's
// buildAcceleratorTable(), the thin Win32-calling wrapper around this).
[[nodiscard]] std::vector<ACCEL> buildAcceleratorRows(const core::KeyBindings& keyBindings);

}  // namespace neomifes::app
