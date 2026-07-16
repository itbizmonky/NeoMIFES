#pragma once

// SelectionModel - the set of carets (Cursor) for a Document, per
// detailed_design.md sec.5.1.
//
// Scope note (Phase 4a, see ADR-012): MovementKind covers only
// character/line-granularity movement (no word/paragraph units - that needs
// a word-boundary tokenizer that does not exist yet, and rectangular
// selection - no highlight rendering exists yet to make it observable).

#include <cstdint>
#include <span>
#include <vector>

#include "neomifes/core/cursor.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::document {
class Document;
}

namespace neomifes::core {

enum class MovementKind : std::uint8_t {
    Left,
    Right,
    Up,
    Down,
    LineStart,
    LineEnd,
    DocumentStart,
    DocumentEnd,
};

class SelectionModel {
public:
    explicit SelectionModel(document::TextPos initialPosition = 0);

    // Adds a new, non-primary cursor at `position` (no selection). Triggers
    // a merge pass, so a position that lands inside/adjacent to an existing
    // cursor's range is absorbed rather than duplicated.
    void addCursor(document::TextPos position);

    // Applies `kind` to every cursor. When `extendSelection` is false, each
    // cursor's anchor collapses to the new position (selection cleared);
    // when true, the anchor is left in place (Shift+move semantics).
    void moveAll(MovementKind kind, const document::Document& doc, bool extendSelection);

    // Drops every cursor but the primary one, collapsing its selection.
    void collapseToPrimary();

    // Sets every cursor's position AND anchor to `position` (selection
    // cleared), then re-merges. Used after an edit or undo/redo moves the
    // document out from under a cursor's old offset (edit commands don't
    // touch SelectionModel themselves - see ICommand::cursorPositionAfter*).
    void moveAllTo(document::TextPos position);

    [[nodiscard]] std::span<const Cursor> cursors() const noexcept { return m_cursors; }
    [[nodiscard]] const Cursor&           primaryCursor() const noexcept;

private:
    void mergeOverlapping();

    std::vector<Cursor> m_cursors;  // always sorted & merged after any mutation
};

}  // namespace neomifes::core
