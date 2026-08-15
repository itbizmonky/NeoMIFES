#pragma once

// KeyChord + parseKeyChord()/keyChordToString() (WI-10) - a Win32 VK/
// modifier representation of a key combination, and conversion to/from the
// human-readable string form ("Ctrl+Shift+P") already used throughout
// normal_mode_wiring.cpp's CommandDescriptor::keybindingLabel literals and
// core::KeyBindings' persisted chord strings.
//
// Lives in src/app/ (not src/ui/ or src/platform/): Win32 accelerator-flag
// knowledge (FVIRTKEY/FCONTROL/FSHIFT/FALT, ACCEL) already lives exclusively
// in src/app/ (command_dispatch.h) - putting chord parsing here keeps every
// Win32-keyboard-specific piece of WI-10's mechanism in one place. Part of
// the neomifes_app_input library (not compiled directly into NeoMIFES.exe
// like command_dispatch.cpp) so tests/unit/ can exercise it headlessly, no
// HWND/window required - same reasoning as editor_input.cpp's own home in
// that library despite also touching Win32 VK_* constants.

#include <optional>
#include <string>
#include <string_view>

#include <windows.h>

namespace neomifes::app {

struct KeyChord {
    bool ctrl        = false;
    bool shift       = false;
    bool alt         = false;
    UINT virtualKey  = 0;  // 0 = invalid/unset

    friend bool operator==(const KeyChord&, const KeyChord&) = default;
};

// Parses a chord string ("Ctrl+Shift+P", case-insensitive modifier names,
// '+'-separated, exactly one trailing non-modifier token - a single
// alphanumeric character taken verbatim, or one of the named keys this
// module recognizes: F1-F12, Tab, Insert, Delete, Home, End, PageUp,
// PageDown, Enter, Escape, Space, Up, Down, Left, Right, Backspace).
// nullopt for anything unparseable (empty string, modifier-only, unknown
// key name, malformed structure) - callers (core::KeyBindings consumers)
// treat an unparseable stored chord as unbound, not a hard error, since
// KeyBindings::loadFrom() itself never fails outright.
[[nodiscard]] std::optional<KeyChord> parseKeyChord(std::u16string_view text);

// Exact inverse of parseKeyChord() for any KeyChord it could have produced -
// fixed "Ctrl+Shift+Alt+<Key>" modifier order, matching every existing
// keybindingLabel literal in buildCommandRegistry() today (all of which
// already happen to use this order). Returns an empty string if
// chord.virtualKey == 0 (nothing to format).
[[nodiscard]] std::u16string keyChordToString(const KeyChord& chord);

}  // namespace neomifes::app
