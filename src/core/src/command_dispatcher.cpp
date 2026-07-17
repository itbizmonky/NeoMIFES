#include "neomifes/core/command_dispatcher.h"

#include <utility>

#include "neomifes/core/selection_model.h"

namespace neomifes::core {

CommandDispatcher::CommandDispatcher(document::Document& document, SelectionModel& selection) noexcept
    : m_context(document, selection) {}

void CommandDispatcher::dispatch(std::unique_ptr<ICommand> command) {
    command->execute(m_context);
    m_context.selection().setCursors(command->cursorsAfterExecute());
    m_undoStack.push(std::move(command));
}

bool CommandDispatcher::undo() { return m_undoStack.undo(m_context); }

bool CommandDispatcher::redo() { return m_undoStack.redo(m_context); }

}  // namespace neomifes::core
