#pragma once

// PieceTree - order-statistic Red-Black tree over Piece values.
//
// Nodes are mutable and owned via std::unique_ptr (parent -> child). The
// tree itself is single-writer / snapshot-copies-list, per ADR-007.
//
// Aggregates maintained on every node:
//   subtreeLength    -- sum of piece.length across this subtree
//   subtreeNewlines  -- sum of piece.newlineCount across this subtree
//   subtreeCount     -- number of pieces in this subtree
//
// What the aggregates DO give us in O(log n):
//   - locating the piece (node) that contains an arbitrary offset
//     (pieceContainingStrictly / findNodeStartingAt)
//   - totalLength() / totalNewlines() / lineCount() (== totalNewlines()+1)
//
// What they do NOT give us: offsetToLine(pos) / lineToOffset(line) for an
// arbitrary position INSIDE a piece. subtreeNewlines only stores a per-piece
// COUNT, not the individual newline OFFSETS within a piece's text - and the
// tree has no access to the backing buffers to compute those on demand (by
// design; see docs/issues/piece_table_rb_tree.md and the newer
// docs/issues/line_index_o_log_n.md for why this was deferred rather than
// solved here). LineIndex therefore keeps its Phase 2b1 O(N)-rebuild /
// O(log n)-query design unchanged.
//
// Phase 2b2 Step 1 scope (done): insert / split / collect / validate.
// Phase 2b2 Step 2 scope (this file): pieceContainingStrictly,
// findNodeStartingAt, eraseRange (CLRS 13.4 delete + fixup).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "neomifes/document/piece.h"
#include "neomifes/document/text_pos.h"

namespace neomifes::document {

enum class RbColor : std::uint8_t { Red = 0, Black = 1 };

struct PieceTreeNode {
    Piece                          piece;
    RbColor                        color = RbColor::Red;   // new nodes start red
    PieceTreeNode*                 parent = nullptr;       // raw, non-owning
    std::unique_ptr<PieceTreeNode> left;                   // owning
    std::unique_ptr<PieceTreeNode> right;                  // owning

    // Aggregates - recomputed by updateAggregate() after every structural change.
    std::uint64_t subtreeLength   = 0;
    std::uint64_t subtreeNewlines = 0;
    std::size_t   subtreeCount    = 0;

    explicit PieceTreeNode(const Piece& p) noexcept;
};

class PieceTree {
public:
    PieceTree() noexcept = default;
    ~PieceTree() = default;                                   // unique_ptr chain frees recursively

    PieceTree(PieceTree&&) noexcept = default;
    PieceTree& operator=(PieceTree&&) noexcept = default;
    PieceTree(const PieceTree&)            = delete;
    PieceTree& operator=(const PieceTree&) = delete;

    // -- Structural mutations -------------------------------------------------

    // Inserts `piece` as a new node so that the new node's logical start
    // position is exactly `pos`. Requires `pos` to be 0, totalLength(), or an
    // existing piece boundary. Positions strictly inside an existing piece are
    // a caller error (asserts in debug, coerced to the containing piece's start
    // in release - callers must call splitPieceAt beforehand).
    // Positions greater than totalLength() are clamped to totalLength().
    void insertAt(TextPos pos, const Piece& piece);

    // Splits the piece containing `pos` at `pos`, so `pos` becomes a boundary
    // in the tree. `pos` MUST fall strictly inside a piece (0 < offsetWithin
    // < piece.length). The caller supplies leftNewlines because the tree does
    // not have access to the backing buffers.
    // No-op (with a debug assert) if pos is already at a boundary.
    void splitPieceAt(TextPos pos, std::uint32_t leftNewlines);

    // Removes all pieces in [range.start, range.end). PRECONDITION: both
    // range.start and range.end must already be piece boundaries (0,
    // totalLength(), or an existing piece's start) - callers must call
    // splitPieceAt beforehand for positions that fall strictly inside a
    // piece. No-op if range.start >= range.end after clamping range.end to
    // totalLength().
    void eraseRange(TextRange range);

    // -- Read queries ---------------------------------------------------------

    [[nodiscard]] std::uint64_t totalLength()   const noexcept;
    [[nodiscard]] std::uint64_t totalNewlines() const noexcept;
    [[nodiscard]] std::size_t   pieceCount()    const noexcept { return m_pieceCount; }
    [[nodiscard]] bool          empty()         const noexcept { return m_root == nullptr; }

    // In-order traversal into a vector. O(n).
    [[nodiscard]] std::vector<Piece> collectPieces() const;

    struct PieceLookup {
        Piece   piece;
        TextPos pieceStart;   // logical (absolute) offset where `piece` begins
    };

    // Returns the piece that STRICTLY contains `pos` (0 < pos-pieceStart <
    // piece.length). Returns std::nullopt when `pos` already coincides with
    // an existing boundary (0, totalLength(), or some piece's start) or the
    // tree is empty - meaning "no split needed." O(log n).
    [[nodiscard]] std::optional<PieceLookup> pieceContainingStrictly(TextPos pos) const noexcept;

    // -- Test / debug ---------------------------------------------------------

    // Verifies:
    //   RB1  root is black (if non-empty)
    //   RB2  no red node has a red child
    //   RB3  every root-to-leaf path traverses the same number of black nodes
    //   PP   parent pointers are consistent with owning unique_ptr edges
    //   AG   subtreeLength / subtreeNewlines / subtreeCount match a fresh
    //        bottom-up recomputation
    // Returns true iff every invariant holds. Used by tests.
    [[nodiscard]] bool validate() const noexcept;

private:
    // Rotation and RB fixup helpers. Kept as private members so they can
    // update `m_root` when a rotation makes a new node the root.
    void rotateLeft(PieceTreeNode* x);
    void rotateRight(PieceTreeNode* x);
    void insertFixup(PieceTreeNode* z);

    // CLRS 13.4 RB-DELETE-FIXUP, adapted for nullptr-as-sentinel. `x` may be
    // null; `xParent` is tracked explicitly because a null x has no parent
    // pointer of its own to consult.
    void eraseFixup(PieceTreeNode* x, PieceTreeNode* xParent);

    // Removes `node` from the tree (standard CLRS RB-DELETE, adapted for
    // unique_ptr ownership) and runs eraseFixup if needed. Decrements
    // m_pieceCount and refreshes aggregates on the affected ancestor chain.
    void eraseNode(PieceTreeNode* z);

    // Returns the node whose OWN piece begins exactly at absolute position
    // `pos`. `pos` must be an existing boundary; returns nullptr only if the
    // tree is empty or pos == totalLength() (no node starts there).
    [[nodiscard]] PieceTreeNode* findNodeStartingAt(TextPos pos) const noexcept;

    // Walks from `n` up to the root recomputing aggregates on every ancestor.
    // Called after any structural change so that ancestor aggregates stay
    // consistent. Safe to call with a post-rotation node: it always follows
    // the CURRENT parent chain, so it correctly re-covers every node that
    // rotations touched regardless of how the local topology shifted.
    static void updateAggregate(PieceTreeNode* n) noexcept;
    static void updateAggregatesUpward(PieceTreeNode* n) noexcept;

    // Returns the unique_ptr slot that currently owns `n`. `n` must be
    // non-null and belong to this tree.
    [[nodiscard]] std::unique_ptr<PieceTreeNode>& slotOf(PieceTreeNode* n) noexcept;

    std::unique_ptr<PieceTreeNode> m_root;
    std::size_t                    m_pieceCount = 0;
};

}  // namespace neomifes::document
