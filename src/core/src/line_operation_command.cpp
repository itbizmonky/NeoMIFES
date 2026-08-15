#include "neomifes/core/line_operation_command.h"

#include "neomifes/core/cumulative_shift_edit.h"

namespace neomifes::core {

LineOperationCommand::LineOperationCommand(std::vector<PerCursorEdit> edits, std::vector<Cursor> cursorsBefore,
                                           std::vector<CursorEditMapping> cursorMappings,
                                           std::string_view id) noexcept
    : m_edits(std::move(edits)),
      m_cursorsBefore(std::move(cursorsBefore)),
      m_cursorMappings(std::move(cursorMappings)),
      m_id(id) {}

void LineOperationCommand::execute(ExecutionContext& ctx) {
    applyEditsWithCumulativeShift(ctx.document(), m_edits, m_deletedTexts, m_currentStartAtExecute);

    const std::size_t n = m_cursorMappings.size();
    m_cursorsAfterExecute.assign(n, Cursor{});
    for (std::size_t i = 0; i < n; ++i) {
        const CursorEditMapping& mapping = m_cursorMappings[i];
        const document::TextPos  pos     = m_currentStartAtExecute[mapping.editIndex] + mapping.offsetIntoInsertedText;
        m_cursorsAfterExecute[i]         = Cursor{.position  = pos,
                                                  .anchor    = pos,
                                                  .isPrimary = m_cursorsBefore[i].isPrimary};
    }
}

void LineOperationCommand::undo(ExecutionContext& ctx) {
    undoEditsDescending(ctx.document(), m_edits, m_deletedTexts, m_currentStartAtExecute);
}

std::size_t LineOperationCommand::weight() const noexcept {
    std::size_t total = 0;
    for (const PerCursorEdit& edit : m_edits) {
        total += (edit.insertedText.size() * 2) + 32;
    }
    return total;
}

}  // namespace neomifes::core
