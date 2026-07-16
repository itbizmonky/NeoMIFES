#pragma once

// UndoStack - push/undo/redo over ICommand, per detailed_design.md sec.6.2.
//
// Scope note (Phase 4a, see ADR-012): the design sketch's 1000-command
// bucketing + zstd compression + disk-swap-on-budget-overflow are deferred.
// This is a plain two-stack implementation; whether compression/disk-swap
// are actually needed is a question for the 1M-command benchmark
// (tests/bench/core_undo_stack_bench.cpp) and docs/issues/
// undo_stack_unbounded_memory.md to answer with real numbers, not
// speculation up front (CLAUDE.md rule 10).
//
// push() does not call execute() - the caller (CommandDispatcher) executes
// the command first and only pushes it once applied, matching the design
// sketch's push(unique_ptr<ICommand>) signature (no ExecutionContext
// parameter, unlike undo()/redo()).

#include <cstddef>
#include <memory>
#include <vector>

#include "neomifes/core/command.h"

namespace neomifes::core {

class UndoStack {
public:
    void push(std::unique_ptr<ICommand> command);

    // Returns false (no-op) if there is nothing to undo/redo.
    bool undo(ExecutionContext&);
    bool redo(ExecutionContext&);

    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
    [[nodiscard]] std::size_t undoCount() const noexcept { return m_undo.size(); }
    [[nodiscard]] std::size_t redoCount() const noexcept { return m_redo.size(); }

private:
    std::vector<std::unique_ptr<ICommand>> m_undo;
    std::vector<std::unique_ptr<ICommand>> m_redo;
};

}  // namespace neomifes::core
