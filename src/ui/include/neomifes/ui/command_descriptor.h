#pragma once

// CommandDescriptor - one entry in the command palette (Phase 5b3c). Lives
// in ui:: rather than core:: because `action` is a UI-facing callback
// supplied by src/app/main.cpp, the same relationship FindDialogConfig's
// callbacks have to their domain logic (find_dialog.h's file header).

#include <functional>
#include <string>

#include "neomifes/ui/command_ids.h"

namespace neomifes::ui {

struct CommandDescriptor {
    std::u16string id;               // e.g. "find.show", not displayed
    std::u16string title;            // e.g. "Find", matched against by fuzzyMatchScore
    std::u16string keybindingLabel;  // display-only, e.g. "Ctrl+F"
    // WI-07 step1: CommandId::None unless this command is ALSO reachable via
    // a keyboard shortcut (i.e. has a real keybindingLabel) - a palette-only
    // entry (e.g. "Convert Tabs to Spaces") has no accelerator-table row to
    // bridge to, so it stays None. See command_ids.h for why a numeric id is
    // needed at all.
    CommandId commandId = CommandId::None;
    std::function<void()> action;
};

}  // namespace neomifes::ui
