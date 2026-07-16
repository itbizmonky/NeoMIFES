#include "neomifes/core/undo_stack.h"

#include <utility>

#include "neomifes/core/selection_model.h"

namespace neomifes::core {

void UndoStack::push(std::unique_ptr<ICommand> command) {
    m_undo.push_back(std::move(command));
    m_redo.clear();
}

bool UndoStack::undo(ExecutionContext& ctx) {
    if (m_undo.empty()) {
        return false;
    }
    std::unique_ptr<ICommand> command = std::move(m_undo.back());
    m_undo.pop_back();
    command->undo(ctx);
    // command is still valid here - only ownership moves into m_redo below,
    // the pointee is untouched by that move.
    ctx.selection().moveAllTo(command->cursorPositionAfterUndo());
    m_redo.push_back(std::move(command));
    return true;
}

bool UndoStack::redo(ExecutionContext& ctx) {
    if (m_redo.empty()) {
        return false;
    }
    std::unique_ptr<ICommand> command = std::move(m_redo.back());
    m_redo.pop_back();
    command->execute(ctx);
    ctx.selection().moveAllTo(command->cursorPositionAfterExecute());
    m_undo.push_back(std::move(command));
    return true;
}

}  // namespace neomifes::core
