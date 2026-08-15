#pragma once

// Pure edit-plan builders for WI-12's line-oriented multi-cursor commands
// (Ctrl+D duplicate line / Alt+Up/Down move line / Ctrl+Shift+K delete
// line). Headless (no Win32/RenderPipeline dependency, same "core stays
// engine-only" convention as indentation_conversion.h) - the caller
// (normal_mode_wiring.cpp) turns the returned LineOperationPlan into a
// LineOperationCommand and dispatches it.

#include <cstdint>
#include <span>
#include <vector>

#include "neomifes/core/cursor.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/core/line_operation_command.h"

namespace neomifes::document {
class Document;
}

namespace neomifes::core {

// The (edits, cursorMappings) pair LineOperationCommand's constructor
// needs. `cursorMappings` always has exactly `cursors.size()` entries (the
// span passed to the compute*() functions below), in the same order - one
// per ORIGINAL cursor, even when multiple cursors collapse onto the same
// edit (e.g. two cursors on the same line for duplicate/delete-line).
// `edits` is empty when the whole operation is a no-op (e.g. every
// affected line-group is already at the document boundary a move would
// push it past) - callers should skip dispatching a command entirely in
// that case, same "no-op skips dispatch" convention
// editor_input.cpp's applyDeleteKey()/deleteAllSelections() already use.
struct LineOperationPlan {
    std::vector<PerCursorEdit>     edits;
    std::vector<CursorEditMapping> cursorMappings;
};

// Ctrl+D: duplicates every distinct line spanned by `cursors` (cursors
// sharing a line collapse to a single insertion), inserting the copy
// immediately below the original - Sublime/JetBrains "duplicate line"
// convention (build_plan.md's WI-12 key choice; note this differs from
// real VS Code's own Ctrl+D binding, which is unrelated - "Add Selection
// To Next Find Match"). Each cursor's post-execute position lands at the
// same column within its line's duplicate.
[[nodiscard]] LineOperationPlan computeDuplicateLineEdits(const document::Document& document,
                                                          std::span<const Cursor>   cursors);

// Ctrl+Shift+K: deletes every distinct line spanned by `cursors` entirely,
// including its own line break. A cursor whose line was deleted lands at
// column 0 of whatever content now occupies that position (the following
// line, shifted up) - or at the new document end if the deleted line(s)
// included the document's last line.
[[nodiscard]] LineOperationPlan computeDeleteLineEdits(const document::Document& document,
                                                       std::span<const Cursor>   cursors);

// Alt+Up / Alt+Down: swaps each maximal contiguous run of distinct lines
// spanned by `cursors` with the single adjacent line in the direction of
// travel (`moveDown` selects which). A run already at the document
// boundary in that direction is left untouched - not an error; other,
// movable runs in the same call still move. The whole plan is a no-op
// (`edits` empty) only when EVERY run is blocked this way. Every cursor
// keeps its column, shifted exactly one line in the direction its own run
// moved (or left unchanged if that run didn't move).
[[nodiscard]] LineOperationPlan computeMoveLineEdits(const document::Document& document,
                                                     std::span<const Cursor> cursors, bool moveDown);

}  // namespace neomifes::core
