#include "neomifes/app/keybinding_dispatch.h"

#include <algorithm>

namespace neomifes::app {

std::map<std::u16string, ui::CommandId> resolveKeyBindingConflicts(const core::KeyBindings& keyBindings) {
    std::map<std::u16string, ui::CommandId> winners;
    for (const ui::CommandId id : ui::kAllRemappableCommandIds) {
        const std::u16string_view chordId = ui::commandIdToString(id);
        if (chordId.empty()) {
            continue;
        }
        for (const auto& chordText : keyBindings.chordsFor(chordId)) {
            if (const auto chord = parseKeyChord(chordText)) {
                winners[keyChordToString(*chord)] = id;  // later enum-order id wins
            }
        }
    }
    return winners;
}

bool chordMatches(const core::KeyBindings& keyBindings, ui::CommandId commandId, bool ctrlDown, bool shiftDown,
                   bool altDown, UINT vkCode) {
    const std::u16string_view chordId = ui::commandIdToString(commandId);
    if (chordId.empty()) {
        return false;
    }
    const KeyChord pressed{.ctrl = ctrlDown, .shift = shiftDown, .alt = altDown, .virtualKey = vkCode};
    const auto     chords  = keyBindings.chordsFor(chordId);
    return std::ranges::any_of(chords, [&pressed](const std::u16string& chordText) {
        const auto chord = parseKeyChord(chordText);
        return chord && *chord == pressed;
    });
}

std::vector<ACCEL> buildAcceleratorRows(const core::KeyBindings& keyBindings) {
    const auto          winners = resolveKeyBindingConflicts(keyBindings);
    std::vector<ACCEL>  rows;
    for (const ui::CommandId id : kAcceleratorEligibleCommands) {
        const std::u16string_view chordId = ui::commandIdToString(id);
        for (const auto& chordText : keyBindings.chordsFor(chordId)) {
            const auto chord = parseKeyChord(chordText);
            if (!chord) {
                continue;
            }
            const auto winnerIt = winners.find(keyChordToString(*chord));
            if (winnerIt == winners.end() || winnerIt->second != id) {
                continue;  // lost conflict resolution to another command - emit no row
            }
            BYTE fVirt = FVIRTKEY;
            if (chord->ctrl) {
                fVirt |= FCONTROL;
            }
            if (chord->shift) {
                fVirt |= FSHIFT;
            }
            if (chord->alt) {
                fVirt |= FALT;
            }
            rows.push_back(ACCEL{fVirt, static_cast<WORD>(chord->virtualKey), static_cast<WORD>(id)});
        }
    }
    return rows;
}

}  // namespace neomifes::app
