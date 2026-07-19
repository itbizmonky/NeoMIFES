#pragma once

// filterAndRankCommands - pure ranking logic for the command palette (Phase
// 5b3c). Header-only and free of Windows-SDK includes so it stays
// unit-testable without a live HWND, mirroring find_navigation.h/
// click_tracking.h's rationale - the Win32-mechanics class (command_palette.h)
// calls this rather than embedding the ranking logic itself.

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "neomifes/ui/command_descriptor.h"
#include "neomifes/util/fuzzy_matcher.h"

namespace neomifes::ui {

// Returns indices into `commands`, ranked by fuzzyMatchScore(query, title)
// descending; commands with no match at all are omitted. Ties keep
// `commands`' original relative order (std::stable_sort). An empty `query`
// scores every command 0, so this returns every index in original order.
[[nodiscard]] inline std::vector<std::size_t> filterAndRankCommands(
    std::u16string_view query, std::span<const CommandDescriptor> commands) {
    std::vector<std::pair<std::size_t, int>> scored;
    scored.reserve(commands.size());
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const auto score = neomifes::util::fuzzyMatchScore(query, commands[i].title);
        if (score.has_value()) {
            scored.emplace_back(i, *score);
        }
    }
    std::stable_sort(scored.begin(), scored.end(),
                      [](const auto& lhs, const auto& rhs) { return lhs.second > rhs.second; });

    std::vector<std::size_t> result;
    result.reserve(scored.size());
    for (const auto& [index, score] : scored) {
        result.push_back(index);
    }
    return result;
}

}  // namespace neomifes::ui
