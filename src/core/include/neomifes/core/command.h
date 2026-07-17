#pragma once

// ICommand / ExecutionContext - the Command pattern interface Editor Core
// commands implement, per detailed_design.md sec.2.2/6.1.
//
// ExecutionContext is new glue not named in the Phase 0 design sketch, but is
// the minimum required to satisfy both ICommand::execute(ExecutionContext&)
// and SelectionModel-aware commands: it bundles the Document a command
// mutates with the SelectionModel a command may need to update (e.g. moving
// the caret to the end of inserted text).

#include <cstddef>
#include <string_view>
#include <vector>

#include "neomifes/core/cursor.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::document {
class Document;
}

namespace neomifes::core {

class SelectionModel;

class ExecutionContext {
public:
    ExecutionContext(document::Document& document, SelectionModel& selection) noexcept
        : m_document(&document), m_selection(&selection) {}

    [[nodiscard]] document::Document& document() noexcept { return *m_document; }
    [[nodiscard]] SelectionModel&     selection() noexcept { return *m_selection; }

private:
    document::Document* m_document;
    SelectionModel*      m_selection;
};

class ICommand {
public:
    virtual ~ICommand() = default;

    virtual void execute(ExecutionContext&) = 0;
    virtual void undo(ExecutionContext&)    = 0;

    // Undo-storage cost estimate. Unused until Phase 4b+ introduces
    // UndoStack bucketing (see ADR-012) - implemented now because it is
    // part of ICommand's required surface (detailed_design.md sec.6.1).
    [[nodiscard]] virtual std::size_t weight() const noexcept = 0;
    [[nodiscard]] virtual std::string_view id() const noexcept = 0;

    // The full cursor set SelectionModel should hold right after execute()/
    // undo() has run. CommandDispatcher/UndoStack call
    // SelectionModel::setCursors() with these so every cursor (not just the
    // primary one) tracks edits without every input-handling call site
    // having to compute it itself (Phase 4b1, generalized to N cursors in
    // Phase 4b5a). Single-cursor commands return a single-element vector.
    [[nodiscard]] virtual std::vector<Cursor> cursorsAfterExecute() const = 0;
    [[nodiscard]] virtual std::vector<Cursor> cursorsAfterUndo() const    = 0;
};

}  // namespace neomifes::core
