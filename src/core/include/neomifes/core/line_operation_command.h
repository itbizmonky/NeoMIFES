#pragma once

// LineOperationCommand - WI-12's third cursor-repositioning policy,
// alongside MultiCursorEditCommand's strict 1:1-edit-per-cursor mapping and
// ReplaceAllCommand's "never moves the cursor" (see edit_commands.h /
// replace_all_command.h for those). Needed for line-oriented multi-cursor
// operations (duplicate/move/delete line, src/core/include/neomifes/core/
// line_operations.h) where cursors sharing a line collapse to ONE edit
// (edits.size() can be < cursorsBefore.size()) and the correct post-edit
// cursor position requires domain knowledge (which line a cursor ends up
// on, and where within that line's text) that the generic edit list alone
// doesn't carry - so the caller supplies it explicitly via
// `cursorMappings` instead of this class deriving it. Shares the same
// cumulative-shift apply/undo core as the other two via
// cumulative_shift_edit.h rather than duplicating the algorithm a third
// time.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/core/command.h"
#include "neomifes/core/cursor.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::core {

// Maps ONE original cursor (by index, same order as cursorsBefore) to a
// position within one particular edit's `insertedText`, resolved to an
// absolute document position only once execute() has actually applied the
// edits and learned (via applyEditsWithCumulativeShift()'s
// outStartsAtExecute) where each edit landed. `editIndex` indexes into the
// `edits`/cursorsAfterExecute-basis vector; `offsetIntoInsertedText` is a
// UTF-16 code-unit offset into that edit's own insertedText (0 for pure
// deletes, where insertedText is always empty).
struct CursorEditMapping {
    std::size_t   editIndex             = 0;
    std::uint32_t offsetIntoInsertedText = 0;
};

class LineOperationCommand final : public ICommand {
public:
    // `id` must have static storage duration (every call site passes a
    // string literal, e.g. "edit.duplicateLine") - stored as a
    // std::string_view, not copied.
    LineOperationCommand(std::vector<PerCursorEdit> edits, std::vector<Cursor> cursorsBefore,
                         std::vector<CursorEditMapping> cursorMappings, std::string_view id) noexcept;

    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    [[nodiscard]] std::size_t      weight() const noexcept override;
    [[nodiscard]] std::string_view id() const noexcept override { return m_id; }
    [[nodiscard]] std::vector<Cursor> cursorsAfterExecute() const override {
        return m_cursorsAfterExecute;
    }
    // Undo always restores cursorsBefore verbatim (same as
    // MultiCursorEditCommand) - a line operation's undo is always "put
    // every cursor back exactly where it was", unlike execute()'s cursor
    // placement, which needs the caller-supplied mapping.
    [[nodiscard]] std::vector<Cursor> cursorsAfterUndo() const override { return m_cursorsBefore; }

private:
    std::vector<PerCursorEdit>     m_edits;
    std::vector<Cursor>            m_cursorsBefore;
    std::vector<CursorEditMapping> m_cursorMappings;
    std::string_view               m_id;
    // Populated by execute(); see cumulative_shift_edit.h.
    std::vector<std::u16string>    m_deletedTexts;
    std::vector<document::TextPos> m_currentStartAtExecute;
    std::vector<Cursor>            m_cursorsAfterExecute;
};

}  // namespace neomifes::core
