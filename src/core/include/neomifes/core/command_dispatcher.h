#pragma once

// CommandDispatcher - the single entry point that executes a command and
// records it for undo. Not named in the Phase 0 design sketch, but required
// by construction: ICommand::execute()/UndoStack::push() are separate calls
// (push() takes an already-executed command, per undo_stack.h), so something
// has to sequence "execute, then record" for callers (Phase 4b's keyboard
// handlers will be the primary caller).

#include <memory>

#include "neomifes/core/command.h"
#include "neomifes/core/undo_stack.h"

namespace neomifes::document {
class Document;
}

namespace neomifes::core {

class SelectionModel;

class CommandDispatcher {
public:
    CommandDispatcher(document::Document& document, SelectionModel& selection) noexcept;

    // Executes `command` immediately, then records it on the undo stack.
    void dispatch(std::unique_ptr<ICommand> command);

    bool undo();
    bool redo();

    [[nodiscard]] bool canUndo() const noexcept { return m_undoStack.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return m_undoStack.canRedo(); }

private:
    ExecutionContext m_context;
    UndoStack        m_undoStack;
};

}  // namespace neomifes::core
