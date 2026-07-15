#include "neomifes/document/document.h"

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/original_buffer.h"

namespace neomifes::document {

Document::Document() = default;

Document::Document(std::shared_ptr<const OriginalBuffer> original)
    : m_pieceTable(std::move(original)) {}

void Document::insertText(TextPos pos, std::u16string_view text) {
    m_pieceTable.insert(pos, text);
    m_lineIndexDirty = true;
}

void Document::eraseRange(TextRange range) {
    m_pieceTable.erase(range);
    m_lineIndexDirty = true;
}

void Document::replaceRange(TextRange range, std::u16string_view text) {
    m_pieceTable.replace(range, text);
    m_lineIndexDirty = true;
}

std::u16string Document::toU16String() const {
    auto snap = m_pieceTable.snapshot();
    return snap->extract(TextRange{.start = 0, .end = snap->length()});
}

void Document::ensureLineIndex() {
    if (!m_lineIndexDirty) {
        return;
    }
    auto snap = m_pieceTable.snapshot();
    m_lineIndex.build(*snap);
    m_lineIndexDirty = false;
}

LineNumber Document::offsetToLine(TextPos pos) {
    ensureLineIndex();
    return m_lineIndex.offsetToLine(pos);
}

TextPos Document::lineToOffset(LineNumber line) {
    ensureLineIndex();
    return m_lineIndex.lineToOffset(line);
}

}  // namespace neomifes::document
