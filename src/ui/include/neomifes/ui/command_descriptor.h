#pragma once

// CommandDescriptor - one entry in the command palette (Phase 5b3c). Lives
// in ui:: rather than core:: because `action` is a UI-facing callback
// supplied by src/app/main.cpp, the same relationship FindBarConfig's
// callbacks have to their domain logic (find_bar.h's file header).

#include <functional>
#include <string>

namespace neomifes::ui {

struct CommandDescriptor {
    std::u16string id;               // e.g. "find.show", not displayed
    std::u16string title;            // e.g. "Find", matched against by fuzzyMatchScore
    std::u16string keybindingLabel;  // display-only, e.g. "Ctrl+F"
    std::function<void()> action;
};

}  // namespace neomifes::ui
