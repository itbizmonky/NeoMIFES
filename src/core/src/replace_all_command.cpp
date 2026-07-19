#include "neomifes/core/replace_all_command.h"

#include <utility>

#include "neomifes/core/cumulative_shift_edit.h"

namespace neomifes::core {

ReplaceAllCommand::ReplaceAllCommand(std::vector<PerCursorEdit> edits,
                                      std::vector<Cursor>         cursorsBefore)
    : m_edits(std::move(edits)), m_cursorsBefore(std::move(cursorsBefore)) {}

void ReplaceAllCommand::execute(ExecutionContext& ctx) {
    applyEditsWithCumulativeShift(ctx.document(), m_edits, m_deletedTexts, m_currentStartAtExecute);
}

void ReplaceAllCommand::undo(ExecutionContext& ctx) {
    undoEditsDescending(ctx.document(), m_edits, m_deletedTexts, m_currentStartAtExecute);
}

std::size_t ReplaceAllCommand::weight() const noexcept {
    std::size_t total = 0;
    for (const PerCursorEdit& edit : m_edits) {
        total += (edit.insertedText.size() * 2) + 32;
    }
    return total;
}

}  // namespace neomifes::core
