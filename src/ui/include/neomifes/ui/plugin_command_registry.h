#pragma once

// PluginCommandRegistry (Phase 8f) - minimal, Win32/plugin_sdk-independent
// state layer behind NeoMifesCoreApi::registerCommand (see plugin_sdk.h).
// Reuses CommandDescriptor as-is (the same {id, title, keybindingLabel,
// action} shape CommandPalette itself stores) so a future sub-phase that
// wires this into the real CommandPalette can simply feed commands() into
// it - see ADR-020. Deliberately NOT wired into CommandPalette/main.cpp in
// this phase (headless-only, matching every prior Phase 8 sub-phase) -
// verified via real PluginHost + real sample DLL round-trip tests instead.

#include <algorithm>
#include <string_view>
#include <vector>

#include "neomifes/ui/command_descriptor.h"

namespace neomifes::ui {

class PluginCommandRegistry {
public:
    PluginCommandRegistry() noexcept = default;

    // Appends `descriptor`. No de-duplication by id - mirrors
    // CommandPalette::m_commands' own lack of de-dup logic; an intentional
    // simplification (CLAUDE.md rule 3), not an oversight.
    void registerCommand(CommandDescriptor descriptor) { m_commands.push_back(std::move(descriptor)); }

    // Removes every entry whose id equals `id`. No-op if none match.
    void unregisterCommand(std::u16string_view id) noexcept {
        std::erase_if(m_commands, [id](const CommandDescriptor& d) { return d.id == id; });
    }

    [[nodiscard]] const std::vector<CommandDescriptor>& commands() const noexcept { return m_commands; }

private:
    std::vector<CommandDescriptor> m_commands;
};

}  // namespace neomifes::ui
