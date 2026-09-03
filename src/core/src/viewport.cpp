#include "neomifes/core/viewport.h"

#include "neomifes/document/document.h"

namespace neomifes::core {

void Viewport::ensureVisible(document::TextPos pos, const document::Document& doc) {
    const document::LineNumber line = doc.offsetToLine(pos);
    if (line < m_topLine) {
        m_topLine = line;
    } else if (m_visibleLineCount > 0 && line >= m_topLine + m_visibleLineCount) {
        m_topLine = line - m_visibleLineCount + 1;
    }

    // WI-03: horizontal counterpart, same clamp-into-window shape as above.
    // WI-21e: skipped while word wrap is on - see setWordWrapEnabled()'s own
    // comment for why a wrapped line never needs this clamp.
    if (m_wordWrapEnabled) {
        return;
    }
    const auto column = static_cast<std::uint32_t>(pos - doc.lineToOffset(line));
    if (column < m_leftColumn) {
        m_leftColumn = column;
    } else if (m_visibleColumnCount > 0 && column >= m_leftColumn + m_visibleColumnCount) {
        m_leftColumn = column - m_visibleColumnCount + 1;
    }
}

}  // namespace neomifes::core
