#include "neomifes/core/selection_model.h"

#include <algorithm>
#include <cassert>

#include "neomifes/document/document.h"

namespace neomifes::core {

namespace {

// Offset of the last content code unit + 1 for `line`, i.e. the position
// right before the '\n' that starts the next line (or document::length()
// for the last line). Derived purely from LineIndex's line-start contract
// (line_index.h: starts are '\n'-delimited only) - no snapshot/extract
// needed, and consistent with RenderPipeline's line splitting, which also
// only ever splits on '\n' (CR is left as trailing content, deferred to the
// Encoding Engine per Phase 6).
[[nodiscard]] document::TextPos lineContentEnd(const document::Document& doc,
                                                document::LineNumber      line) {
    if (line + 1 >= doc.lineCount()) {
        return doc.length();
    }
    return doc.lineToOffset(line + 1) - 1;
}

[[nodiscard]] document::TextPos moveVertically(const document::Document& doc,
                                                document::TextPos current, bool up) {
    const document::LineNumber currentLine = doc.offsetToLine(current);
    const document::TextPos    column      = current - doc.lineToOffset(currentLine);
    document::LineNumber       targetLine  = currentLine;
    if (up) {
        if (currentLine > 0) {
            targetLine = currentLine - 1;
        }
    } else if (currentLine + 1 < doc.lineCount()) {
        targetLine = currentLine + 1;
    }
    const document::TextPos targetLineStart = doc.lineToOffset(targetLine);
    const document::TextPos targetLineEnd   = lineContentEnd(doc, targetLine);
    return std::min(targetLineStart + column, targetLineEnd);
}

[[nodiscard]] document::TextPos moveOne(MovementKind kind, const document::Document& doc,
                                         document::TextPos position) {
    switch (kind) {
        case MovementKind::Left:
            return position > 0 ? position - 1 : 0;
        case MovementKind::Right:
            return position < doc.length() ? position + 1 : doc.length();
        case MovementKind::Up:
            return moveVertically(doc, position, /*up=*/true);
        case MovementKind::Down:
            return moveVertically(doc, position, /*up=*/false);
        case MovementKind::LineStart:
            return doc.lineToOffset(doc.offsetToLine(position));
        case MovementKind::LineEnd:
            return lineContentEnd(doc, doc.offsetToLine(position));
        case MovementKind::DocumentStart:
            return 0;
        case MovementKind::DocumentEnd:
            return doc.length();
    }
    assert(false && "unhandled MovementKind");
    return position;
}

}  // namespace

SelectionModel::SelectionModel(document::TextPos initialPosition) {
    m_cursors.push_back(Cursor{.position = initialPosition, .anchor = initialPosition, .isPrimary = true});
}

void SelectionModel::addCursor(document::TextPos position) {
    m_cursors.push_back(Cursor{.position = position, .anchor = position, .isPrimary = false});
    mergeOverlapping();
}

void SelectionModel::moveAll(MovementKind kind, const document::Document& doc,
                              bool extendSelection) {
    for (Cursor& cursor : m_cursors) {
        const document::TextPos newPosition = moveOne(kind, doc, cursor.position);
        cursor.position                     = newPosition;
        if (!extendSelection) {
            cursor.anchor = newPosition;
        }
    }
    mergeOverlapping();
}

void SelectionModel::collapseToPrimary() {
    Cursor primary  = primaryCursor();
    primary.anchor  = primary.position;
    m_cursors.assign(1, primary);
}

void SelectionModel::moveAllTo(document::TextPos position) {
    for (Cursor& cursor : m_cursors) {
        cursor.position = position;
        cursor.anchor   = position;
    }
    mergeOverlapping();
}

const Cursor& SelectionModel::primaryCursor() const noexcept {
    for (const Cursor& cursor : m_cursors) {
        if (cursor.isPrimary) {
            return cursor;
        }
    }
    return m_cursors.front();
}

void SelectionModel::mergeOverlapping() {
    if (m_cursors.size() <= 1) {
        return;
    }
    std::ranges::sort(m_cursors, {}, [](const Cursor& c) { return std::min(c.position, c.anchor); });

    std::vector<Cursor> merged;
    merged.reserve(m_cursors.size());
    for (const Cursor& cursor : m_cursors) {
        if (!merged.empty()) {
            Cursor&                 last     = merged.back();
            const document::TextPos lastEnd  = std::max(last.position, last.anchor);
            const document::TextPos curStart = std::min(cursor.position, cursor.anchor);
            if (curStart <= lastEnd) {
                const document::TextPos start = std::min({last.position, last.anchor, cursor.position, cursor.anchor});
                const document::TextPos end   = std::max({last.position, last.anchor, cursor.position, cursor.anchor});
                const bool               forward = last.position >= last.anchor;
                last.anchor    = forward ? start : end;
                last.position  = forward ? end : start;
                last.isPrimary = last.isPrimary || cursor.isPrimary;
                continue;
            }
        }
        merged.push_back(cursor);
    }
    m_cursors = std::move(merged);

    // Exactly one cursor must remain primary (defensive - collapses to the
    // first cursor if a merge dropped every isPrimary flag, which cannot
    // currently happen since merging ORs the flag, but guards future
    // movement kinds that might construct cursors differently).
    if (std::ranges::none_of(m_cursors, [](const Cursor& c) { return c.isPrimary; })) {
        m_cursors.front().isPrimary = true;
    }
}

}  // namespace neomifes::core
