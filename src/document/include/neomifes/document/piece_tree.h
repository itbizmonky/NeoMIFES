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
// The aggregates let us answer position-based queries (offset -> node,
// offset -> line, line -> offset) in O(log n). They are the reason the
// tree exists; a plain sequence container would work otherwise.
//
// Phase 2b2 Step 1 scope: insert / split / collect / validate.
// Erase, offsetToLine, lineToOffset, and the PieceTable swap arrive in
// Step 2 (next session).

#include <cstddef>
#include <cstdint>
#include <memory>
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

    // -- Read queries ---------------------------------------------------------

    [[nodiscard]] std::uint64_t totalLength()   const noexcept;
    [[nodiscard]] std::uint64_t totalNewlines() const noexcept;
    [[nodiscard]] std::size_t   pieceCount()    const noexcept { return m_pieceCount; }
    [[nodiscard]] bool          empty()         const noexcept { return m_root == nullptr; }

    // In-order traversal into a vector. O(n).
    [[nodiscard]] std::vector<Piece> collectPieces() const;

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

    // Walks from `n` up to the root recomputing aggregates on every ancestor.
    // Called after any structural change so that ancestor aggregates stay
    // consistent.
    static void updateAggregate(PieceTreeNode* n) noexcept;
    static void updateAggregatesUpward(PieceTreeNode* n) noexcept;

    // Returns the unique_ptr slot that currently owns `n`. `n` must be
    // non-null and belong to this tree.
    [[nodiscard]] std::unique_ptr<PieceTreeNode>& slotOf(PieceTreeNode* n) noexcept;

    std::unique_ptr<PieceTreeNode> m_root;
    std::size_t                    m_pieceCount = 0;
};

}  // namespace neomifes::document
