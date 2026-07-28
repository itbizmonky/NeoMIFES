#include "neomifes/document/document.h"

#include "neomifes/document/buffer_snapshot.h"
#include "neomifes/document/original_buffer.h"

namespace neomifes::document {

Document::Document() = default;

Document::Document(std::shared_ptr<const OriginalBuffer> original)
    : m_pieceTable(std::move(original)) {}

void Document::insertText(TextPos pos, std::u16string_view text) {
    // Old side captured BEFORE mutating (pre-edit line structure) - erasing
    // nothing means oldEnd == start. New side captured AFTER (post-edit),
    // forcing the LineIndex rebuild that would happen on the next query
    // anyway (see EditDelta's header comment).
    const LineNumber startLine   = offsetToLine(pos);
    const auto       startColumn = static_cast<std::uint32_t>(pos - lineToOffset(startLine));
    m_pieceTable.insert(pos, text);
    m_lineIndexDirty = true;
    ++m_version;
    const TextPos    newEnd       = pos + text.size();
    const LineNumber newEndLine   = offsetToLine(newEnd);
    const auto       newEndColumn = static_cast<std::uint32_t>(newEnd - lineToOffset(newEndLine));
    m_pendingEdits.push_back(EditDelta{.startPos     = pos,
                                        .startLine    = startLine,
                                        .startColumn  = startColumn,
                                        .oldEndPos    = pos,
                                        .oldEndLine   = startLine,
                                        .oldEndColumn = startColumn,
                                        .newEndPos    = newEnd,
                                        .newEndLine   = newEndLine,
                                        .newEndColumn = newEndColumn});
}

void Document::eraseRange(TextRange range) {
    const LineNumber startLine    = offsetToLine(range.start);
    const auto       startColumn  = static_cast<std::uint32_t>(range.start - lineToOffset(startLine));
    const LineNumber oldEndLine   = offsetToLine(range.end);
    const auto       oldEndColumn = static_cast<std::uint32_t>(range.end - lineToOffset(oldEndLine));
    m_pieceTable.erase(range);
    m_lineIndexDirty = true;
    ++m_version;
    // Nothing is inserted, so the new end equals the (now post-edit) start.
    m_pendingEdits.push_back(EditDelta{.startPos     = range.start,
                                        .startLine    = startLine,
                                        .startColumn  = startColumn,
                                        .oldEndPos    = range.end,
                                        .oldEndLine   = oldEndLine,
                                        .oldEndColumn = oldEndColumn,
                                        .newEndPos    = range.start,
                                        .newEndLine   = startLine,
                                        .newEndColumn = startColumn});
}

void Document::replaceRange(TextRange range, std::u16string_view text) {
    const LineNumber startLine    = offsetToLine(range.start);
    const auto       startColumn  = static_cast<std::uint32_t>(range.start - lineToOffset(startLine));
    const LineNumber oldEndLine   = offsetToLine(range.end);
    const auto       oldEndColumn = static_cast<std::uint32_t>(range.end - lineToOffset(oldEndLine));
    m_pieceTable.replace(range, text);
    m_lineIndexDirty = true;
    ++m_version;
    const TextPos    newEnd       = range.start + text.size();
    const LineNumber newEndLine   = offsetToLine(newEnd);
    const auto       newEndColumn = static_cast<std::uint32_t>(newEnd - lineToOffset(newEndLine));
    m_pendingEdits.push_back(EditDelta{.startPos     = range.start,
                                        .startLine    = startLine,
                                        .startColumn  = startColumn,
                                        .oldEndPos    = range.end,
                                        .oldEndLine   = oldEndLine,
                                        .oldEndColumn = oldEndColumn,
                                        .newEndPos    = newEnd,
                                        .newEndLine   = newEndLine,
                                        .newEndColumn = newEndColumn});
}

std::u16string Document::toU16String() const {
    auto snap = m_pieceTable.snapshot();
    return snap->extract(TextRange{.start = 0, .end = snap->length()});
}

void Document::ensureLineIndex() const {
    if (!m_lineIndexDirty) {
        return;
    }
    auto snap = m_pieceTable.snapshot();
    m_lineIndex.build(*snap);
    m_lineIndexDirty = false;
}

LineNumber Document::offsetToLine(TextPos pos) const {
    ensureLineIndex();
    return m_lineIndex.offsetToLine(pos);
}

TextPos Document::lineToOffset(LineNumber line) const {
    ensureLineIndex();
    return m_lineIndex.lineToOffset(line);
}

}  // namespace neomifes::document
