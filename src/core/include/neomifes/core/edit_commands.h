#pragma once

// Basic edit commands, per detailed_design.md sec.6.1's edit.insert/delete/
// replace. tryMerge() (consecutive-input packing) is intentionally not
// implemented yet - see ADR-012: there is no real keyboard input (Phase 4b)
// to validate a merge heuristic against, so adding it now would be
// speculative (CLAUDE.md rule 3).

#include <cstddef>
#include <string>
#include <string_view>

#include "neomifes/core/command.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::core {

class InsertTextCommand final : public ICommand {
public:
    InsertTextCommand(document::TextPos pos, std::u16string text) noexcept;

    void execute(ExecutionContext&) override;
    void undo(ExecutionContext&) override;
    [[nodiscard]] std::size_t      weight() const noexcept override;
    [[nodiscard]] std::string_view id() const noexcept override { return "edit.insert"; }
    [[nodiscard]] document::TextPos cursorPositionAfterExecute() const noexcept override {
        return m_pos + m_text.size();
    }
    [[nodiscard]] document::TextPos cursorPositionAfterUndo() const noexcept override { return m_pos; }

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
    [[nodiscard]] document::TextPos cursorPositionAfterExecute() const noexcept override {
        return m_range.start;
    }
    [[nodiscard]] document::TextPos cursorPositionAfterUndo() const noexcept override {
        return m_range.start + m_deletedText.size();
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
    [[nodiscard]] document::TextPos cursorPositionAfterExecute() const noexcept override {
        return m_range.start + m_newText.size();
    }
    [[nodiscard]] document::TextPos cursorPositionAfterUndo() const noexcept override {
        return m_range.start + m_oldText.size();
    }

private:
    document::TextRange m_range;
    std::u16string       m_newText;
    // Captured by execute(), see DeleteRangeCommand::m_deletedText.
    std::u16string m_oldText;
};

}  // namespace neomifes::core
