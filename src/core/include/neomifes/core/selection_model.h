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
    PageUp,    // Phase 4b6a - moveAll()'s pageSize argument supplies the jump size
    PageDown,  // Phase 4b6a
    // Phase 4b6b - simple character-class word boundaries (same rule as
    // selectWordAt(), Phase 4b4). Crosses line boundaries (Phase 4b7b),
    // treating each line break (and any empty line) as whitespace - a
    // distinct stop at paragraph breaks is a separate, unimplemented
    // concern (see selection_model.cpp).
    WordLeft,
    WordRight,
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
    // `pageSize` (line count) is only consulted for PageUp/PageDown (Phase
    // 4b6a) - callers of the other MovementKinds don't need to pass it.
    void moveAll(MovementKind kind, const document::Document& doc, bool extendSelection,
                document::LineNumber pageSize = 0);

    // Drops every cursor but the primary one, collapsing its selection.
    void collapseToPrimary();

    // Sets every cursor's position to `position`. When extendSelection is
    // false (default), anchor is set to the same value (selection cleared) -
    // used after an edit or undo/redo moves the document out from under a
    // cursor's old offset (edit commands don't touch SelectionModel
    // themselves - see ICommand::cursorsAfterExecute()/cursorsAfterUndo()).
    // When true, anchor is left in place (Shift+click semantics, Phase 4b2),
    // mirroring moveAll's extendSelection parameter.
    void moveAllTo(document::TextPos position, bool extendSelection = false);

    // Replaces the entire cursor set (e.g. with ICommand::cursorsAfterExecute()/
    // cursorsAfterUndo() after a multi-cursor edit, Phase 4b5a). Triggers a
    // merge pass, same as addCursor() - two cursors whose edits landed on the
    // same final position collapse into one.
    void setCursors(std::vector<Cursor> cursors);

    // Replaces the entire cursor set with one cursor per line spanned by
    // `anchor`/`active` (Phase 4b8a) - "rectangular selection = one cursor
    // per row" (master_roadmap.md sec.3.3). Each row's column is
    // independently clamped to that row's actual content length (no
    // free-cursor/virtual-space support yet - deferred to a later Phase 4b8
    // sub-phase). `anchor`'s column always becomes that row's Cursor::anchor
    // and `active`'s column always becomes that row's Cursor::position
    // (never swapped by numeric ordering) - callers rely on this to draw the
    // live caret at the dragged-to column rather than jumping back to the
    // anchor's column once a drag crosses past it. No `SelectionMode` is
    // tracked; a subsequent moveAll()/moveAllTo() call naturally treats the
    // resulting cursors like any other multi-cursor set.
    void setRectangularSelection(document::TextPos anchor, document::TextPos active,
                                 const document::Document& doc);

    // Selects the "word" (or single punctuation character, or run of
    // whitespace) at `pos`, using simple character-class boundaries rather
    // than full Unicode word segmentation (Phase 4b4 - see ADR-012's
    // MovementUnit deferral; the user confirmed this simplified rule over
    // UAX #29). Applies to every cursor, same convention as moveAllTo.
    void selectWordAt(document::TextPos pos, const document::Document& doc);

    // Selects the entire line containing `pos`. Includes the trailing '\n'
    // when one exists (i.e. not the last line), so Backspace/Delete on the
    // resulting selection removes the line cleanly (Phase 4b4).
    void selectLineAt(document::TextPos pos, const document::Document& doc);

    // Extends exactly one cursor - the one whose anchor equals
    // `identifyingAnchor` - to `newPos`, leaving every other cursor
    // untouched (Phase 4b6d: Alt+Shift+click / Alt+drag extending the
    // cursor a prior Alt+click added, as opposed to moveAll()/moveAllTo(),
    // which always apply to every cursor uniformly). A cursor's anchor
    // stays fixed while its position moves during a single extend gesture,
    // so the caller can keep passing the same `identifyingAnchor` across
    // repeated calls (e.g. successive WM_MOUSEMOVE events) to keep
    // targeting the same cursor. A no-op if no cursor's anchor matches
    // (e.g. it merged into another cursor since - see mergeOverlapping()).
    void moveCursorMatching(document::TextPos identifyingAnchor, document::TextPos newPos);

    [[nodiscard]] std::span<const Cursor> cursors() const noexcept { return m_cursors; }
    [[nodiscard]] const Cursor&           primaryCursor() const noexcept;

private:
    void mergeOverlapping();

    std::vector<Cursor> m_cursors;  // always sorted & merged after any mutation
};

}  // namespace neomifes::core
