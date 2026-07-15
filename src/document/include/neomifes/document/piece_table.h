#pragma once

// PieceTable - the mutable core of the Document Engine.
//
// Phase 2b2: pieces are stored in a PieceTree (mutable Red-Black tree with
// order-statistic aggregates, per ADR-007). Insert/erase locate the target
// piece in O(log n). snapshot() walks the tree in-order to materialise a
// std::vector<Piece> for BufferSnapshot - O(n pieces), which ADR-007
// deliberately accepts (see ADR-007 sec."根拠" for why O(1) persistent
// snapshots were not worth the implementation risk).
//
// Threading: PieceTable itself is single-writer. Concurrent readers use
// snapshot() which returns a shared_ptr<BufferSnapshot> holding an
// independent copy of the piece list - readers never touch the live tree.

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "neomifes/document/piece.h"
#include "neomifes/document/piece_tree.h"
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
    [[nodiscard]] std::uint64_t length()       const noexcept { return m_tree.totalLength();   }
    [[nodiscard]] std::uint64_t newlineCount() const noexcept { return m_tree.totalNewlines();  }
    [[nodiscard]] std::uint64_t lineCount()    const noexcept { return m_tree.totalNewlines() + 1; }
    [[nodiscard]] std::size_t   pieceCount()   const noexcept { return m_tree.pieceCount(); }

private:
    // Counts '\n' inside a UTF-16 view - kept out-of-line to keep the header lean.
    static std::uint32_t countNewlines(std::u16string_view v) noexcept;

    // Ensures `pos` is a piece boundary in the tree. If `pos` falls strictly
    // inside an existing piece, computes how many newlines precede `pos`
    // within that piece (by reading the backing buffer - the tree itself has
    // no buffer access) and calls m_tree.splitPieceAt accordingly. No-op if
    // `pos` is already a boundary.
    void ensureBoundary(TextPos pos);

    std::shared_ptr<const OriginalBuffer> m_original;
    std::shared_ptr<AddBuffer>            m_add;      // Mutable, shared_ptr for snapshots.
    PieceTree                             m_tree;
};

}  // namespace neomifes::document
