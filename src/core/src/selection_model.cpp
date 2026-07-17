#include "neomifes/core/selection_model.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "neomifes/document/buffer_snapshot.h"
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

// `lineDelta` generalizes the old up/bool parameter to an arbitrary line
// count (negative = up, positive = down) so Up/Down (delta = ±1, Phase 4a)
// and PageUp/PageDown (delta = ±pageSize, Phase 4b6a) share one column-
// preserving implementation.
[[nodiscard]] document::TextPos moveVertically(const document::Document& doc,
                                                document::TextPos current,
                                                std::int64_t       lineDelta) {
    const document::LineNumber currentLine = doc.offsetToLine(current);
    const document::TextPos    column      = current - doc.lineToOffset(currentLine);
    const document::LineNumber lastLine    = doc.lineCount() > 0 ? doc.lineCount() - 1 : 0;
    document::LineNumber       targetLine  = currentLine;
    if (lineDelta < 0) {
        const auto up = static_cast<document::LineNumber>(-lineDelta);
        targetLine     = (currentLine >= up) ? currentLine - up : 0;
    } else if (lineDelta > 0) {
        const auto down = static_cast<document::LineNumber>(lineDelta);
        targetLine       = std::min(currentLine + down, lastLine);
    }
    const document::TextPos targetLineStart = doc.lineToOffset(targetLine);
    const document::TextPos targetLineEnd   = lineContentEnd(doc, targetLine);
    return std::min(targetLineStart + column, targetLineEnd);
}

[[nodiscard]] document::TextPos moveOne(MovementKind kind, const document::Document& doc,
                                         document::TextPos position, document::LineNumber pageSize) {
    switch (kind) {
        case MovementKind::Left:
            return position > 0 ? position - 1 : 0;
        case MovementKind::Right:
            return position < doc.length() ? position + 1 : doc.length();
        case MovementKind::Up:
            return moveVertically(doc, position, -1);
        case MovementKind::Down:
            return moveVertically(doc, position, 1);
        case MovementKind::LineStart:
            return doc.lineToOffset(doc.offsetToLine(position));
        case MovementKind::LineEnd:
            return lineContentEnd(doc, doc.offsetToLine(position));
        case MovementKind::DocumentStart:
            return 0;
        case MovementKind::DocumentEnd:
            return doc.length();
        case MovementKind::PageUp:
            return moveVertically(doc, position, -static_cast<std::int64_t>(pageSize));
        case MovementKind::PageDown:
            return moveVertically(doc, position, static_cast<std::int64_t>(pageSize));
    }
    assert(false && "unhandled MovementKind");
    return position;
}

// Simple character-class boundaries for word selection (Phase 4b4, user
// confirmed over full Unicode UAX #29 segmentation - see ADR-012's
// MovementUnit deferral). ASCII alnum/underscore and CJK ranges (hiragana,
// katakana, CJK unified ideographs, halfwidth/fullwidth forms) count as
// "word" characters and merge into runs; everything else that isn't
// whitespace is its own single-character "word" (so punctuation is
// selectable individually, matching most editors' double-click behavior).
enum class CharKind : std::uint8_t { Word, Whitespace, Other };

[[nodiscard]] CharKind classify(char16_t ch) noexcept {
    if (ch == u' ' || ch == u'\t' || ch == u'\r' || ch == u'\n') {
        return CharKind::Whitespace;
    }
    const bool isAsciiWord = (ch >= u'a' && ch <= u'z') || (ch >= u'A' && ch <= u'Z') ||
                             (ch >= u'0' && ch <= u'9') || ch == u'_';
    const bool isCjk = (ch >= 0x3040 && ch <= 0x30FF) ||   // hiragana + katakana
                       (ch >= 0x4E00 && ch <= 0x9FFF) ||   // CJK unified ideographs
                       (ch >= 0xFF00 && ch <= 0xFFEF);     // halfwidth/fullwidth forms
    return (isAsciiWord || isCjk) ? CharKind::Word : CharKind::Other;
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
                              bool extendSelection, document::LineNumber pageSize) {
    for (Cursor& cursor : m_cursors) {
        const document::TextPos newPosition = moveOne(kind, doc, cursor.position, pageSize);
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

void SelectionModel::moveAllTo(document::TextPos position, bool extendSelection) {
    for (Cursor& cursor : m_cursors) {
        cursor.position = position;
        if (!extendSelection) {
            cursor.anchor = position;
        }
    }
    mergeOverlapping();
}

void SelectionModel::setCursors(std::vector<Cursor> cursors) {
    m_cursors = std::move(cursors);
    mergeOverlapping();
}

void SelectionModel::selectWordAt(document::TextPos pos, const document::Document& doc) {
    const document::LineNumber line      = doc.offsetToLine(pos);
    const document::TextPos    lineStart = doc.lineToOffset(line);
    const document::TextPos    lineEnd   = lineContentEnd(doc, line);
    if (lineStart >= lineEnd) {
        moveAllTo(pos);  // empty line - nothing to select
        return;
    }
    const std::u16string lineText =
        doc.snapshot()->extract(document::TextRange{.start = lineStart, .end = lineEnd});

    // Clamp the click column into the line's bounds - a click at lineEnd
    // (the very end of the line) must look at the last character, not one
    // past it.
    const document::TextPos clamped = std::min(pos, lineEnd);
    auto                    col     = static_cast<std::size_t>(clamped - lineStart);
    if (col >= lineText.size()) {
        col = lineText.size() - 1;
    }

    const CharKind kind = classify(lineText[col]);
    std::size_t    start = col;
    while (start > 0 && classify(lineText[start - 1]) == kind) {
        --start;
    }
    std::size_t end = col + 1;
    while (end < lineText.size() && classify(lineText[end]) == kind) {
        ++end;
    }

    for (Cursor& cursor : m_cursors) {
        cursor.anchor   = lineStart + start;
        cursor.position = lineStart + end;
    }
    mergeOverlapping();
}

void SelectionModel::selectLineAt(document::TextPos pos, const document::Document& doc) {
    const document::LineNumber line      = doc.offsetToLine(pos);
    const document::TextPos    lineStart = doc.lineToOffset(line);
    const document::TextPos    lineEnd   = (line + 1 < doc.lineCount())
                                               ? doc.lineToOffset(line + 1)  // includes the trailing '\n'
                                               : lineContentEnd(doc, line);  // last line: no '\n' to include

    for (Cursor& cursor : m_cursors) {
        cursor.anchor   = lineStart;
        cursor.position = lineEnd;
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
