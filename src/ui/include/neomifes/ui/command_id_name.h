#pragma once

// commandIdToString()/commandIdFromString() (WI-10) - a dotted string
// identifier for every remappable ui::CommandId, used as the key space for
// core::KeyBindings' bindings map (keybindings.json). Header-only: 34
// entries, a linear switch/if-chain is trivially cheap and this is never a
// hot path (consulted only when loading/saving keybindings.json or building
// the runtime dispatch tables, not per-keystroke - the per-keystroke path
// consults core::KeyBindings::chordsFor() directly with the already-known
// CommandId).
//
// Extends the dotted-namespace convention buildCommandRegistry() already
// uses for its 6 palette-registered CommandDescriptor::id values
// ("find.show", "edit.undo", ...) to the other 28 CommandIds that have no
// palette entry today (build_plan.md's WI-10 design point: keybindings.json
// keys should match CommandDescriptor::id for consistency). Deliberately
// NOT placed in src/app/ (unlike theme_settings.h's render<->core bridge):
// this mapping is a property of CommandId itself, has zero dependency on
// core:: or Win32, and doesn't cross any layering boundary that doesn't
// already exist (ui:: already doesn't depend on core::) - keeping it next
// to command_ids.h is the tighter-cohesion choice.

#include <algorithm>
#include <array>
#include <string_view>

#include "neomifes/ui/command_ids.h"

namespace neomifes::ui {

// Every CommandId enumerator that IS remappable per WI-10's scope: all 37
// non-None enumerators except About (menu-only, no keyboard path by
// design). Declaration order matches command_ids.h exactly - consumers that
// need a deterministic tie-break order (see
// app::resolveKeyBindingConflicts()) rely on THIS being enum-declaration
// order, not an arbitrary one.
inline constexpr std::array<CommandId, 36> kAllRemappableCommandIds{
    CommandId::FindShow,          CommandId::FindReplace,     CommandId::FindNext,
    CommandId::FindPrevious,      CommandId::GrepShow,        CommandId::CommandPaletteShow,
    CommandId::OutlineToggle,     CommandId::JsonTreeToggle,  CommandId::CsvGridToggle,
    CommandId::GotoLineShow,      CommandId::BookmarkToggle,  CommandId::BookmarkNext,
    CommandId::BookmarkPrevious,  CommandId::TagJump,         CommandId::Save,
    CommandId::SaveAs,            CommandId::Open,            CommandId::New,
    CommandId::TabNext,           CommandId::TabPrevious,     CommandId::TabClose,
    CommandId::TabSwitch1,        CommandId::TabSwitch2,      CommandId::TabSwitch3,
    CommandId::TabSwitch4,        CommandId::TabSwitch5,      CommandId::TabSwitch6,
    CommandId::TabSwitch7,        CommandId::TabSwitch8,      CommandId::TabSwitch9,
    CommandId::Copy,              CommandId::Cut,             CommandId::Paste,
    CommandId::Undo,              CommandId::Redo,            CommandId::ToggleOverwriteMode,
};

// nullptr-equivalent (empty string_view) for CommandId::None and
// CommandId::About - About is deliberately menu-only with no keyboard path
// (see command_ids.h's own comment), so it has no place in a keybinding
// config's key space.
[[nodiscard]] inline std::u16string_view commandIdToString(CommandId id) noexcept {
    switch (id) {
        case CommandId::Save: return u"file.save";
        case CommandId::SaveAs: return u"file.saveAs";
        case CommandId::Open: return u"file.open";
        case CommandId::New: return u"file.new";
        case CommandId::TabClose: return u"tab.close";
        case CommandId::TabNext: return u"tab.next";
        case CommandId::TabPrevious: return u"tab.previous";
        case CommandId::TabSwitch1: return u"tab.switch1";
        case CommandId::TabSwitch2: return u"tab.switch2";
        case CommandId::TabSwitch3: return u"tab.switch3";
        case CommandId::TabSwitch4: return u"tab.switch4";
        case CommandId::TabSwitch5: return u"tab.switch5";
        case CommandId::TabSwitch6: return u"tab.switch6";
        case CommandId::TabSwitch7: return u"tab.switch7";
        case CommandId::TabSwitch8: return u"tab.switch8";
        case CommandId::TabSwitch9: return u"tab.switch9";
        case CommandId::Copy: return u"edit.copy";
        case CommandId::Cut: return u"edit.cut";
        case CommandId::Paste: return u"edit.paste";
        case CommandId::Undo: return u"edit.undo";
        case CommandId::Redo: return u"edit.redo";
        case CommandId::ToggleOverwriteMode: return u"edit.toggleOverwriteMode";
        case CommandId::FindShow: return u"find.show";
        case CommandId::FindReplace: return u"find.replace";
        case CommandId::FindNext: return u"find.next";
        case CommandId::FindPrevious: return u"find.previous";
        case CommandId::GrepShow: return u"search.grep.show";
        case CommandId::CommandPaletteShow: return u"command.paletteShow";
        case CommandId::OutlineToggle: return u"view.outline.toggle";
        case CommandId::JsonTreeToggle: return u"view.jsonTree.toggle";
        case CommandId::CsvGridToggle: return u"view.csvGrid.toggle";
        case CommandId::GotoLineShow: return u"goto.line.show";
        case CommandId::BookmarkToggle: return u"bookmark.toggle";
        case CommandId::BookmarkNext: return u"bookmark.next";
        case CommandId::BookmarkPrevious: return u"bookmark.previous";
        case CommandId::TagJump: return u"navigate.tagJump";
        case CommandId::About:
        case CommandId::None:
            return u"";
    }
    return u"";  // unreachable
}

// Inverse of commandIdToString() - CommandId::None for any unrecognized/
// empty string (e.g. a keybindings.json key from a future CommandId this
// build doesn't know about yet, or a typo in a hand-edited file). Never
// fails.
//
// Deliberately implemented as a linear scan over kAllRemappableCommandIds
// calling commandIdToString() on each, rather than a second 34-way if/switch
// chain with its own copy of every string literal: commandIdToString()'s
// switch is already the single source of truth for the string<->id mapping,
// and a 34-entry if-chain here would both duplicate every literal (a typo
// risk the two functions could silently disagree on) and trip clang-tidy's
// cognitive-complexity check. O(34) string comparisons is negligible given
// this header's own top comment on call frequency (never per-keystroke).
[[nodiscard]] inline CommandId commandIdFromString(std::u16string_view name) noexcept {
    const auto it = std::ranges::find_if(
        kAllRemappableCommandIds, [name](CommandId id) { return commandIdToString(id) == name; });
    return it != kAllRemappableCommandIds.end() ? *it : CommandId::None;
}

}  // namespace neomifes::ui
