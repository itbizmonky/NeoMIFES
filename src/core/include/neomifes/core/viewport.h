#pragma once

// Viewport - scroll-position bookkeeping, per detailed_design.md sec.5.2.
//
// Scope note (Phase 4a, see ADR-012): FoldingMap/setFoldingRanges are
// deferred - no folding engine exists yet (Phase 7), so a folding member
// here would be an empty, untestable stub. This class is deliberately kept
// render-agnostic (no dependency on neomifes::render / RenderPipeline) even
// though the Editor Core -> Rendering Engine dependency direction is
// sanctioned by the layer diagram (see ADR-010's precedent) - CLAUDE.md
// sec.3's "independent, concurrently runnable engines" principle is
// preferred here, and the setTopLine() bridge is Phase 4b's job (in
// src/app or src/ui, not src/core).
//
// Clamping to the document's actual line count is intentionally left to the
// same place it already happens today - RenderPipeline::drawVisibleLines()
// clamps m_topLine against lineCount() at render time - so Viewport does not
// duplicate that logic; only ensureVisible() needs a Document reference, and
// only to look up the line number of a TextPos.

#include <cstdint>

#include "neomifes/document/text_pos.h"

namespace neomifes::document {
class Document;
}

namespace neomifes::core {

struct LineRange {
    document::LineNumber start = 0;
    document::LineNumber end   = 0;  // exclusive
};

class Viewport {
public:
    void scrollTo(document::LineNumber topLine) noexcept { m_topLine = topLine; }

    // WI-03: horizontal counterpart to scrollTo() - sets the leftmost
    // visible column (UTF-16 code units from each line's start, same
    // monospace-column convention CaretDraw::column already uses).
    void scrollToColumn(std::uint32_t column) noexcept { m_leftColumn = column; }

    // Adjusts topLine/leftColumn (if needed) so the line+column containing
    // `pos` falls inside the currently visible window. The column half
    // (WI-03) mirrors the line half exactly: derives the column via
    // `pos - doc.lineToOffset(line)`, the same idiom
    // RenderPipeline::computeCaretDraws() already uses
    // (`cv.position - m_document->lineToOffset(cursorLine)`).
    void ensureVisible(document::TextPos pos, const document::Document& doc);

    void setVisibleLineCount(std::uint32_t count) noexcept { m_visibleLineCount = count; }

    // WI-03: horizontal counterpart to setVisibleLineCount().
    void setVisibleColumnCount(std::uint32_t count) noexcept { m_visibleColumnCount = count; }

    [[nodiscard]] document::LineNumber topLine() const noexcept { return m_topLine; }
    // WI-03: horizontal counterpart to topLine().
    [[nodiscard]] std::uint32_t leftColumn() const noexcept { return m_leftColumn; }
    [[nodiscard]] LineRange            visibleLines() const noexcept {
        return LineRange{.start = m_topLine, .end = m_topLine + m_visibleLineCount};
    }

private:
    document::LineNumber m_topLine            = 0;
    std::uint32_t          m_visibleLineCount   = 0;
    // WI-03: horizontal scroll state, same "0 == default/unset" convention
    // as the vertical members above.
    std::uint32_t          m_leftColumn         = 0;
    std::uint32_t          m_visibleColumnCount = 0;
};

}  // namespace neomifes::core
