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

    // Monotonically increasing counter, bumped by every mutating call
    // (insertText/eraseRange/replaceRange). Single UI thread only (ADR-009)
    // - plain uint64_t, no atomics needed. Observers (RenderPipeline) compare
    // this against the version they last cached a snapshot at, per
    // detailed_design.md sec.4.3's "don't call snapshot() every frame" rule
    // (see ADR-010).
    [[nodiscard]] std::uint64_t version() const noexcept { return m_version; }

    // Convenience read of the whole document.
    [[nodiscard]] std::u16string toU16String() const;

    // --- Line queries -------------------------------------------------------
    // The Document caches a LineIndex and rebuilds it lazily on the next query
    // after any mutation. This is intentionally coarse for Phase 2a; Phase 2b
    // maintains the index incrementally. Logically const (callers only ever
    // observe query results, never the cache itself) - m_lineIndex/
    // m_lineIndexDirty are `mutable` so RenderPipeline can query through a
    // `const Document*` (ADR-010).
    [[nodiscard]] LineNumber offsetToLine(TextPos pos) const;
    [[nodiscard]] TextPos    lineToOffset(LineNumber line) const;

private:
    void ensureLineIndex() const;

    PieceTable            m_pieceTable;
    mutable LineIndex     m_lineIndex;
    mutable bool          m_lineIndexDirty = true;
    std::uint64_t         m_version        = 0;
};

}  // namespace neomifes::document
