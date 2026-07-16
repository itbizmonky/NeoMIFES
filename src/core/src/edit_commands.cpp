#include "neomifes/core/edit_commands.h"

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

}  // namespace neomifes::core
