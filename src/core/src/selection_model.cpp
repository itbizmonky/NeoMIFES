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

// Simple character-class boundaries, shared by moveByWordForward()/
// moveByWordBackward() (Ctrl+Left/Right, Phase 4b6b/4b7b) and selectWordAt()
// (double-click, Phase 4b4 - see
// ADR-012's MovementUnit deferral for why this simplified rule was chosen
// over full Unicode UAX #29 segmentation). ASCII alnum/underscore and CJK
// ranges (hiragana, katakana, CJK unified ideographs, halfwidth/fullwidth
// forms) count as "word" characters and merge into runs; everything else
// that isn't whitespace is its own single-character "word" (so punctuation
// is selectable/skippable individually, matching most editors).
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

// Ctrl+Left/Right (Phase 4b6b, cross-line since Phase 4b7b): skip to the
// start of the previous/next "word", using the same simple character-class
// rule as selectWordAt() (Phase 4b4). classify() already treats '\n' as
// whitespace within a single line's extracted text; these helpers extend
// that to the boundary BETWEEN lines (which classify() never directly sees,
// since each line is extracted separately) by continuing the whitespace
// skip onto the next/previous line's content. An empty line counts as one
// line-break's worth of whitespace and is skipped over rather than treated
// as its own stop - a distinct "stop at paragraph breaks" behavior belongs
// to paragraph movement, a separate, unimplemented concern (see
// MovementKind's doc comment).

[[nodiscard]] document::TextPos skipWhitespaceForward(const document::Document& doc,
                                                       document::TextPos         position) {
    for (;;) {
        const document::LineNumber line      = doc.offsetToLine(position);
        const document::TextPos    lineStart = doc.lineToOffset(line);
        const document::TextPos    lineEnd   = lineContentEnd(doc, line);
        if (lineStart >= lineEnd) {
            if (line + 1 >= doc.lineCount()) {
                return position;  // empty line, and it's the last one
            }
            position = doc.lineToOffset(line + 1);
            continue;
        }
        const std::u16string lineText =
            doc.snapshot()->extract(document::TextRange{.start = lineStart, .end = lineEnd});
        auto col = static_cast<std::size_t>(position - lineStart);
        while (col < lineText.size() && classify(lineText[col]) == CharKind::Whitespace) {
            ++col;
        }
        if (col < lineText.size()) {
            return lineStart + col;
        }
        if (line + 1 >= doc.lineCount()) {
            return lineEnd;
        }
        position = doc.lineToOffset(line + 1);
    }
}

[[nodiscard]] document::TextPos skipWhitespaceBackward(const document::Document& doc,
                                                        document::TextPos         position) {
    for (;;) {
        const document::LineNumber line      = doc.offsetToLine(position);
        const document::TextPos    lineStart = doc.lineToOffset(line);
        const document::TextPos    lineEnd   = lineContentEnd(doc, line);
        if (lineStart >= lineEnd) {
            if (lineStart == 0) {
                return position;  // empty line, and it's the first one
            }
            position = lineStart - 1;  // step onto the previous line's trailing '\n'
            continue;
        }
        const std::u16string lineText =
            doc.snapshot()->extract(document::TextRange{.start = lineStart, .end = lineEnd});
        auto col = static_cast<std::size_t>(std::min(position, lineEnd) - lineStart);
        while (col > 0 && classify(lineText[col - 1]) == CharKind::Whitespace) {
            --col;
        }
        if (col > 0) {
            return lineStart + col;
        }
        if (lineStart == 0) {
            return lineStart;
        }
        position = lineStart - 1;
    }
}

[[nodiscard]] document::TextPos moveByWordForward(const document::Document& doc,
                                                   document::TextPos         position) {
    const document::LineNumber line      = doc.offsetToLine(position);
    const document::TextPos    lineStart = doc.lineToOffset(line);
    const document::TextPos    lineEnd   = lineContentEnd(doc, line);
    if (lineStart < lineEnd) {
        const std::u16string lineText =
            doc.snapshot()->extract(document::TextRange{.start = lineStart, .end = lineEnd});
        auto col = static_cast<std::size_t>(std::min(position, lineEnd) - lineStart);
        if (col < lineText.size() && classify(lineText[col]) != CharKind::Whitespace) {
            const CharKind kind = classify(lineText[col]);
            while (col < lineText.size() && classify(lineText[col]) == kind) {
                ++col;
            }
        }
        position = lineStart + col;
    }
    return skipWhitespaceForward(doc, position);
}

[[nodiscard]] document::TextPos moveByWordBackward(const document::Document& doc,
                                                    document::TextPos         position) {
    position = skipWhitespaceBackward(doc, position);
    const document::LineNumber line      = doc.offsetToLine(position);
    const document::TextPos    lineStart = doc.lineToOffset(line);
    const document::TextPos    lineEnd   = lineContentEnd(doc, line);
    if (lineStart >= lineEnd) {
        return position;  // landed on an empty line - nothing to consume
    }
    const std::u16string lineText =
        doc.snapshot()->extract(document::TextRange{.start = lineStart, .end = lineEnd});
    auto col = static_cast<std::size_t>(position - lineStart);
    if (col > 0) {
        const CharKind kind = classify(lineText[col - 1]);
        while (col > 0 && classify(lineText[col - 1]) == kind) {
            --col;
        }
    }
    return lineStart + col;
}

}  // namespace

document::TextPos moveTextPos(MovementKind kind, const document::Document& doc,
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
        case MovementKind::WordLeft:
            return moveByWordBackward(doc, position);
        case MovementKind::WordRight:
            return moveByWordForward(doc, position);
    }
    assert(false && "unhandled MovementKind");
    return position;
}

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
        const document::TextPos newPosition = moveTextPos(kind, doc, cursor.position, pageSize);
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

void SelectionModel::setRectangularSelection(document::TextPos anchor, document::TextPos active,
                                             const document::Document& doc) {
    const document::LineNumber anchorLine = doc.offsetToLine(anchor);
    const document::LineNumber activeLine = doc.offsetToLine(active);
    const document::TextPos    anchorCol  = anchor - doc.lineToOffset(anchorLine);
    const document::TextPos    activeCol  = active - doc.lineToOffset(activeLine);
    const document::LineNumber startLine  = std::min(anchorLine, activeLine);
    const document::LineNumber endLine    = std::max(anchorLine, activeLine);

    std::vector<Cursor> cursors;
    cursors.reserve(endLine - startLine + 1);
    for (document::LineNumber line = startLine; line <= endLine; ++line) {
        const document::TextPos lineStart  = doc.lineToOffset(line);
        const document::TextPos lineLength = lineContentEnd(doc, line) - lineStart;
        // Independently clamped, never swapped by min/max - anchorCol always
        // feeds Cursor::anchor and activeCol always feeds Cursor::position,
        // matching the rest of this codebase's convention that a drag only
        // ever moves `position`, leaving `anchor` fixed.
        const document::TextPos rowAnchorCol = std::min(anchorCol, lineLength);
        const document::TextPos rowActiveCol = std::min(activeCol, lineLength);
        cursors.push_back(Cursor{.position  = lineStart + rowActiveCol,
                                 .anchor    = lineStart + rowAnchorCol,
                                 .isPrimary = (line == activeLine)});
    }
    setCursors(std::move(cursors));
}

void SelectionModel::convertToLineEndCursors(const document::Document& doc) {
    document::LineNumber minLine = doc.offsetToLine(std::min(m_cursors.front().position, m_cursors.front().anchor));
    document::LineNumber maxLine = minLine;
    for (const Cursor& cursor : m_cursors) {
        const document::LineNumber lo = doc.offsetToLine(std::min(cursor.position, cursor.anchor));
        const document::LineNumber hi = doc.offsetToLine(std::max(cursor.position, cursor.anchor));
        minLine = std::min(minLine, lo);
        maxLine = std::max(maxLine, hi);
    }

    std::vector<Cursor> cursors;
    cursors.reserve(maxLine - minLine + 1);
    for (document::LineNumber line = minLine; line <= maxLine; ++line) {
        const document::TextPos lineEnd = lineContentEnd(doc, line);
        cursors.push_back(Cursor{.position = lineEnd, .anchor = lineEnd, .isPrimary = (line == maxLine)});
    }
    setCursors(std::move(cursors));
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

void SelectionModel::moveCursorMatching(document::TextPos identifyingAnchor,
                                        document::TextPos newPos) {
    for (Cursor& cursor : m_cursors) {
        if (cursor.anchor == identifyingAnchor) {
            cursor.position = newPos;
            mergeOverlapping();
            return;
        }
    }
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
