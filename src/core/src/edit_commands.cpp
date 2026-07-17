#include "neomifes/core/edit_commands.h"

#include <cstdint>
#include <utility>

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
    const std::size_t n = m_edits.size();
    m_deletedTexts.assign(n, std::u16string{});
    m_currentStartAtExecute.assign(n, document::TextPos{0});
    m_cursorsAfterExecute.assign(n, Cursor{});

    // Ascending pass with a running cumulative shift: edits[i].range.start is
    // captured in the ORIGINAL (pre-command) document, so by the time we
    // reach edit i, every earlier edit (all at lower offsets, since
    // SelectionModel::cursors() is sorted and non-overlapping) has already
    // shifted the document by `cumulativeShift`. Edit i's own start in the
    // CURRENT document is therefore range.start + cumulativeShift, and edit
    // i's final cursor position is unaffected by any later edit (they are
    // all at offsets >= this one's end).
    std::int64_t cumulativeShift = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const PerCursorEdit& edit         = m_edits[i];
        const auto           currentStart = static_cast<document::TextPos>(
            static_cast<std::int64_t>(edit.range.start) + cumulativeShift);
        const document::TextRange currentRange{.start = currentStart,
                                               .end   = currentStart + edit.range.length()};
        m_deletedTexts[i]           = ctx.document().snapshot()->extract(currentRange);
        m_currentStartAtExecute[i]  = currentStart;
        ctx.document().replaceRange(currentRange, edit.insertedText);

        const document::TextPos finalPos = currentStart + edit.insertedText.size();
        m_cursorsAfterExecute[i] = Cursor{.position  = finalPos,
                                          .anchor    = finalPos,
                                          .isPrimary = m_cursorsBefore[i].isPrimary};
        cumulativeShift += static_cast<std::int64_t>(edit.insertedText.size()) -
                           static_cast<std::int64_t>(edit.range.length());
    }
}

void MultiCursorEditCommand::undo(ExecutionContext& ctx) {
    // Descending order: undoing the highest-offset edit first never shifts
    // the (lower) offsets the remaining undos still need, so
    // m_currentStartAtExecute[i] stays valid without recomputing a shift.
    for (std::size_t i = m_edits.size(); i-- > 0;) {
        const document::TextPos   start = m_currentStartAtExecute[i];
        const document::TextRange insertedRange{.start = start,
                                                .end    = start + m_edits[i].insertedText.size()};
        ctx.document().replaceRange(insertedRange, m_deletedTexts[i]);
    }
}

std::size_t MultiCursorEditCommand::weight() const noexcept {
    std::size_t total = 0;
    for (const PerCursorEdit& edit : m_edits) {
        total += (edit.insertedText.size() * 2) + 32;
    }
    return total;
}

}  // namespace neomifes::core
