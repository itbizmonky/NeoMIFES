#pragma once

// selection_metrics (WI-07 step4) - pure metrics derived from
// SelectionModel, header-only in the same spirit as core::moveTextPos()
// (moving/measuring logic that doesn't need its own .cpp).

#include <algorithm>
#include <cstdint>

#include "neomifes/core/selection_model.h"

namespace neomifes::core {

// Sum of |position - anchor| across every cursor - the UTF-16 code-unit
// count a status-bar "N selected" display shows. 0 if every cursor has
// position == anchor (Cursor::hasSelection() false for all of them) - no
// special-casing needed since that case already contributes 0 to the sum.
[[nodiscard]] inline std::uint64_t totalSelectedLength(const SelectionModel& selection) noexcept {
    std::uint64_t total = 0;
    for (const Cursor& cursor : selection.cursors()) {
        const auto lo = std::min(cursor.position, cursor.anchor);
        const auto hi = std::max(cursor.position, cursor.anchor);
        total += (hi - lo);
    }
    return total;
}

}  // namespace neomifes::core
