#pragma once

// ReplaceAllCommand - applies N independent range-replace edits atomically
// as one undo step, per Phase 5b2 (置換). Deliberately does NOT reuse
// MultiCursorEditCommand: that class hard-assumes edits.size() ==
// cursorsBefore.size() (it indexes m_cursorsBefore[i] per edit i to compute
// each cursor's post-edit position) because it exists for genuine multi-
// cursor typing, where each cursor's caret should follow what it just
// typed. Replace-all's edit count (matches found) is unrelated to the
// cursor count (e.g. 1 cursor, 500 matches), and critically must NOT move
// the cursor/selection at all - see cursorsAfterExecute()/cursorsAfterUndo()
// below. The apply/undo algorithm itself is shared with MultiCursorEditCommand
// via cumulative_shift_edit.h rather than duplicated.
//
// Deliberately does NOT depend on search:: (no search::Match/search::Query
// in this header) - core:: stays decoupled from search:: until Phase 5b3
// actually links search into the app (see docs/history/TIMELINE.md's
// Phase 5b2 entry for why). The caller is responsible for turning
// search::Match + a replacement template into PerCursorEdit - see
// search::expandReplacementTemplate(), src/search/include/neomifes/search/replacement.h.

#include <cstddef>
#include <string_view>
#include <vector>

#include "neomifes/core/command.h"
#include "neomifes/core/cursor.h"
#include "neomifes/core/edit_commands.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::core {

class ReplaceAllCommand final : public ICommand {
public:
    ReplaceAllCommand(std::vector<PerCursorEdit> edits, std::vector<Cursor> cursorsBefore);

    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    [[nodiscard]] std::size_t      weight() const noexcept override;
    [[nodiscard]] std::string_view id() const noexcept override { return "edit.replaceAll"; }

    // Replace-all never moves the cursor/selection - both accessors return
    // the pre-existing snapshot verbatim (CommandDispatcher::dispatch()/
    // UndoStack::undo()/redo() unconditionally call SelectionModel::setCursors()
    // with this, so some value must be returned even though nothing moved).
    [[nodiscard]] std::vector<Cursor> cursorsAfterExecute() const override { return m_cursorsBefore; }
    [[nodiscard]] std::vector<Cursor> cursorsAfterUndo() const override { return m_cursorsBefore; }

private:
    std::vector<PerCursorEdit>     m_edits;
    std::vector<Cursor>            m_cursorsBefore;
    // Populated by execute(); see cumulative_shift_edit.h.
    std::vector<std::u16string>    m_deletedTexts;
    std::vector<document::TextPos> m_currentStartAtExecute;
};

}  // namespace neomifes::core
