#include "neomifes/core/viewport.h"

#include "neomifes/document/document.h"

namespace neomifes::core {

void Viewport::ensureVisible(document::TextPos pos, const document::Document& doc) noexcept {
    const document::LineNumber line = doc.offsetToLine(pos);
    if (line < m_topLine) {
        m_topLine = line;
        return;
    }
    if (m_visibleLineCount > 0 && line >= m_topLine + m_visibleLineCount) {
        m_topLine = line - m_visibleLineCount + 1;
    }
}

}  // namespace neomifes::core
