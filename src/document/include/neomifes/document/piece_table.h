#pragma once

// PieceTable - the mutable core of the Document Engine.
//
// Phase 2a MVP: pieces are stored in a std::vector<Piece>. Insertion and
// deletion locate the target piece by a linear scan on cumulative lengths.
// This is O(n) per edit in the number of pieces. It is correct and easy to
// audit; the container is deliberately hidden behind a small interface so
// Phase 2b can drop in a red-black tree with order-statistic aggregates
// (design: detailed_design.md sec.3.1, follow-up docs/issues/piece_table_rb_tree.md).
//
// Threading: PieceTable itself is single-writer. Concurrent readers use
// snapshot() which returns a shared_ptr<BufferSnapshot> that stays valid
// forever (RCU-style).

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "neomifes/document/piece.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::document {

class AddBuffer;
class BufferSnapshot;
class OriginalBuffer;

class PieceTable {
public:
    // Creates an empty document.
    PieceTable();

    // Creates a document initially backed by `original` (an immutable UTF-16
    // snapshot of the loaded file). The passed-in shared_ptr is retained.
    explicit PieceTable(std::shared_ptr<const OriginalBuffer> original);

    PieceTable(const PieceTable&)            = delete;
    PieceTable& operator=(const PieceTable&) = delete;
    PieceTable(PieceTable&&) noexcept        = default;
    PieceTable& operator=(PieceTable&&) noexcept = default;
    ~PieceTable() = default;

    // Inserts `text` at UTF-16 offset `pos`. Positions greater than length()
    // clamp to length() (append). Behaviour is defined for the empty string.
    void insert(TextPos pos, std::u16string_view text);

    // Removes the half-open range. Positions outside the document are clamped.
    // Behaviour is defined for empty ranges (no-op).
    void erase(TextRange range);

    // Convenience helper equivalent to erase(range) + insert(range.start, text).
    void replace(TextRange range, std::u16string_view text);

    // Publishes an immutable snapshot readable from any thread.
    [[nodiscard]] std::shared_ptr<const BufferSnapshot> snapshot() const;

    // Cheap accessors that reflect the current mutable state.
    [[nodiscard]] std::uint64_t length()       const noexcept { return m_totalLength;   }
    [[nodiscard]] std::uint64_t newlineCount() const noexcept { return m_totalNewlines; }
    [[nodiscard]] std::uint64_t lineCount()    const noexcept { return m_totalNewlines + 1; }
    [[nodiscard]] std::size_t   pieceCount()   const noexcept { return m_pieces.size(); }

private:
    // Counts '\n' inside a UTF-16 view - kept out-of-line to keep the header lean.
    static std::uint32_t countNewlines(std::u16string_view v) noexcept;

    // Finds the piece index containing UTF-16 offset `pos`. If `pos` falls on a
    // piece boundary, returns the piece to the *right* of the boundary. On
    // return, `posWithin` is set to the offset inside the returned piece
    // (0 when `pos` is a boundary). If the position is at end-of-document,
    // returns m_pieces.size().
    std::size_t findPiece(TextPos pos, std::uint64_t& posWithin) const noexcept;

    // Splits the piece at index `pi` so that position `posWithin` becomes a
    // piece boundary. Returns the index of the piece that starts at
    // `posWithin`. No-op when posWithin == 0 or posWithin == piece.length.
    std::size_t splitAt(std::size_t pi, std::uint64_t posWithin);

    std::shared_ptr<const OriginalBuffer> m_original;
    std::shared_ptr<AddBuffer>            m_add;      // Mutable, shared_ptr for snapshots.
    std::vector<Piece>                    m_pieces;
    std::uint64_t                         m_totalLength   = 0;
    std::uint64_t                         m_totalNewlines = 0;
};

}  // namespace neomifes::document
