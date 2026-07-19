#include "neomifes/core/edit_commands.h"

#include <cstdint>
#include <utility>

#include "neomifes/core/cumulative_shift_edit.h"
#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/document.h"

namespace neomifes::core {

// --- InsertTextCommand ------------------------------------------------------

InsertTextCommand::InsertTextCommand(document::TextPos pos, std::u16string text) noexcept
    : m_pos(pos), m_text(std::move(text)) {}

void InsertTextCommand::execute(ExecutionContext& ctx) {
    ctx.document().insertText(m_pos, m_text);
}

void InsertTextCommand::undo(ExecutionContext& ctx) {
    ctx.document().eraseRange(document::TextRange{.start = m_pos, .end = m_pos + m_text.size()});
}

std::size_t InsertTextCommand::weight() const noexcept {
    // Matches detailed_design.md sec.6.1's example formula. No consumer
    // exists until UndoStack bucketing lands (ADR-012).
    return (m_text.size() * 2) + 32;
}

// --- DeleteRangeCommand ------------------------------------------------------

DeleteRangeCommand::DeleteRangeCommand(document::TextRange range) noexcept : m_range(range) {}

void DeleteRangeCommand::execute(ExecutionContext& ctx) {
    m_deletedText = ctx.document().snapshot()->extract(m_range);
    ctx.document().eraseRange(m_range);
}

void DeleteRangeCommand::undo(ExecutionContext& ctx) {
    ctx.document().insertText(m_range.start, m_deletedText);
}

std::size_t DeleteRangeCommand::weight() const noexcept { return (m_deletedText.size() * 2) + 32; }

// --- ReplaceRangeCommand ------------------------------------------------------

ReplaceRangeCommand::ReplaceRangeCommand(document::TextRange range, std::u16string text) noexcept
    : m_range(range), m_newText(std::move(text)) {}

void ReplaceRangeCommand::execute(ExecutionContext& ctx) {
    m_oldText = ctx.document().snapshot()->extract(m_range);
    ctx.document().replaceRange(m_range, m_newText);
}

void ReplaceRangeCommand::undo(ExecutionContext& ctx) {
    ctx.document().replaceRange(
        document::TextRange{.start = m_range.start, .end = m_range.start + m_newText.size()}, m_oldText);
}

std::size_t ReplaceRangeCommand::weight() const noexcept {
    return ((m_oldText.size() + m_newText.size()) * 2) + 32;
}

// --- MultiCursorEditCommand ------------------------------------------------------

MultiCursorEditCommand::MultiCursorEditCommand(std::vector<PerCursorEdit> edits,
                                                std::vector<Cursor>         cursorsBefore)
    : m_edits(std::move(edits)), m_cursorsBefore(std::move(cursorsBefore)) {}

void MultiCursorEditCommand::execute(ExecutionContext& ctx) {
    applyEditsWithCumulativeShift(ctx.document(), m_edits, m_deletedTexts, m_currentStartAtExecute);

    // Edit i's final cursor position is unaffected by any later edit (they
    // are all at offsets >= this one's end, per applyEditsWithCumulativeShift's
    // ascending, non-overlapping contract), so this pass can run after all
    // edits have been applied.
    const std::size_t n = m_edits.size();
    m_cursorsAfterExecute.assign(n, Cursor{});
    for (std::size_t i = 0; i < n; ++i) {
        const document::TextPos finalPos = m_currentStartAtExecute[i] + m_edits[i].insertedText.size();
        m_cursorsAfterExecute[i]         = Cursor{.position  = finalPos,
                                                  .anchor    = finalPos,
                                                  .isPrimary = m_cursorsBefore[i].isPrimary};
    }
}

void MultiCursorEditCommand::undo(ExecutionContext& ctx) {
    undoEditsDescending(ctx.document(), m_edits, m_deletedTexts, m_currentStartAtExecute);
}

std::size_t MultiCursorEditCommand::weight() const noexcept {
    std::size_t total = 0;
    for (const PerCursorEdit& edit : m_edits) {
        total += (edit.insertedText.size() * 2) + 32;
    }
    return total;
}

}  // namespace neomifes::core
