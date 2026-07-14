#pragma once

// Document - facade that combines PieceTable and LineIndex.
// This is what upper layers (Editor Core, Application) will use; they should
// not touch PieceTable / LineIndex directly.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "neomifes/document/line_index.h"
#include "neomifes/document/piece_table.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::document {

class BufferSnapshot;
class OriginalBuffer;

class Document {
public:
    Document();
    explicit Document(std::shared_ptr<const OriginalBuffer> original);

    Document(const Document&)            = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) noexcept        = default;
    Document& operator=(Document&&) noexcept = default;
    ~Document() = default;

    // --- Mutation ----------------------------------------------------------
    void insertText(TextPos pos, std::u16string_view text);
    void eraseRange(TextRange range);
    void replaceRange(TextRange range, std::u16string_view text);

    // --- Read ---------------------------------------------------------------
    [[nodiscard]] std::shared_ptr<const BufferSnapshot> snapshot() const {
        return m_pieceTable.snapshot();
    }
    [[nodiscard]] std::uint64_t length()    const noexcept { return m_pieceTable.length(); }
    [[nodiscard]] std::uint64_t lineCount() const noexcept { return m_pieceTable.lineCount(); }
    [[nodiscard]] std::size_t   pieceCount() const noexcept { return m_pieceTable.pieceCount(); }

    // Convenience read of the whole document.
    [[nodiscard]] std::u16string toU16String() const;

    // --- Line queries -------------------------------------------------------
    // The Document caches a LineIndex and rebuilds it lazily on the next query
    // after any mutation. This is intentionally coarse for Phase 2a; Phase 2b
    // maintains the index incrementally.
    [[nodiscard]] LineNumber offsetToLine(TextPos pos);
    [[nodiscard]] TextPos    lineToOffset(LineNumber line);

private:
    void ensureLineIndex();

    PieceTable m_pieceTable;
    LineIndex  m_lineIndex;
    bool       m_lineIndexDirty = true;
};

}  // namespace neomifes::document
