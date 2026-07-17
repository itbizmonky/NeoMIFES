#pragma once

// Basic edit commands, per detailed_design.md sec.6.1's edit.insert/delete/
// replace. tryMerge() (consecutive-input packing) is intentionally not
// implemented yet - see ADR-012: there is no real keyboard input (Phase 4b)
// to validate a merge heuristic against, so adding it now would be
// speculative (CLAUDE.md rule 3).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "neomifes/core/command.h"
#include "neomifes/core/cursor.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::core {

class InsertTextCommand final : public ICommand {
public:
    InsertTextCommand(document::TextPos pos, std::u16string text) noexcept;

    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    [[nodiscard]] std::size_t      weight() const noexcept override;
    [[nodiscard]] std::string_view id() const noexcept override { return "edit.insert"; }
    [[nodiscard]] std::vector<Cursor> cursorsAfterExecute() const override {
        const document::TextPos pos = m_pos + m_text.size();
        return {Cursor{.position = pos, .anchor = pos, .isPrimary = true}};
    }
    [[nodiscard]] std::vector<Cursor> cursorsAfterUndo() const override {
        return {Cursor{.position = m_pos, .anchor = m_pos, .isPrimary = true}};
    }

private:
    document::TextPos m_pos;
    std::u16string     m_text;
};

class DeleteRangeCommand final : public ICommand {
public:
    explicit DeleteRangeCommand(document::TextRange range) noexcept;

    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    [[nodiscard]] std::size_t      weight() const noexcept override;
    [[nodiscard]] std::string_view id() const noexcept override { return "edit.delete"; }
    [[nodiscard]] std::vector<Cursor> cursorsAfterExecute() const override {
        return {Cursor{.position = m_range.start, .anchor = m_range.start, .isPrimary = true}};
    }
    [[nodiscard]] std::vector<Cursor> cursorsAfterUndo() const override {
        const document::TextPos pos = m_range.start + m_deletedText.size();
        return {Cursor{.position = pos, .anchor = pos, .isPrimary = true}};
    }

private:
    document::TextRange m_range;
    // Captured by execute() (read from the Document just before erasing) so
    // undo() can restore it. Empty until execute() has run once.
    std::u16string m_deletedText;
};

class ReplaceRangeCommand final : public ICommand {
public:
    ReplaceRangeCommand(document::TextRange range, std::u16string text) noexcept;

    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    [[nodiscard]] std::size_t      weight() const noexcept override;
    [[nodiscard]] std::string_view id() const noexcept override { return "edit.replace"; }
    [[nodiscard]] std::vector<Cursor> cursorsAfterExecute() const override {
        const document::TextPos pos = m_range.start + m_newText.size();
        return {Cursor{.position = pos, .anchor = pos, .isPrimary = true}};
    }
    [[nodiscard]] std::vector<Cursor> cursorsAfterUndo() const override {
        const document::TextPos pos = m_range.start + m_oldText.size();
        return {Cursor{.position = pos, .anchor = pos, .isPrimary = true}};
    }

private:
    document::TextRange m_range;
    std::u16string       m_newText;
    // Captured by execute(), see DeleteRangeCommand::m_deletedText.
    std::u16string m_oldText;
};

// A single cursor's contribution to a MultiCursorEditCommand: replace `range`
// (possibly empty, i.e. a pure insert) with `insertedText` (possibly empty,
// i.e. a pure delete).
struct PerCursorEdit {
    document::TextRange range;
    std::u16string        insertedText;
};

// Applies one edit per cursor atomically as a single undo step, per Phase
// 4b5a - see docs/history/TIMELINE.md's Phase 4b5a entry for the cumulative-
// offset derivation. `edits` must be supplied in the same ascending, non-
// overlapping order as SelectionModel::cursors() (mergeOverlapping()
// guarantees this) - the command does not re-sort them. `cursorsBefore` is a
// verbatim snapshot of SelectionModel::cursors() at construction time, used
// to restore undo() exactly (including any selections cursors had before the
// edit) rather than recomputing it from the edit ranges.
class MultiCursorEditCommand final : public ICommand {
public:
    MultiCursorEditCommand(std::vector<PerCursorEdit> edits, std::vector<Cursor> cursorsBefore);

    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    [[nodiscard]] std::size_t      weight() const noexcept override;
    [[nodiscard]] std::string_view id() const noexcept override { return "edit.multiCursor"; }
    [[nodiscard]] std::vector<Cursor> cursorsAfterExecute() const override {
        return m_cursorsAfterExecute;
    }
    [[nodiscard]] std::vector<Cursor> cursorsAfterUndo() const override { return m_cursorsBefore; }

private:
    std::vector<PerCursorEdit> m_edits;
    std::vector<Cursor>         m_cursorsBefore;
    // Populated by execute(): the text removed by edit i (for undo), and the
    // document position where edit i's insertedText actually starts once
    // earlier (lower-offset) edits' length changes have shifted it.
    std::vector<std::u16string>    m_deletedTexts;
    std::vector<document::TextPos> m_currentStartAtExecute;
    std::vector<Cursor>            m_cursorsAfterExecute;
};

}  // namespace neomifes::core
